// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the Steam leaderboard bridge against a fake CM service
// that answers with the real CMsgClientLBS* shapes, and for the exact file the
// Steam API provider (gbe_fork) reads.
#include "../steam_leaderboards.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <map>
using namespace steam_leaderboards;

// A fake Steam backend: boards keyed by (appid, name), rows sorted the way
// Steam sorts them, and score uploads applied with the API's keep-best rule.
struct Server {
    struct Row { uint64_t steamid; int32_t score; std::string details; };
    std::map<std::string, int32_t> ids;                 // "appid/name" -> leaderboard id
    std::map<int32_t, std::vector<Row>> rows;           // id -> rows
    std::map<int32_t, int> sort;                        // id -> sort method
    unsigned finds = 0, gets = 0, sets = 0;
    std::vector<std::pair<uint32_t, uint32_t>> routed;  // (emsg, routing_appid)

    int32_t ensure(uint32_t appid, const std::string& name, int sortMethod, bool create) {
        auto key = std::to_string(appid) + "/" + name;
        auto it = ids.find(key);
        if (it != ids.end()) return it->second;
        if (!create) return 0;
        int32_t id = int32_t(ids.size()) + 1;
        ids[key] = id; sort[id] = sortMethod;
        return id;
    }
    bool call(uint32_t emsg, uint32_t routing, const std::string& body, std::string& resp) {
        routed.emplace_back(emsg, routing);
        auto f = pb_parse(body);
        resp.clear();
        if (emsg == ClientLBSFindOrCreateLB) {
            ++finds;
            uint32_t appid = uint32_t(pb_get(f, 1)->u);
            auto name = pb_get(f, 5)->s;
            int sortMethod = int32_t(pb_get(f, 2)->u), display = int32_t(pb_get(f, 3)->u);
            bool create = pb_get(f, 4)->u != 0;
            int32_t id = ensure(appid, name, sortMethod, create);
            if (!id) { pb_u(resp, 1, 9 /* k_EResultNoMatch */); return true; }
            pb_u(resp, 1, 1); pb_u(resp, 2, uint64_t(int64_t(id)));
            pb_u(resp, 3, rows[id].size()); pb_u(resp, 4, uint64_t(sort[id])); pb_u(resp, 5, uint64_t(display));
            pb_s(resp, 6, name);
            return true;
        }
        if (emsg == ClientLBSGetLBEntries) {
            ++gets;
            int32_t id = int32_t(pb_get(f, 2)->u);
            int request = int32_t(pb_get(f, 5)->u);
            int start = int32_t(pb_get(f, 3)->u), end = int32_t(pb_get(f, 4)->u);
            auto& all = rows[id];
            std::vector<Row> sorted = all;
            const bool asc = sort[id] == SortAscending;
            std::sort(sorted.begin(), sorted.end(), [asc](const Row& a, const Row& b) {
                return asc ? a.score < b.score : a.score > b.score;
            });
            pb_u(resp, 1, 1); pb_u(resp, 2, sorted.size());
            // Ranks are global (1-based) in the sorted table, whatever the range.
            for (size_t i = 0; i < sorted.size(); ++i) {
                int rank = int(i) + 1;
                if (request == DataGlobal && (rank < start || rank > end)) continue;
                std::string e;
                pb_fixed64(e, 1, sorted[i].steamid); pb_u(e, 2, uint64_t(rank));
                pb_u(e, 3, uint64_t(int64_t(sorted[i].score))); if (!sorted[i].details.empty()) pb_s(e, 4, sorted[i].details);
                pb_s(resp, 3, e);
            }
            return true;
        }
        if (emsg == ClientLBSSetScore) {
            ++sets;
            int32_t id = int32_t(pb_get(f, 2)->u);
            int32_t score = int32_t(pb_get(f, 3)->u);
            int method = int32_t(pb_get(f, 5)->u);
            std::string details = pb_get(f, 4) ? pb_get(f, 4)->s : std::string();
            auto& all = rows[id];
            int32_t before = 0;
            for (size_t i = 0; i < all.size(); ++i) if (all[i].steamid == 0x110000100000001ull) before = all[i].score;
            bool changed = false;
            auto it = std::find_if(all.begin(), all.end(), [](const Row& r) { return r.steamid == 0x110000100000001ull; });
            if (it == all.end()) { all.push_back({0x110000100000001ull, score, details}); changed = true; }
            else if (method == UploadForceUpdate || (sort[id] == SortAscending ? score < it->score : score > it->score)) {
                changed = it->score != score; it->score = score; it->details = details;
            }
            pb_u(resp, 1, 1); pb_u(resp, 2, all.size()); pb_u(resp, 3, changed ? 1 : 0);
            pb_u(resp, 4, uint64_t(int64_t(before)));
            return true;
        }
        return false;
    }
};

static std::string details_of(std::initializer_list<int32_t> v) {
    std::string out;
    for (auto x : v) { out += char(x & 0xff); out += char((x >> 8) & 0xff); out += char((x >> 16) & 0xff); out += char((x >> 24) & 0xff); }
    return out;
}

int main() {
    Server server;
    std::vector<std::string> log;
    Io io;
    io.call = [&](uint32_t emsg, uint32_t routing, const std::string& body, std::string& resp) {
        return server.call(emsg, routing, body, resp);
    };
    io.log = [&](const std::string& line) { log.push_back(line); };

    // A board Steam already has, with three players on it.
    server.ensure(3405340, "kills_all_time", SortDescending, true);
    int32_t id = server.ids["3405340/kills_all_time"];
    server.rows[id] = {{0x110000100000002ull, 900, details_of({1, 2})},
                       {0x110000100000003ull, 1500, {}},
                       {0x110000100000004ull, 400, details_of({7})}};

    // ---- find + rows
    auto got = fetch(io, 3405340, "kills_all_time", SortDescending, DisplayNumeric, false);
    assert(got.ok && got.board.found);
    assert(got.board.id == id && got.board.count == 3);
    assert(got.entries.size() == 3);
    // Every request carried the app id in its header: Steam answers nothing otherwise.
    assert(server.routed.size() == 2);
    for (auto& r : server.routed) assert(r.second == 3405340);
    assert(server.routed[0].first == ClientLBSFindOrCreateLB && server.routed[1].first == ClientLBSGetLBEntries);
    // Descending: the highest score is rank 1.
    assert(got.entries[0].rank == 1 && got.entries[0].score == 1500);
    assert(got.entries[2].rank == 3 && got.entries[2].score == 400);

    // ---- the provider's cache file
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "xbn-leaderboards-test";
    fs::remove_all(root);
    auto path = cache_path(root, 3405340, "kills_all_time");
    assert(path && path->filename() == "kills_all_time");
    assert(path->parent_path().filename() == "leaderboard");
    assert(path->parent_path().parent_path().filename() == "3405340");
    assert(write_cache(*path, got.entries));
    auto raw = [&] {
        std::ifstream in(*path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }();
    // 3 entries: each 4 header words + its details words, little-endian.
    assert(raw.size() == (4 + 2 + 4 + 0 + 4 + 1) * 4);
    auto word = [&](size_t at) { uint32_t v; memcpy(&v, raw.data() + at * 4, 4); return v; };
    assert(word(0) == 0x00000003u && word(1) == 0x01100001u);   // low, high of the rank-1 steam id
    assert(word(2) == 1500u && word(3) == 0u);                  // score, no details
    assert(word(4) == 0x00000002u);                             // second entry's id low
    assert(word(6) == 900u && word(7) == 2u);                   // score, 2 details
    assert(word(8) == 1u && word(9) == 2u);                     // the details

    // ---- a board the game only asked to find, not create
    auto missing = fetch(io, 3405340, "never_created", SortDescending, DisplayNumeric, false);
    assert(!missing.ok && !missing.board.found && missing.board.eresult == 9);
    assert(!cache_path(root, 3405340, "never_created") || !fs::exists(*cache_path(root, 3405340, "never_created")));

    // ---- upload keeps the best score, and the response carries both ranks
    int32_t details[] = {5, 6};
    auto up = upload(io, 3405340, id, 2000, details, 2, UploadKeepBest);
    assert(up.ok && up.changed && up.entryCount == 4 && up.rankNew == 0);
    assert(server.rows[id].size() == 4);
    auto mine = std::find_if(server.rows[id].begin(), server.rows[id].end(),
                             [](const Server::Row& r) { return r.steamid == 0x110000100000001ull; });
    assert(mine != server.rows[id].end() && mine->score == 2000 && mine->details == details_of({5, 6}));
    auto worse = upload(io, 3405340, id, 100, nullptr, 0, UploadKeepBest);
    assert(worse.ok && !worse.changed);
    mine = std::find_if(server.rows[id].begin(), server.rows[id].end(),
                        [](const Server::Row& r) { return r.steamid == 0x110000100000001ull; });
    assert(mine->score == 2000);

    // ---- names that would leave the provider's leaderboard folder are refused
    assert(!cache_name("../escape"));
    assert(!cache_name("a/b"));
    assert(!cache_name(std::string(129, 'x')));
    assert(cache_name("Kills_All_Time") == "kills_all_time");
    assert(!cache_path(root, 3405340, "..\\..\\x"));

    // ---- the bridge policy the launcher runs on the game's Find
    // One round trip per board: the second seed of a known board asks Steam
    // nothing, because the provider has already cached it.
    {
        Server s2;
        s2.ensure(3405340, "v2_kills", SortDescending, true);
        int32_t id2 = s2.ids["3405340/v2_kills"];
        s2.rows[id2] = {{0x110000100000002ull, 900, {}}, {0x110000100000003ull, 1500, {}}};
        Io io2;
        io2.call = [&](uint32_t emsg, uint32_t routing, const std::string& body, std::string& resp) {
            return s2.call(emsg, routing, body, resp);
        };
        std::vector<std::string> lines;
        io2.log = [&](const std::string& line) { lines.push_back(line); };

        fs::path root2 = fs::temp_directory_path() / "xbn-leaderboards-bridge";
        fs::remove_all(root2);
        Bridge bridge;
        // The seed writes the file the provider reads on its first Find.
        assert(bridge.seed(io2, 3405340, root2, "v2_kills", SortDescending, DisplayNumeric, false));
        auto file = cache_path(root2, 3405340, "v2_kills");
        assert(file && fs::exists(*file) && fs::file_size(*file) == 2 * 4 * 4);
        assert(s2.finds == 1 && s2.gets == 1);
        // Second Find: answered from what the bridge learned, no new request.
        assert(bridge.seed(io2, 3405340, root2, "v2_kills", SortDescending, DisplayNumeric, false));
        assert(s2.finds == 1 && s2.gets == 1);
        // A board Steam does not have is asked once and then left alone; the
        // provider's own answer stands.
        assert(!bridge.seed(io2, 3405340, root2, "v2_absent", SortDescending, DisplayNumeric, false));
        assert(!bridge.seed(io2, 3405340, root2, "v2_absent", SortDescending, DisplayNumeric, false));
        assert(s2.finds == 2);
        assert(!cache_path(root2, 3405340, "v2_absent") || !fs::exists(*cache_path(root2, 3405340, "v2_absent")));

        // Upload addresses the id the seed learned; a board that was never
        // seeded has no id, so nothing is sent.
        int32_t mine[] = {9};
        assert(bridge.upload(io2, 3405340, "v2_kills", 2000, mine, 1, UploadKeepBest));
        assert(s2.sets == 1);
        assert(!bridge.upload(io2, 3405340, "v2_absent", 2000, mine, 1, UploadKeepBest));
        assert(!bridge.upload(io2, 3405340, "v2_never_asked", 2000, mine, 1, UploadKeepBest));
        assert(s2.sets == 1);
        // The log names the board and the file, never a Steam id.
        bool seeded_line = false;
        for (auto& l : lines) if (l.find("'v2_kills' seeded 2 rows") != std::string::npos && l.find("file written") != std::string::npos) seeded_line = true;
        assert(seeded_line);
        for (auto& l : lines) assert(l.find("7656119") == std::string::npos);
        fs::remove_all(root2);
    }

    // ---- a board Steam has, whose rows do not arrive
    // The board is still addressable: the id from FindOrCreateLB is kept, so a
    // score for it is uploaded rather than silently dropped for the launch.
    {
        Server s3;
        s3.ensure(3405340, "v2_rows_lost", SortDescending, true);
        int32_t id3 = s3.ids["3405340/v2_rows_lost"];
        Io io3;
        io3.call = [&](uint32_t emsg, uint32_t routing, const std::string& body, std::string& resp) {
            // The rows request times out; the find answers normally.
            if (emsg == ClientLBSGetLBEntries) return false;
            return s3.call(emsg, routing, body, resp);
        };
        std::vector<std::string> lines;
        io3.log = [&](const std::string& line) { lines.push_back(line); };
        fs::path root3 = fs::temp_directory_path() / "xbn-leaderboards-rows-lost";
        fs::remove_all(root3);
        Bridge bridge;
        assert(!bridge.seed(io3, 3405340, root3, "v2_rows_lost", SortDescending, DisplayNumeric, false));
        assert(!cache_path(root3, 3405340, "v2_rows_lost") || !fs::exists(*cache_path(root3, 3405340, "v2_rows_lost")));
        bool told = false;
        for (auto& l : lines) if (l.find("'v2_rows_lost' seeded 0 rows (id " + std::to_string(id3)) != std::string::npos) told = true;
        assert(told);
        int32_t mine3[] = {3};
        assert(bridge.upload(io3, 3405340, "v2_rows_lost", 2500, mine3, 1, UploadKeepBest));
        assert(s3.sets == 1);
        // The second seed does not ask again: the board is already known.
        assert(!bridge.seed(io3, 3405340, root3, "v2_rows_lost", SortDescending, DisplayNumeric, false));
        assert(s3.finds == 1);
        fs::remove_all(root3);
    }

    // ---- refresh: a later Find picks up other players' new scores
    // The provider caches a board in memory at its first Find and never re-reads
    // the file, so the launcher keeps its own rows and hands them to the game
    // from GetDownloadedLeaderboardEntry. Those rows must follow Steam.
    {
        Server s4;
        s4.ensure(3405340, "v2_kills_weekly", SortDescending, true);
        int32_t id4 = s4.ids["3405340/v2_kills_weekly"];
        s4.rows[id4] = {{0x110000100000002ull, 900, {}}, {0x110000100000003ull, 1500, {}}};
        Io io4;
        io4.call = [&](uint32_t emsg, uint32_t routing, const std::string& body, std::string& resp) {
            return s4.call(emsg, routing, body, resp);
        };
        std::vector<std::string> lines;
        io4.log = [&](const std::string& line) { lines.push_back(line); };
        fs::path root4 = fs::temp_directory_path() / "xbn-leaderboards-refresh";
        fs::remove_all(root4);
        Bridge bridge;
        // First Find: seeded, and the rows are the ones Steam sent.
        assert(bridge.seed(io4, 3405340, root4, "v2_kills_weekly", SortDescending, DisplayNumeric, false, 100));
        assert(s4.finds == 1 && s4.gets == 1);
        auto rows = bridge.entries("v2_kills_weekly");
        assert(rows && rows->size() == 2 && (*rows)[0].score == 1500);
        // Inside the TTL, a Find answers from the rows already held.
        assert(bridge.seed(io4, 3405340, root4, "v2_kills_weekly", SortDescending, DisplayNumeric, false, 110));
        assert(s4.gets == 1);
        // Another player's score moved. Past the TTL the Find refetches, and the
        // rows the game will read are the new ones.
        s4.rows[id4][0].score = 2100;
        assert(bridge.seed(io4, 3405340, root4, "v2_kills_weekly", SortDescending, DisplayNumeric, false, 100 + 31));
        assert(s4.gets == 2);
        rows = bridge.entries("v2_kills_weekly");
        assert(rows && rows->size() == 2 && (*rows)[0].score == 2100);
        // This account's own upload dirties the board: the next Find refetches
        // at once, inside the TTL, so the owner sees their new rank.
        int32_t mine4[] = {1};
        assert(bridge.upload(io4, 3405340, "v2_kills_weekly", 3000, mine4, 1, UploadKeepBest));
        assert(bridge.seed(io4, 3405340, root4, "v2_kills_weekly", SortDescending, DisplayNumeric, false, 100 + 32));
        assert(s4.gets == 3);
        rows = bridge.entries("v2_kills_weekly");
        assert(rows && rows->size() == 3);
        bool mineFirst = false;
        for (auto& e : *rows) if (e.steamid == 0x110000100000001ull && e.score == 3000) mineFirst = true;
        assert(mineFirst);
        bool refreshed = false;
        for (auto& l : lines) if (l.find("'v2_kills_weekly' refreshed 3 rows") != std::string::npos) refreshed = true;
        assert(refreshed);
        // A failed refresh keeps what the last successful one left: the game is
        // never shown less than it already had.
        Io broken;
        broken.call = [&](uint32_t emsg, uint32_t routing, const std::string& body, std::string& resp) {
            if (emsg == ClientLBSGetLBEntries) return false;
            return s4.call(emsg, routing, body, resp);
        };
        broken.log = [&](const std::string& line) { lines.push_back(line); };
        assert(bridge.seed(broken, 3405340, root4, "v2_kills_weekly", SortDescending, DisplayNumeric, false, 100 + 200));
        rows = bridge.entries("v2_kills_weekly");
        assert(rows && rows->size() == 3);
        bool kept = false;
        for (auto& l : lines) if (l.find("refresh unavailable") != std::string::npos && l.find("keeping 3 rows") != std::string::npos) kept = true;
        assert(kept);
        // No SteamID or name reaches the log.
        for (auto& l : lines) assert(l.find("7656119") == std::string::npos);
        fs::remove_all(root4);
    }

    // ---- transport failure is reported, never thrown
    Io dead;
    dead.call = [](uint32_t, uint32_t, const std::string&, std::string&) { return false; };
    dead.log = [&](const std::string& line) { log.push_back(line); };
    auto none = fetch(dead, 3405340, "kills_all_time", SortDescending, DisplayNumeric, false);
    assert(!none.ok && none.entries.empty());

    fs::remove_all(root);
    puts("STEAM-LEADERBOARDS PASS: LBS wire shapes, provider cache file, bridge seed policy, keep-best upload, name safety, transport failure");
    return 0;
}
