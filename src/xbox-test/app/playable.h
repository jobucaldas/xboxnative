// SPDX-License-Identifier: GPL-3.0-or-later
// The playable set (issue 51 item 32): games the owner verified working on the
// console (see docs/PLAYABLE.md). The library's 'Playable' filter shows only
// these. Extra appids come from LocalState\playable.txt when present: one
// appid per line, '#' starts a comment, junk lines are ignored — so the owner
// can extend the set on the console without a new package.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace playable {

// Owner-verified on the console (docs/PLAYABLE.md): Megabonk, Puyo Puyo Tetris 2.
constexpr uint32_t kBuiltIn[] = {3405340, 1259790};
constexpr int kBuiltInCount = 2;

inline bool is_builtin(uint32_t appid) {
    for (int i = 0; i < kBuiltInCount; ++i) if (kBuiltIn[i] == appid) return true;
    return false;
}

// One appid per line; '#' anywhere starts a comment (whole-line or trailing);
// blank lines and junk are skipped. Returns the parsed ids in file order
// (duplicates possible; the caller dedups or not as it prefers).
inline std::vector<uint32_t> parse_txt(const std::string& text) {
    std::vector<uint32_t> out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        // trim
        size_t a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r");
        line = line.substr(a, b - a + 1);
        // digits only
        bool ok = !line.empty();
        for (char c : line) if (c < '0' || c > '9') { ok = false; break; }
        if (!ok) continue;
        // unsigned parse without UB on overflow (uint64 accumulator, cap sane)
        uint64_t v = 0;
        for (char c : line) { v = v * 10 + (uint64_t)(c - '0'); if (v > 0xFFFFFFFFull) { v = 0; break; } }
        if (v) out.push_back((uint32_t)v);
    }
    return out;
}

}  // namespace playable
