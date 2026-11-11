// SPDX-License-Identifier: GPL-3.0-or-later
// Item D23: "Date added to the library" sort. Steam's owned-games answer
// carries no acquisition date, so the launcher records the first time it saw
// each appid in the account's library (LocalState\library_added.txt, one
// "appid<TAB>unix-seconds" line per game) and sorts newest first. A game the
// account already owned when this file was first written shares that first
// stamp, which is honest: the launcher cannot know better, and the order is
// stable from then on. Pure and Win32-free, so tests/game_library_test.cpp
// covers the round trip.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace library_added {

using Seen = std::map<uint32_t, uint64_t>;   // appid -> first seen (unix seconds)

inline Seen parse(const std::string& text) {
    Seen out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        size_t a = 0, b = line.size();
        while (a < b && (line[a] == ' ' || line[a] == '\t' || line[a] == '\r')) ++a;
        while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r')) --b;
        if (a == b) continue;
        const size_t tab = line.find('\t', a);
        if (tab == std::string::npos || tab >= b) continue;
        std::string id = line.substr(a, tab - a), when = line.substr(tab + 1, b - tab - 1);
        auto trim = [](std::string& s) {
            size_t p = 0, q = s.size();
            while (p < q && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r')) ++p;
            while (q > p && (s[q - 1] == ' ' || s[q - 1] == '\t' || s[q - 1] == '\r')) --q;
            s = s.substr(p, q - p);
        };
        trim(id); trim(when);
        char* end = nullptr;
        const unsigned long long appid = std::strtoull(id.c_str(), &end, 10);
        if (!end || *end || appid == 0 || appid > 0xFFFFFFFFull) continue;
        end = nullptr;
        const unsigned long long stamp = std::strtoull(when.c_str(), &end, 10);
        if (!end || *end) continue;
        out[(uint32_t)appid] = (uint64_t)stamp;
    }
    return out;
}

inline std::string format(const Seen& seen) {
    std::string out;
    for (const auto& [appid, when] : seen) out += std::to_string(appid) + "\t" + std::to_string(when) + "\n";
    return out;
}

// Stamps every id not yet recorded with `now` (seconds). Returns true when the
// set changed, i.e. the caller must write the file back. Ids already recorded
// keep their first stamp, so the order never reshuffles on a later fetch.
inline bool update(Seen& seen, const std::vector<uint32_t>& ids, uint64_t now) {
    bool changed = false;
    for (uint32_t id : ids)
        if (id && seen.find(id) == seen.end()) { seen[id] = now; changed = true; }
    return changed;
}

// Newest first: the most recently added game leads. Ties (a first fetch stamps
// the whole library at once) fall back to the caller's stable sort, so the
// alphabetical order of the previous mode survives inside a tie.
inline bool newer_first(const Seen& seen, uint32_t a, uint32_t b) {
    const auto ia = seen.find(a), ib = seen.find(b);
    const uint64_t ta = ia == seen.end() ? 0 : ia->second, tb = ib == seen.end() ? 0 : ib->second;
    return ta > tb;
}

}  // namespace library_added
