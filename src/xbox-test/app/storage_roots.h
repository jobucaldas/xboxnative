// SPDX-License-Identifier: GPL-3.0-or-later
// Install-location choices for the detail page: portable rules (no Win32) so
// tests/game_library_test.cpp covers them natively. installs.h fills the list.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "game_library.h"

namespace storage_roots {

// Every free-space number the launcher can read for one path. installs.h fills
// it from GetDiskFreeSpaceExW; main.cpp adds WinRT's view of LocalFolder.
// available is what this app may still write (lpFreeBytesAvailableToCaller):
// with a per-user quota or a separate pool it is below the volume's own free
// space, and a write past it fails even when the volume has room.
struct Space {
    bool ok = false;            // GetDiskFreeSpaceExW succeeded
    uint64_t available = 0;     // lpFreeBytesAvailableToCaller
    uint64_t total = 0;         // lpTotalNumberOfBytes (the pool this app writes into)
    uint64_t freeTotal = 0;     // lpTotalNumberOfFreeBytes (the whole volume)
    bool winrt = false;         // System.FreeSpace / System.Capacity on the folder
    uint64_t winrtFree = 0, winrtCapacity = 0;
};

// root "" = internal (LocalState); else a game_library::valid_root path on a USB/extra drive.
// freeBytes is the figure an install is checked against (space.available); type is GetDriveTypeW.
struct Choice { std::string root; std::string name; uint64_t freeBytes = 0; Space space; uint32_t type = 0; };

inline std::string size_text(uint64_t b) {
    char t[32]; if (b >= (1ull << 30)) std::snprintf(t, sizeof t, "%.1f GB", b / 1073741824.0); else std::snprintf(t, sizeof t, "%.0f MB", b / 1048576.0);
    return t;
}
// "Internal (4.8 GB free)" / "USB E: (120.0 GB free)"
inline std::string label(const Choice& c) { return c.name + " (" + size_text(c.freeBytes) + " free)"; }
// Same, with the size of the pool when it is known: "Internal (4.8 GB free of 5.0 GB)".
// A Dev Mode console keeps app data in a partition of its own (5 GB by default,
// resized in Dev Home), so the number the player sees in the console's storage
// settings is not the number this app can use. The volume's own figure is left to
// the refusal line and the log: the button has one line to say where an install goes.
inline std::string label_capacity(const Choice& c) {
    if (!c.space.ok || !c.space.total) return label(c);
    return c.name + " (" + size_text(c.freeBytes) + " free of " + size_text(c.space.total) + ")";
}

// GetDriveTypeW results (winbase.h), repeated so this header stays Win32-free.
enum : uint32_t { kUnknown = 0, kNoRootDir = 1, kRemovable = 2, kFixed = 3, kRemote = 4, kCdrom = 5, kRamdisk = 6 };
inline const char* drive_type_name(uint32_t t) {
    switch (t) { case kRemovable: return "removable"; case kFixed: return "fixed"; case kRemote: return "network";
                 case kCdrom: return "cd-rom"; case kRamdisk: return "ramdisk"; default: return "unknown"; }
}
// "USB E:" only for a removable drive. A fixed drive is "Drive D:": on a Dev Mode
// console D:\ is the developer scratch partition, not the USB stick, and calling
// it USB would send the player looking for a drive that is not there.
inline std::string drive_name(const std::string& root, uint32_t type) {
    if (root.size() < 2) return "Drive";
    return std::string(type == kRemovable ? "USB " : "Drive ") + (char)std::toupper((unsigned char)root[0]) + ":";
}

// Index of the saved choice in the current list; the first entry (internal) when it is gone.
inline size_t index_of(const std::vector<Choice>& v, const std::string& root) {
    for (size_t i = 0; i < v.size(); i++) if (v[i].root == root || (!root.empty() && game_library::canonical_dir(v[i].root) == game_library::canonical_dir(root))) return i;
    return 0;
}
inline size_t cycle(size_t cur, size_t n, int step) { return n == 0 ? 0 : (cur + (step < 0 ? n - 1 : 1)) % n; }

// Enough room for `need` bytes plus 5% and 64 MB slack (the downloader's rule).
inline bool has_room(uint64_t freeBytes, uint64_t need) { return freeBytes >= need + need / 20 + (64ull << 20); }

// The other install location with the most room (v.size() when there is none).
// Iterates with iterators: gcc 16's array-bounds pass reports a false positive on
// the indexed form once this is inlined for a one-element vector.
inline size_t best_other(const std::vector<Choice>& v, size_t cur) {
    size_t best = v.size(), bestFree = 0, i = 0;
    for (auto it = v.begin(); it != v.end(); ++it, ++i) {
        if (i == cur) continue;
        if (best == v.size() || it->freeBytes > bestFree) { best = i; bestFree = it->freeBytes; }
    }
    return best;
}

// The sidebar disk widget's figures for ONE install location, computed exactly
// like the Storage screen's drive row (free_total / total), so the widget and
// the screen can never disagree. free_gb_x10 is the volume's free space in
// tenths of a GB (the widget's "412.6 GB free"); used_pct is what the ring
// fills. Returns false when the drive's space could not be read.
inline bool widget_figures(const Choice& c, int& free_gb_x10, int& used_pct) {
    if (!c.space.ok || !c.space.total) return false;
    free_gb_x10 = int((double(c.space.freeTotal) * 10.0 / 1073741824.0) + 0.5);
    used_pct = int(100.0 - 100.0 * double(c.space.freeTotal) / double(c.space.total));
    if (used_pct < 0) used_pct = 0;
    if (used_pct > 100) used_pct = 100;
    return true;
}

// Detail-page storage line: what this app may still write, the size of the
// space it is writing into, and the volume's own free space when that differs
// (a quota or a separate pool), so "0 MB free" is not read as "disk full".
inline std::string space_text(const Space& s) {
    if (!s.ok) return "free space could not be read";
    std::string t = size_text(s.available) + " for this app";
    if (s.total) t += " of " + size_text(s.total);
    if (s.freeTotal > s.available + s.available / 20) t += ", " + size_text(s.freeTotal) + " volume free";
    return t;
}
// One "storage:" log line per free-space query: the path, the API and every
// figure it returns, so the next console log says whether a small number is a
// per-user quota, a separate pool, or a genuinely full volume. extra carries
// what only Win32 knows (drive type, volume label, filesystem).
inline std::string space_line(const std::string& name, const std::string& path, const Space& s, const std::string& extra = std::string()) {
    std::string t = name + " path=" + path + " GetDiskFreeSpaceEx ";
    if (!s.ok) t += "FAILED";
    else t += "available=" + size_text(s.available) + " total=" + size_text(s.total) + " free_total=" + size_text(s.freeTotal);
    if (s.winrt) t += " WinRT System.FreeSpace=" + size_text(s.winrtFree) + " System.Capacity=" + size_text(s.winrtCapacity);
    if (!extra.empty()) t += " " + extra;
    return t;
}

// Split a detail-page note into the two lines it is drawn on: an explicit '\n'
// first, then a word wrap at `n` characters (the page's 2x text, 42 to the
// right column). Overflow is cut with U+2026 (item D4: Selawik has the glyph,
// and a bare "." reads as part of the name) rather than drawn past the column.
inline void note_lines(const std::string& s, size_t n, std::string& l1, std::string& l2) {
    l1.clear(); l2.clear();
    size_t br = s.find('\n');
    std::string first = s.substr(0, br), rest = br == std::string::npos ? std::string() : s.substr(br + 1);
    auto wrap = [&](const std::string& t, std::string& out) {
        if (t.size() <= n) { out = t; return; }
        size_t cut = t.rfind(' ', n); if (cut == std::string::npos || cut < n / 2) cut = n;
        out = t.substr(0, cut);
    };
    wrap(first, l1);
    if (l1.size() < first.size() && rest.empty()) {   // no explicit break: the tail is line two
        size_t st = l1.size(); while (st < first.size() && first[st] == ' ') st++;
        rest = first.substr(st);
    }
    wrap(rest, l2);
    if (l2.size() < rest.size()) {
        size_t cut = n > 3 ? n - 3 : 0;
        while (cut > 0 && ((unsigned char)l2[cut] & 0xC0) == 0x80) --cut;   // whole glyphs only
        l2 = l2.substr(0, cut) + "\xE2\x80\xA6";
    }
}

// Detail-page message when an install is refused for space: the figure with the
// size of the pool it is a share of ("0 MB of 5.0 GB", not a bare "0 MB free"),
// then the way out (another drive, or a game to delete). Two lines, each under
// 42 characters at the page's 2x text (the right column starts at x=740).
inline std::string refusal_text(const Choice& c, const std::vector<Choice>& all, size_t cur) {
    if (!c.space.ok) return "Could not read free space on " + c.name + ".\nTry again, or press X for another drive.";
    const std::string colon = (!c.name.empty() && c.name.back() == ':') ? " " : ": ";
    std::string t = "No space on " + c.name + colon + size_text(c.freeBytes);
    if (c.space.total) t += " of " + size_text(c.space.total);
    else t += " free";
    t += ".";
    size_t o = best_other(all, cur);
    if (o != all.size()) t += "\nX: install to " + label(all[o]) + ".";
    else t += "\nDelete an installed game to free space.";
    return t;
}

}  // namespace storage_roots
