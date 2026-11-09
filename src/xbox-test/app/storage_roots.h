// SPDX-License-Identifier: GPL-3.0-or-later
// Install-location choices for the detail page: portable rules (no Win32) so
// tests/game_library_test.cpp covers them natively. installs.h fills the list.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "game_library.h"

namespace storage_roots {

// root "" = internal (LocalState); else a game_library::valid_root path on a USB/extra drive.
struct Choice { std::string root; std::string name; uint64_t freeBytes = 0; };

inline std::string size_text(uint64_t b) {
    char t[32]; if (b >= (1ull << 30)) std::snprintf(t, sizeof t, "%.1f GB", b / 1073741824.0); else std::snprintf(t, sizeof t, "%.0f MB", b / 1048576.0);
    return t;
}
// "Internal (4.8 GB free)" / "USB E: (120.0 GB free)"
inline std::string label(const Choice& c) { return c.name + " (" + size_text(c.freeBytes) + " free)"; }
inline std::string drive_name(const std::string& root) { return root.size() >= 2 ? std::string("USB ") + (char)std::toupper((unsigned char)root[0]) + ":" : "USB"; }

// Index of the saved choice in the current list; the first entry (internal) when it is gone.
inline size_t index_of(const std::vector<Choice>& v, const std::string& root) {
    for (size_t i = 0; i < v.size(); i++) if (v[i].root == root || (!root.empty() && game_library::canonical_dir(v[i].root) == game_library::canonical_dir(root))) return i;
    return 0;
}
inline size_t cycle(size_t cur, size_t n, int step) { return n == 0 ? 0 : (cur + (step < 0 ? n - 1 : 1)) % n; }

// Enough room for `need` bytes plus 5% and 64 MB slack (the downloader's rule).
inline bool has_room(uint64_t freeBytes, uint64_t need) { return freeBytes >= need + need / 20 + (64ull << 20); }

}  // namespace storage_roots
