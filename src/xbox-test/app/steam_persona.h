// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Steam persona names for owned games served by the Steam API provider
// (gbe_fork; the ownership rule). gbe_fork answers ISteamFriends::GetFriendPersonaName
// from its own friend list, which only ever holds peers on its LAN network
// (dll/steam_friends.cpp: `find_friend` -> "Unknown User"). With
// disable_networking=1 that list is empty, so every leaderboard row the game
// draws from a real Steam board reads "Unknown User".
//
// The launcher asks the real Steam backend over its own signed-in CM session:
// CMsgClientRequestFriendData (EMSG 815) for the SteamIDs the leaderboard rows
// named, and CMsgClientPersonaState (EMSG 766) comes back as an unsolicited push
// carrying player_name (field 15) and persona_state (field 2) per user. Names
// are handed to the game in memory by the export tap in play_runtime.h; nothing
// on disk changes and the provider still answers every call it answered before.
//
// Portable: the CM transport is injected, so the wire shapes, the cache and the
// wait policy are tested natively. No SteamID or persona name is ever logged:
// the log lines carry counts only.
// Message shapes follow SteamDatabase/Protobufs (public domain) as vendored in
// SteamKit2 / steam-user; see issue #180.
#include "steam_wire.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace steam_persona {
namespace fs = std::filesystem;
// EMsg values from enums_clientserver.proto.
enum : uint32_t {
    ClientRequestFriendData = 815,   // CMsgClientRequestFriendData
    ClientPersonaState = 766,        // CMsgClientPersonaState (unsolicited push)
};
// EClientPersonaStateFlag: what to ask for. Only the name and the online state
// are needed to draw a rank row; asking for less keeps the reply small.
enum : uint32_t { FlagStatus = 1, FlagPlayerName = 2 };
// Steam answers a batch; keep one request well inside a single CM frame.
enum : unsigned { RequestBatch = 100 };
// How long the seed waits after a reply before it lets the game draw: Steam
// sends one push per request, and an account it will not name (a private
// profile) never arrives, so waiting for "all" would always burn the timeout.
// The quiet slice only starts once a push has arrived: a first reply slower
// than this must still be waited for, up to the caller's timeout.
enum : int { QuietMs = 500 };
// Most SteamIDs one launch will track. A rank screen that polls unresolved
// names must not grow the request set without bound. Distinct ids only: the
// sets below dedupe, so a repeat of an id already asked for costs nothing.
enum : size_t { MaxTracked = 2000 };

struct Io {
    // Fire and forget: CMsgClientRequestFriendData has no job id, and the answer
    // arrives as a push on the same session (Bridge::on_state).
    std::function<bool(uint32_t emsg, const std::string& body)> send;
    std::function<void(const std::string&)> log;
};

// ---- wire
// CMsgClientRequestFriendData: persona_state_requested (1), friends (2, repeated
// fixed64).
inline std::string request_body(const std::vector<uint64_t>& ids, uint32_t flags = FlagStatus | FlagPlayerName) {
    std::string b;
    pb_u(b, 1, flags);
    for (auto id : ids) pb_fixed64(b, 2, id);
    return b;
}
struct Persona {
    uint64_t steamid = 0;
    std::string name;
    uint32_t state = 0;
    bool has_state = false;
};
// CMsgClientPersonaState: status_flags (1), friends (2) -> friendid (1),
// persona_state (2), player_name (15), avatar_hash (31). One message may carry
// any number of users, and Steam sends it unprompted when a name changes.
inline std::vector<Persona> parse_state(const std::string& body) {
    std::vector<Persona> out;
    for (auto& f : pb_parse(body)) {
        if (f.f != 2) continue;
        Persona p;
        for (auto& x : pb_parse(f.s)) {
            if (x.f == 1) p.steamid = x.u;
            else if (x.f == 2) { p.state = uint32_t(x.u); p.has_state = true; }
            else if (x.f == 15) p.name = x.s;
        }
        if (p.steamid) out.push_back(std::move(p));
    }
    return out;
}

// ---- the private on-device cache
// Persona names change rarely and a launch usually asks for the same board, so
// the names from the last launch are kept in the app's own LocalState (the same
// place the provider keeps its leaderboard cache). The file is private on-device
// state: it is never logged, never committed, and expires by TTL. Names are
// hex-encoded so a persona name containing a space or a newline cannot corrupt
// the record layout.
inline std::string hex_encode(const std::string& s) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}
inline bool hex_decode(const std::string& h, std::string& out) {
    if (h.size() % 2) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.clear();
    for (size_t i = 0; i < h.size(); i += 2) {
        int hi = nib(h[i]), lo = nib(h[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out += char((hi << 4) | lo);
    }
    return true;
}
struct CacheRecord { uint64_t stamp = 0; std::map<uint64_t, std::string> names; };
// A cache older than `ttl` seconds is ignored: a name change must not survive
// forever on a console that has not been online since.
inline bool load_cache(const fs::path& path, uint64_t now, uint64_t ttl, std::map<uint64_t, std::string>& out) {
    out.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string line;
    if (!std::getline(in, line)) return false;
    // "#xbn-persona 1 <unix seconds>"
    const std::string head = "#xbn-persona 1 ";
    if (line.compare(0, head.size(), head) != 0) return false;
    uint64_t stamp = 0;
    try { stamp = std::stoull(line.substr(head.size())); } catch (...) { return false; }
    if (stamp > now || now - stamp > ttl) return false;
    while (std::getline(in, line)) {
        auto sep = line.find(' ');
        if (sep == std::string::npos) continue;
        uint64_t id = 0;
        try { id = std::stoull(line.substr(0, sep)); } catch (...) { continue; }
        std::string name;
        if (!id || !hex_decode(line.substr(sep + 1), name) || name.empty()) continue;
        out[id] = name;
    }
    return !out.empty();
}
inline bool save_cache(const fs::path& path, uint64_t now, const std::map<uint64_t, std::string>& names) {
    if (names.empty()) return false;
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    auto tmp = path;
    tmp += ".xboxnative-part";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "#xbn-persona 1 " << now << "\n";
        for (auto& [id, name] : names) out << id << ' ' << hex_encode(name) << "\n";
        if (!out) return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
}

// ---- the bridge policy
// One Bridge per launch. The game's thread asks for the names it is about to
// draw (the leaderboard seed hands over the rows it just fetched); the CM reader
// thread delivers the reply. A name is only ever handed to the game once it is
// in `names`, so GetFriendPersonaName never sees a half-filled answer.
struct Bridge {
    mutable std::mutex m;
    std::condition_variable cv;
    // Name storage the game may hold a pointer into: a deque never moves its
    // elements, so a pointer handed out stays valid for the launch.
    std::deque<std::string> arena;
    std::map<uint64_t, const std::string*> names;
    std::map<uint64_t, uint32_t> states;
    std::set<uint64_t> asked;     // requested this launch (answered or not)
    std::set<uint64_t> pending;   // wanted by the game, not yet requested
    uint64_t self = 0;            // the signed-in account, for GetPersonaName
    // The ids the seed is waiting on. Only a push that mentions one of them is
    // this request's answer: Steam also pushes persona state for the account
    // itself, for friends and for anything else the game looks at, and one of
    // those must not end the wait before the names arrive. A push that mentions
    // a requested id but carries no name (a private profile) is still the
    // answer, so the wait counts replies, not names.
    std::set<uint64_t> awaiting;
    uint64_t replies = 0;
    // The seed runs on the game's thread, which is drawing behind it, so the
    // wait is bounded hard: a Find that has to wait for Steam costs a fraction
    // of a second, not the two seconds the caller asks for. The names that
    // arrive after that are still handed out by the tap (the game re-reads
    // them on its next draw) and still cached for the next launch.
    enum : int { SeedWaitMs = 250 };

    bool known(uint64_t id) const { return names.find(id) != names.end(); }
    // A persona push arrived. Returns the number of names it added or changed.
    // A rename appends the new name and repoints the map: a pointer already
    // handed to the game stays readable (the deque never moves its elements).
    size_t on_state(const std::string& body) {
        size_t added = 0;
        std::lock_guard<std::mutex> lock(m);
        bool ours = false;
        for (auto& p : parse_state(body)) {
            if (awaiting.count(p.steamid)) ours = true;
            if (!p.name.empty()) {
                auto it = names.find(p.steamid);
                if (it == names.end()) {
                    arena.push_back(p.name);
                    names[p.steamid] = &arena.back();
                    ++added;
                } else if (*it->second != p.name) {
                    arena.push_back(p.name);
                    it->second = &arena.back();
                    ++added;
                }
            }
            if (p.has_state) states[p.steamid] = p.state;
        }
        if (ours) ++replies;
        cv.notify_all();
        return added;
    }
    // Ask Steam for the names of `ids` that are not known yet, and wait a
    // bounded time for the reply. Called on the game's thread from the seed, so
    // the wait must end as soon as Steam has said what it is going to say: the
    // push arrives as one entry per account, and accounts Steam will not name
    // (private profiles) never arrive at all. So it returns once every id is
    // known, or once the names have stopped arriving for `QuietMs`, or at
    // `timeoutMs` — whichever comes first. A missing reply leaves the
    // provider's own answer exactly as it was.
    void ensure(Io& io, const std::vector<uint64_t>& ids, int timeoutMs) {
        // Never hold the game's thread for the caller's full timeout: that
        // timeout is an upper bound, and the game is drawing behind this call.
        timeoutMs = std::min(timeoutMs, int(SeedWaitMs));
        std::vector<uint64_t> missing;
        std::set<uint64_t> seen;
        uint64_t repliesBefore = 0;
        {
            std::lock_guard<std::mutex> lock(m);
            // Taken before the send: a push that lands between the send and the
            // wait below is Steam's answer, not one that was already there.
            repliesBefore = replies;
            for (auto id : ids)
                if (id && !known(id) && !asked.count(id) && seen.insert(id).second) missing.push_back(id);
            // Registered before the send so a push that beats the wait still
            // counts. Only the ids actually asked for: a push about a row that
            // was already known is not this request's answer.
            awaiting.insert(missing.begin(), missing.end());
        }
        if (missing.empty()) return;
        bool sent = true;
        for (size_t i = 0; i < missing.size() && sent; i += RequestBatch) {
            auto end = std::min(missing.size(), i + size_t(RequestBatch));
            std::vector<uint64_t> batch(missing.begin() + i, missing.begin() + end);
            sent = io.send(ClientRequestFriendData, request_body(batch));
        }
        if (!sent) {
            std::lock_guard<std::mutex> lock(m);
            for (auto id : missing) awaiting.erase(id);
            io.log("persona: name request not sent (transport)");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m);
            for (auto id : missing) asked.insert(id);
        }
        auto known_count = [&] {
            size_t got = 0;
            for (auto id : seen)
                if (known(id)) ++got;
            return got;
        };
        std::unique_lock<std::mutex> lock(m);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        size_t want = seen.size();
        size_t got = known_count(), last = got;
        uint64_t repliesSeen = repliesBefore;
        bool replied = false;
        while (got < want) {
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) break;
            if (!replied) {
                // No answer yet: wait for Steam's first push that mentions one
                // of these ids for the whole timeout. A reply slower than
                // QuietMs is still a reply, and returning before it lands is
                // how a rank screen ends up with "Unknown User" on rows Steam
                // was about to name. A push about anything else (the account
                // itself, a friend, another board) is not this answer and does
                // not end the wait.
                cv.wait_for(lock, std::chrono::milliseconds(left), [&] { return replies != repliesSeen; });
                if (replies == repliesSeen) break;   // nothing came: the provider's answers stand
                replied = true;
                repliesSeen = replies;
                got = known_count();
                last = got;
                continue;
            }
            auto slice = std::chrono::milliseconds(std::min<long long>(QuietMs, left));
            cv.wait_for(lock, slice, [&] { return replies != repliesSeen || known_count() != got; });
            got = known_count();
            // Steam answers one push per request, and the accounts it will not
            // name (private profiles) simply do not appear in it. A quiet slice
            // after a push therefore means it has said all it is going to say.
            if (replies == repliesSeen && got == last) break;
            repliesSeen = replies;
            last = got;
        }
        for (auto id : missing) awaiting.erase(id);
        lock.unlock();
        // Counts only: a SteamID or a persona name must never reach the log.
        io.log("persona: " + std::to_string(got) + " of " + std::to_string(want) + " leaderboard names resolved");
    }
    // Names the game asked for outside the seeded rows: queued for the keeper,
    // which requests them on its next pass (never on the game's thread). Bounded
    // by distinct ids: a rank screen polling the same unresolved rows adds
    // nothing, and a game that walks thousands of users cannot grow this.
    void want(const std::vector<uint64_t>& ids) {
        std::lock_guard<std::mutex> lock(m);
        for (auto id : ids) {
            if (!id || known(id) || asked.count(id) || pending.count(id)) continue;
            if (asked.size() + pending.size() >= MaxTracked) return;
            pending.insert(id);
        }
    }
    std::vector<uint64_t> take_pending() {
        std::lock_guard<std::mutex> lock(m);
        std::vector<uint64_t> out(pending.begin(), pending.end());
        for (auto id : out) asked.insert(id);
        pending.clear();
        return out;
    }
    // The name for a SteamID, or nullptr when Steam has not answered for it.
    // The pointer stays valid for the launch.
    const char* name(uint64_t id) const {
        std::lock_guard<std::mutex> lock(m);
        auto it = names.find(id);
        return it == names.end() ? nullptr : it->second->c_str();
    }
    bool state(uint64_t id, uint32_t& out) const {
        std::lock_guard<std::mutex> lock(m);
        auto it = states.find(id);
        if (it == states.end()) return false;
        out = it->second;
        return true;
    }
    const char* self_name() const { return self ? name(self) : nullptr; }
    // Names from the last launch, so a row drawn before Steam answers still
    // reads correctly. A name Steam already answered this launch wins.
    void adopt(const std::map<uint64_t, std::string>& cached) {
        std::lock_guard<std::mutex> lock(m);
        for (auto& [id, name] : cached) {
            if (name.empty()) continue;
            auto it = names.find(id);
            if (it == names.end()) {
                arena.push_back(name);
                names[id] = &arena.back();
            } else if (*it->second != name) {
                arena.push_back(name);
                it->second = &arena.back();
            }
        }
    }
    std::map<uint64_t, std::string> snapshot() const {
        std::lock_guard<std::mutex> lock(m);
        std::map<uint64_t, std::string> out;
        for (auto& [id, p] : names) out[id] = *p;
        return out;
    }
    void reset() {
        std::lock_guard<std::mutex> lock(m);
        arena.clear(); names.clear(); states.clear(); asked.clear(); pending.clear();
        awaiting.clear(); self = 0; replies = 0;
    }
};
} // namespace steam_persona
