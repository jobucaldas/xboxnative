// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Steam persona names for owned games served by the Steam API provider
// (gbe_fork; AGENTS.md rule 3). gbe_fork answers ISteamFriends::GetFriendPersonaName
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
// SteamKit2 / steam-user; see research/STEAMWORKS-MULTIPLAYER.md.
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

    bool known(uint64_t id) const { return names.find(id) != names.end(); }
    // A persona push arrived. Returns the number of names it added or changed.
    // A rename appends the new name and repoints the map: a pointer already
    // handed to the game stays readable (the deque never moves its elements).
    size_t on_state(const std::string& body) {
        size_t added = 0;
        std::lock_guard<std::mutex> lock(m);
        for (auto& p : parse_state(body)) {
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
        if (added) cv.notify_all();
        return added;
    }
    // Ask Steam for the names of `ids` that are not known yet, and wait up to
    // `timeoutMs` for the reply. Called on the game's thread from the seed, so
    // the wait is bounded and a missing reply leaves the provider's own answer
    // exactly as it was.
    void ensure(Io& io, const std::vector<uint64_t>& ids, int timeoutMs) {
        std::vector<uint64_t> missing;
        {
            std::lock_guard<std::mutex> lock(m);
            for (auto id : ids)
                if (id && !known(id) && !asked.count(id)) missing.push_back(id);
        }
        if (missing.empty()) return;
        bool sent = true;
        for (size_t i = 0; i < missing.size() && sent; i += RequestBatch) {
            auto end = std::min(missing.size(), i + size_t(RequestBatch));
            std::vector<uint64_t> batch(missing.begin() + i, missing.begin() + end);
            sent = io.send(ClientRequestFriendData, request_body(batch));
        }
        if (!sent) { io.log("persona: name request not sent (transport)"); return; }
        {
            std::lock_guard<std::mutex> lock(m);
            for (auto id : missing) asked.insert(id);
        }
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
            for (auto id : ids)
                if (id && !known(id)) return false;
            return true;
        });
        size_t got = 0;
        for (auto id : ids)
            if (id && known(id)) ++got;
        lock.unlock();
        // Counts only: a SteamID or a persona name must never reach the log.
        io.log("persona: " + std::to_string(got) + " of " + std::to_string(ids.size()) + " leaderboard names resolved");
    }
    // Names the game asked for outside the seeded rows: queued for the keeper,
    // which requests them on its next pass (never on the game's thread).
    void want(const std::vector<uint64_t>& ids) {
        std::lock_guard<std::mutex> lock(m);
        for (auto id : ids)
            if (id && !known(id) && !asked.count(id)) pending.insert(id);
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
        arena.clear(); names.clear(); states.clear(); asked.clear(); pending.clear(); self = 0;
    }
};
} // namespace steam_persona
