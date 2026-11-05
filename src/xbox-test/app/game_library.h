// SPDX-License-Identifier: GPL-3.0-or-later
// Portable rules for the installed-games view: parsing the downloader's
// dl_<appid>.done marker, deciding which install folder a Delete may remove,
// and filtering the library grid to installed games. No Win32 here so the
// rules are unit-tested natively (tests/game_library_test.cpp).
#pragma once
#include <cstdint>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace game_library {

// "app <id> build <b> files <n> bytes <n> installdir <dir>\n" (installdir absent before 0.5.0.0)
struct Marker { uint64_t bytes = 0; std::string installdir; };
inline Marker parse_marker(const std::string& s) {
    Marker m;
    size_t p = s.find(" bytes ");
    if (p != std::string::npos) m.bytes = std::strtoull(s.c_str() + p + 7, nullptr, 10);
    p = s.find(" installdir ");
    if (p != std::string::npos) {
        m.installdir = s.substr(p + 12);
        while (!m.installdir.empty() && (m.installdir.back() == '\n' || m.installdir.back() == '\r' || m.installdir.back() == ' ')) m.installdir.pop_back();
    }
    return m;
}

// "dl_<digits>.done" -> appid; anything else (dl_x.done, dl_1.done.bak, ...) is not a marker.
inline bool marker_appid(const std::string& name, uint32_t& appid) {
    const std::string pre = "dl_", suf = ".done";
    if (name.size() <= pre.size() + suf.size() || name.compare(0, pre.size(), pre) || name.compare(name.size() - suf.size(), suf.size(), suf)) return false;
    std::string digits = name.substr(pre.size(), name.size() - pre.size() - suf.size());
    if (digits.size() > 10) return false;
    for (char c : digits) if (c < '0' || c > '9') return false;
    unsigned long long v = std::strtoull(digits.c_str(), nullptr, 10);
    if (v == 0 || v > 0xFFFFFFFFull) return false;
    appid = (uint32_t)v; return true;
}

// The downloader writes installdir as one lower-case [a-z0-9._-] component under
// LocalState. Delete only removes a folder of exactly that shape, never "." or
// "..", and never a launcher-owned folder (steam\ holds provider settings and saves).
inline bool deletable_installdir(const std::string& d) {
    if (d.empty() || d.size() > 128 || d == "." || d == "..") return false;
    for (unsigned char c : d) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    if (d.find("..") != std::string::npos) return false;
    return d != "steam";
}

// Another installed app pointing at the same folder keeps the files; only this app's marker goes.
inline bool installdir_shared(uint32_t appid, const std::string& dir, const std::vector<std::pair<uint32_t, std::string>>& installs) {
    for (auto& i : installs) if (i.first != appid && i.second == dir) return true;
    return false;
}

// Library grid filter: all games, or only those with an install marker (original order kept).
template <class Ptr> std::vector<Ptr> shown(const std::vector<Ptr>& all, bool installedOnly, const std::set<uint32_t>& installed) {
    if (!installedOnly) return all;
    std::vector<Ptr> out;
    for (auto& g : all) if (installed.count(g->appid)) out.push_back(g);
    return out;
}

inline int clamp_selection(int sel, size_t n) { return n == 0 ? 0 : sel < 0 ? 0 : sel >= (int)n ? (int)n - 1 : sel; }

}  // namespace game_library
