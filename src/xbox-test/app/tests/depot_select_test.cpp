// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the downloader's depot choice (depot_select.h).
#include "../depot_select.h"
#include <cassert>
#include <cstdio>
using namespace depot_select;

static Depot dep(uint32_t id, const char* os, const char* arch, const char* gid = "1", const char* lang = "", bool lv = false, bool dlc = false) {
    Depot d; d.id = id; d.oslist = os; d.osarch = arch; d.gid = gid; d.language = lang; d.lowviolence = lv; d.dlc = dlc; return d;
}
static std::vector<uint32_t> ids(const std::vector<Depot>& v) { std::vector<uint32_t> o; for (auto& d : v) o.push_back(d.id); return o; }

int main() {
    // separate 32- and 64-bit Windows builds (Slime Rancher shape): only the 64-bit one
    std::vector<Skip> sk;
    auto c = choose({dep(433341, "windows", "32"), dep(433342, "windows", "64"), dep(433343, "macos", ""), dep(433344, "linux", ""), dep(939480, "", "", "1", "", false, true)}, &sk);
    assert((ids(c) == std::vector<uint32_t>{433342}));
    assert(sk.size() == 4);

    // a 32-bit-only game keeps its 32-bit depot; OS-neutral content depots stay alongside the build
    assert((ids(choose({dep(10, "windows", "32"), dep(11, "", "")})) == std::vector<uint32_t>{10, 11}));
    assert((ids(choose({dep(20, "windows", "64"), dep(21, "", ""), dep(22, "windows", "32")})) == std::vector<uint32_t>{20, 21}));

    // depots shared from another app (redistributable installers) are not fetched
    { std::vector<Skip> sh; Depot r = dep(228986, "windows", ""); r.sharedFrom = "228980";
      assert((ids(choose({dep(40, "windows", "64"), r}, &sh)) == std::vector<uint32_t>{40}));
      assert(sh.size() == 1 && sh[0].id == 228986 && sh[0].why.find("228980") != std::string::npos); }

    // language and low-violence depots, missing manifests
    assert((ids(choose({dep(30, "windows", ""), dep(31, "windows", "", "1", "german"), dep(32, "windows", "", "1", "english"), dep(33, "windows", "", "1", "", true), dep(34, "windows", "", "")})) == std::vector<uint32_t>{30, 32}));

    // oslist matching is per entry, not substring
    assert(has_word("windows,macos", "windows") && has_word("Windows", "windows") && !has_word("notwindows", "windows") && !has_word("linux", "windows"));
    assert((ids(choose({dep(40, "macos,windows", ""), dep(41, "linux", "")})) == std::vector<uint32_t>{40}));

    std::printf("DEPOT-SELECT PASS\n");
    return 0;
}
