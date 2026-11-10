// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Steam leaderboards for owned games served by the Steam API provider
// (gbe_fork; AGENTS.md rule 3). gbe_fork answers ISteamUserStats leaderboard
// calls from its own disk cache and its LAN peer network, so with no peers the
// game sees an empty board. The launcher asks the real Steam backend over its
// own signed-in CM session (CMsgClientLBSFindOrCreateLB / GetLBEntries /
// SetScore) and hands the rows to the provider through the one channel it
// documents: its leaderboard cache file, which it reads the first time a board
// is found. Nothing on disk changes and the provider's own callbacks still run.
//
// Portable: the CM transport and logging are injected, so the wire shapes, the
// provider's file format and the fetch policy are tested natively.
// Message shapes follow SteamDatabase/Protobufs (public domain) as vendored in
// SteamKit2 / steam-user; see research/STEAMWORKS-MULTIPLAYER.md.
#include "steam_wire.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace steam_leaderboards {
namespace fs = std::filesystem;
// EMsg values from enums_clientserver.proto (k_EMsgClientLBS*).
enum : uint32_t {
    ClientLBSSetScore = 5414, ClientLBSSetScoreResponse = 5415,
    ClientLBSFindOrCreateLB = 5416, ClientLBSFindOrCreateLBResponse = 5417,
    ClientLBSGetLBEntries = 5418, ClientLBSGetLBEntriesResponse = 5419,
};
// ELeaderboardDataRequest / ELeaderboardSortMethod / ELeaderboardDisplayType.
enum : int { DataGlobal = 0, DataGlobalAroundUser = 1, DataFriends = 2, DataUsers = 3 };
enum : int { SortNone = 0, SortAscending = 1, SortDescending = 2 };
enum : int { DisplayNone = 0, DisplayNumeric = 1, DisplayTimeSeconds = 2, DisplayTimeMilliSeconds = 3 };
// ELeaderboardUploadScoreMethod.
enum : int { UploadNone = 0, UploadKeepBest = 1, UploadForceUpdate = 2 };
// k_cchLeaderboardNameMax and k_cLeaderboardDetailsMax.
enum : unsigned { NameMax = 128, DetailsMax = 64 };
// How many rows one seed asks Steam for: enough for a rank screen's first page
// and its surroundings without a large response.
enum : int { SeedEntries = 100 };

struct Io {
    // One CM request -> one response. `routing_appid` goes in the message
    // header: Steam only answers an LBS request that carries the app id.
    std::function<bool(uint32_t emsg, uint32_t routing_appid, const std::string& body, std::string& response)> call;
    std::function<void(const std::string&)> log;
};
struct Entry { uint64_t steamid = 0; int32_t rank = 0, score = 0; std::string details; };
struct Board { bool found = false; int32_t eresult = 2, id = 0, sort = 0, display = 0, count = 0; };
struct Score { bool ok = false; int32_t entryCount = 0, rankPrevious = 0, rankNew = 0; bool changed = false; };

// ---- wire
// CMsgClientLBSFindOrCreateLB: app_id (1), leaderboard_sort_method (2),
// leaderboard_display_type (3), create_if_not_found (4), leaderboard_name (5).
inline std::string find_body(uint32_t appid, const std::string& name, int sort, int display, bool create) {
    std::string b;
    pb_u(b, 1, appid); pb_u(b, 2, uint64_t(int64_t(sort))); pb_u(b, 3, uint64_t(int64_t(display)));
    pb_u(b, 4, create ? 1 : 0); pb_s(b, 5, name);
    return b;
}
// CMsgClientLBSFindOrCreateLBResponse: eresult (1), leaderboard_id (2),
// leaderboard_entry_count (3), ...sort_method (4), ...display_type (5), name (6).
inline Board parse_find(const std::string& body, int32_t headerEresult) {
    Board out; int32_t eresult = headerEresult;
    for (auto& f : pb_parse(body)) {
        if (f.f == 1) eresult = int32_t(f.u);
        else if (f.f == 2) out.id = int32_t(f.u);
        else if (f.f == 3) out.count = int32_t(f.u);
        else if (f.f == 4) out.sort = int32_t(f.u);
        else if (f.f == 5) out.display = int32_t(f.u);
    }
    out.eresult = eresult;
    out.found = eresult == 1;
    return out;
}
// CMsgClientLBSGetLBEntries: app_id (1), leaderboard_id (2), range_start (3),
// range_end (4), leaderboard_data_request (5), steamids (6, repeated fixed64).
inline std::string entries_body(uint32_t appid, int32_t id, int request, int start, int end) {
    std::string b;
    pb_u(b, 1, appid); pb_u(b, 2, uint64_t(int64_t(id)));
    pb_u(b, 3, uint64_t(int64_t(start))); pb_u(b, 4, uint64_t(int64_t(end)));
    pb_u(b, 5, uint64_t(int64_t(request)));
    return b;
}
inline std::string entries_for_users_body(uint32_t appid, int32_t id, const std::vector<uint64_t>& users) {
    std::string b = entries_body(appid, id, DataUsers, 0, 0);
    for (auto u : users) pb_fixed64(b, 6, u);
    return b;
}
// CMsgClientLBSGetLBEntriesResponse: eresult (1), leaderboard_entry_count (2),
// entries (3) -> steam_id_user (1), global_rank (2), score (3), details (4),
// ugc_id (5). `details` is the packed int32 score-detail array.
inline bool parse_entries(const std::string& body, int32_t headerEresult, std::vector<Entry>& out, int32_t& count) {
    out.clear(); count = 0; int32_t eresult = headerEresult;
    for (auto& f : pb_parse(body)) {
        if (f.f == 1) eresult = int32_t(f.u);
        else if (f.f == 2) count = int32_t(f.u);
        else if (f.f == 3) {
            Entry e;
            for (auto& x : pb_parse(f.s)) {
                if (x.f == 1) e.steamid = x.u;
                else if (x.f == 2) e.rank = int32_t(x.u);
                else if (x.f == 3) e.score = int32_t(x.u);
                else if (x.f == 4) e.details = x.s;
            }
            out.push_back(std::move(e));
        }
    }
    return eresult == 1;
}
// CMsgClientLBSSetScore: app_id (1), leaderboard_id (2), score (3),
// details (4), upload_score_method (5).
inline std::string score_body(uint32_t appid, int32_t id, int32_t score, const int32_t* details, int count, int method) {
    std::string b;
    pb_u(b, 1, appid); pb_u(b, 2, uint64_t(int64_t(id))); pb_u(b, 3, uint64_t(int64_t(score)));
    if (details && count > 0) {
        std::string raw(size_t(count) * 4, '\0');
        memcpy(&raw[0], details, raw.size());
        pb_s(b, 4, raw);
    }
    pb_u(b, 5, uint64_t(int64_t(method)));
    return b;
}
// CMsgClientLBSSetScoreResponse: eresult (1), leaderboard_entry_count (2),
// score_changed (3), global_rank_previous (4), global_rank_new (5).
inline Score parse_score(const std::string& body, int32_t headerEresult) {
    Score out; int32_t eresult = headerEresult;
    for (auto& f : pb_parse(body)) {
        if (f.f == 1) eresult = int32_t(f.u);
        else if (f.f == 2) out.entryCount = int32_t(f.u);
        else if (f.f == 3) out.changed = f.u != 0;
        else if (f.f == 4) out.rankPrevious = int32_t(f.u);
        else if (f.f == 5) out.rankNew = int32_t(f.u);
    }
    out.ok = eresult == 1;
    return out;
}

// ---- the provider's leaderboard cache file
// gbe_fork stores a board as a flat array of little-endian uint32:
//   steamid low | steamid high | score | details count | details...
// repeated per entry (dll/steam_user_stats_leaderboard.cpp). It reads the file
// once, the first time the board is found, and sorts it itself.
inline std::string encode_cache(const std::vector<Entry>& entries) {
    std::string out;
    auto put = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out += char(v >> (8 * i)); };
    for (auto& e : entries) {
        put(uint32_t(e.steamid & 0xffffffffu)); put(uint32_t(e.steamid >> 32));
        put(uint32_t(e.score));
        size_t n = e.details.size() / 4;
        if (n > DetailsMax) n = DetailsMax;
        put(uint32_t(n));
        for (size_t i = 0; i < n; ++i) {
            uint32_t d; memcpy(&d, e.details.data() + i * 4, 4); put(d);
        }
    }
    return out;
}
// The provider lowercases the name and sanitises it before using it as a file
// name; a name that would escape the leaderboard folder is refused instead.
inline std::optional<std::string> cache_name(const std::string& name) {
    if (name.empty() || name.size() > NameMax) return std::nullopt;
    std::string out;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '"' || c == '?' || c == '%' || c == '\0') return std::nullopt;
        out += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return out;
}
// <provider saves>/<appid>/leaderboard/<lowercased name>
inline std::optional<fs::path> cache_path(const fs::path& savesRoot, uint32_t appid, const std::string& name) {
    auto file = cache_name(name);
    if (!file) return std::nullopt;
    return savesRoot / std::to_string(appid) / "leaderboard" / *file;
}
inline bool write_cache(const fs::path& path, const std::vector<Entry>& entries) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    auto tmp = path; tmp += ".xboxnative-part";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        auto data = encode_cache(entries);
        out.write(data.data(), std::streamsize(data.size()));
        if (!out) return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
}

// ---- the fetch
// Asks Steam for the board and its top rows, then writes the provider's cache
// file so the game reads real players on its next (or current) Find. `create`
// mirrors the API call the game made: only FindOrCreateLeaderboard may create.
struct Fetch { bool ok = false; Board board; std::vector<Entry> entries; };
// One FindOrCreateLB request. `board.found` is the only verdict: a board Steam
// has is addressable from here on, even if its rows do not arrive.
inline Board find_board(Io& io, uint32_t appid, const std::string& name, int sort, int display, bool create) {
    std::string resp;
    if (!io.call(ClientLBSFindOrCreateLB, appid, find_body(appid, name, sort, display, create), resp)) {
        io.log("leaderboards: '" + name + "' request failed (transport)");
        return Board{};
    }
    auto board = parse_find(resp, 2);
    if (!board.found) io.log("leaderboards: '" + name + "' unavailable (eresult " + std::to_string(board.eresult) + ")");
    return board;
}
// One GetLBEntries request for a board whose id is already known.
inline bool fetch_rows(Io& io, uint32_t appid, const std::string& name, int32_t id,
                       std::vector<Entry>& entries, int32_t& count) {
    std::string resp;
    if (!io.call(ClientLBSGetLBEntries, appid, entries_body(appid, id, DataGlobal, 1, SeedEntries), resp)) {
        io.log("leaderboards: '" + name + "' rows request failed (transport)");
        return false;
    }
    if (!parse_entries(resp, 2, entries, count)) {
        io.log("leaderboards: '" + name + "' rows unavailable");
        return false;
    }
    return true;
}
inline Fetch fetch(Io& io, uint32_t appid, const std::string& name, int sort, int display, bool create) {
    Fetch out;
    out.board = find_board(io, appid, name, sort, display, create);
    if (!out.board.found) return out;
    int32_t count = 0;
    out.ok = fetch_rows(io, appid, name, out.board.id, out.entries, count);
    return out;
}
inline Score upload(Io& io, uint32_t appid, int32_t id, int32_t score, const int32_t* details, int count, int method) {
    Score out; std::string resp;
    if (!io.call(ClientLBSSetScore, appid, score_body(appid, id, score, details, count, method), resp)) {
        io.log("leaderboards: score not sent (transport)");
        return out;
    }
    return parse_score(resp, 2);
}

// ---- the bridge policy
// The provider reads a board's cache file once, the first time that board is
// found in the process, so the seed must run before the provider's own Find.
// One round trip per board per launch; a board Steam does not have is not asked
// again. The launcher holds one of these per launch and calls it from the
// provider's export tap.
struct Bridge {
    // name -> Steam leaderboard id. -1: asked, and Steam had nothing (or the
    // request failed), so do not ask again this launch.
    std::map<std::string, int32_t> ids;
    // Names whose rows are in the provider's cache file. Kept apart from `ids`
    // because a board can be known while its rows never arrived.
    std::map<std::string, bool> seeded;
    // The freshest rows Steam sent for a board, and when. The provider caches a
    // board in memory on its first Find and never re-reads the file, so a later
    // Find cannot refresh it; these rows are what the launcher hands the game
    // instead (play_runtime's GetDownloadedLeaderboardEntry tap), and the ids
    // the launcher asks Steam for persona names.
    std::map<std::string, std::vector<Entry>> rows;
    std::map<std::string, double> rowsAt;
    // A score this account uploaded changed the board; the next Find refetches
    // even inside the TTL.
    std::set<std::string> dirty;
    // How long a row set is trusted before a Find refetches it. Short on
    // purpose: the owner asked to see other players' scores move.
    double ttl = 30.0;
    // Serialises seeds: the check and the round trip must not interleave, and
    // the caller's own lock must not be held across the network wait.
    mutable std::mutex m;

    // Returns true when the provider's cache file (or the launcher's own rows)
    // holds rows for `name`. `now` is seconds on any monotonic clock.
    bool seed(Io& io, uint32_t appid, const fs::path& savesRoot, const std::string& name,
              int sort, int display, bool create, double now = 0) {
        if (name.empty()) return false;
        std::lock_guard<std::mutex> lock(m);
        int32_t id = 0;
        bool refresh = false;
        auto known = ids.find(name);
        if (known == ids.end()) {
            auto board = find_board(io, appid, name, sort, display, create);
            if (!board.found) { ids[name] = -1; return false; }
            // The board exists on Steam, so its id is kept even when the rows do
            // not arrive: a score for it can still be uploaded this launch.
            ids[name] = board.id;
            id = board.id;
            refresh = true;
        } else if (known->second <= 0) {
            return false;   // asked, Steam had nothing: the provider's answer stands
        } else {
            id = known->second;
            auto at = rowsAt.find(name);
            refresh = dirty.count(name) != 0 || at == rowsAt.end() || now - at->second >= ttl;
        }
        bool had = seeded.find(name) != seeded.end() && seeded[name];
        if (!refresh) return had;
        std::vector<Entry> entries; int32_t count = 0;
        bool got = fetch_rows(io, appid, name, id, entries, count);
        if (!got || entries.empty()) {
            // Keep whatever the last successful fetch left: a failed refresh is
            // not a reason to show the game less than it already had.
            char line[240];
            if (had)
                snprintf(line, sizeof line, "leaderboards: '%s' refresh unavailable (id %d); keeping %zu rows",
                         name.c_str(), id, rows.count(name) ? rows[name].size() : size_t(0));
            else
                snprintf(line, sizeof line, "leaderboards: '%s' seeded 0 rows (id %d, %d on Steam, file %s)",
                         name.c_str(), id, count, got ? "empty" : "unavailable");
            io.log(line);
            return had;
        }
        rows[name] = entries;
        rowsAt[name] = now;
        dirty.erase(name);
        bool stored = false;
        auto path = cache_path(savesRoot, appid, name);
        stored = path && write_cache(*path, entries);
        if (!stored) io.log("leaderboards: '" + name + "' rows could not be stored");
        seeded[name] = true;
        char line[240];
        if (had) {
            snprintf(line, sizeof line, "leaderboards: '%s' refreshed %zu rows (id %d, %d on Steam)",
                     name.c_str(), entries.size(), id, count);
        } else {
            snprintf(line, sizeof line, "leaderboards: '%s' seeded %zu rows (id %d, %d on Steam, file %s)",
                     name.c_str(), entries.size(), id, count, stored ? "written" : "empty");
        }
        io.log(line);
        return true;
    }
    // A score the game uploaded. The id is the one the seed learned for this
    // board; without it there is nothing to address, so nothing is sent.
    bool upload(Io& io, uint32_t appid, const std::string& name, int32_t score,
                const int32_t* details, int count, int method) {
        std::lock_guard<std::mutex> lock(m);
        auto known = ids.find(name);
        if (known == ids.end() || known->second <= 0) return false;
        auto up = steam_leaderboards::upload(io, appid, known->second, score, details, count, method);
        if (!up.ok) return false;
        // The board changed on Steam's side; the next Find must not answer from
        // the rows fetched before the upload.
        dirty.insert(name);
        char line[240];
        snprintf(line, sizeof line, "leaderboards: score %d sent to '%s' (changed=%d, rank %d -> %d)",
                 score, name.c_str(), int(up.changed), up.rankPrevious, up.rankNew);
        io.log(line);
        return true;
    }
    // The rows the launcher holds for a board, or nullptr. Used by the
    // GetDownloadedLeaderboardEntry tap to hand the game fresher rows than the
    // provider cached at its first Find, and by the seed to ask Steam for the
    // persona names of the players on it.
    const std::vector<Entry>* entries(const std::string& name) const {
        std::lock_guard<std::mutex> lock(m);
        auto it = rows.find(name);
        return it == rows.end() ? nullptr : &it->second;
    }
    void reset() {
        std::lock_guard<std::mutex> lock(m);
        ids.clear(); seeded.clear(); rows.clear(); rowsAt.clear(); dirty.clear();
    }
};
} // namespace steam_leaderboards
