// SPDX-License-Identifier: GPL-3.0-or-later
// Which depots of an app the downloader fetches, following the Steam client's
// own rules for a 64-bit English Windows install: Windows (or OS-neutral)
// depots only, no DLC depots, no depot without a public manifest, no
// non-English language depot, no low-violence depot, and when the app ships a
// 64-bit Windows build its 32-bit builds are skipped (they usually carry the
// same file paths, so fetching both doubles the size and the two overwrite
// each other). No Win32 here: tests/depot_select_test.cpp runs it natively.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace depot_select {

struct Depot {
    uint32_t id = 0;
    std::string oslist, osarch, language, gid;   // appinfo depots/<id>/config/{oslist,osarch,language}, manifests/public gid
    bool lowviolence = false, dlc = false;
};
struct Skip { uint32_t id; std::string why; };

inline bool has_word(const std::string& list, const char* w) {   // "windows,macos" style lists
    std::string l; for (char c : list) l += (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    std::string n(w); size_t p = 0;
    while ((p = l.find(n, p)) != std::string::npos) {
        bool startOk = p == 0 || l[p - 1] == ',' || l[p - 1] == ' ', endOk = p + n.size() == l.size() || l[p + n.size()] == ',' || l[p + n.size()] == ' ';
        if (startOk && endOk) return true;
        p += n.size();
    }
    return false;
}

inline std::vector<Depot> choose(const std::vector<Depot>& all, std::vector<Skip>* skipped = nullptr) {
    auto skip = [&](const Depot& d, std::string why) { if (skipped) skipped->push_back({d.id, std::move(why)}); };
    std::vector<Depot> keep;
    for (auto& d : all) {
        if (!d.oslist.empty() && !has_word(d.oslist, "windows")) { skip(d, "os=" + d.oslist); continue; }
        if (d.dlc) { skip(d, "DLC depot"); continue; }
        if (d.gid.empty()) { skip(d, "no public manifest"); continue; }
        if (!d.language.empty() && d.language != "english") { skip(d, "language=" + d.language); continue; }
        if (d.lowviolence) { skip(d, "low-violence depot"); continue; }
        keep.push_back(d);
    }
    bool has64 = false; for (auto& d : keep) if (d.osarch == "64") has64 = true;
    if (!has64) return keep;
    std::vector<Depot> out;
    for (auto& d : keep) { if (d.osarch == "32") skip(d, "32-bit build (64-bit build present)"); else out.push_back(d); }
    return out;
}

}  // namespace depot_select
