// SPDX-License-Identifier: GPL-3.0-or-later
// Item D9: the game page's "Game size" row before a download starts. The store
// page only publishes a storage *requirement* ("Storage: 30 GB available
// space"), which is why an app whose store text lists none showed "Unknown".
// The app's own product info (PICS appinfo, the same buffer the downloader
// parses) carries the real figure per depot: manifests/public/download is the
// compressed size the client fetches. Summing it over exactly the depots the
// downloader installs (depot_select.h) gives the number the download itself
// will report. Pure and Win32-free: tests/depot_size_test.cpp runs it natively.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "depot_select.h"
#include "steam_wire.h"

namespace depot_size {

// Sum of manifests/public/download over the depots depot_select::choose keeps,
// or 0 when the product info has no usable figure. `chosen` receives the depot
// ids that were counted (for the log line).
inline uint64_t from_appinfo(const std::string& appinfo, std::vector<uint32_t>* chosen = nullptr) {
    Vdf root;
    if (!vdf_parse(appinfo, root)) return 0;
    const Vdf* ai = root.get("appinfo");
    if (!ai) ai = &root;
    const Vdf* dv = ai->get("depots");
    if (!dv) return 0;
    std::vector<depot_select::Depot> listed;
    std::map<uint32_t, uint64_t> download;
    for (size_t i = 0; i < dv->keys.size(); i++) {
        const std::string& k = dv->keys[i];
        bool digits = !k.empty();
        for (char c : k) if (!std::isdigit((unsigned char)c)) { digits = false; break; }
        if (!digits) continue;   // "baselanguages", "branches", ...
        const Vdf& d = dv->kids[i];
        if (!d.obj) continue;
        depot_select::Depot x;
        x.id = (uint32_t)std::strtoul(k.c_str(), nullptr, 10);
        x.oslist = d.str({"config", "oslist"});
        x.osarch = d.str({"config", "osarch"});
        x.language = d.str({"config", "language"});
        for (auto& c : x.language) c = (char)std::tolower((unsigned char)c);
        x.lowviolence = d.str({"config", "lowviolence"}) == "1";
        x.dlc = d.get("dlcappid") != nullptr;
        x.sharedFrom = d.str({"depotfromapp"});
        const Vdf* pm = d.at({"manifests", "public"});
        uint64_t dl = 0;
        if (pm) {
            x.gid = pm->obj ? pm->str({"gid"}) : pm->val;
            const std::string s = pm->str({"download"});
            if (!s.empty()) dl = std::strtoull(s.c_str(), nullptr, 10);
        }
        download[x.id] = dl;
        listed.push_back(x);
    }
    uint64_t total = 0;
    for (auto& d : depot_select::choose(listed)) {
        auto it = download.find(d.id);
        if (it != download.end()) total += it->second;
        if (chosen) chosen->push_back(d.id);
    }
    return total;
}

}  // namespace depot_size
