// SPDX-License-Identifier: GPL-3.0-or-later
// Native test for ui_nav.h: sidebar <-> grid focus, sidebar view switching,
// sort cycling, queue-row status copy and the game screen's icon-button focus
// rules. Assert-based, no rendering; run through tests/ui-nav.sh.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "../ui_nav.h"

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using namespace ui_nav;

    // ---- lateral: left from the first grid column reaches the sidebar ----
    {
        Focus f = Focus::grid;
        check(lateral(f, 0, true) && f == Focus::sidebar, "left from column 0 enters the sidebar");
        f = Focus::grid;
        check(!lateral(f, 3, true) && f == Focus::grid, "left inside the grid does nothing");
        f = Focus::sidebar;
        check(lateral(f, 0, false) && f == Focus::grid, "right from the sidebar returns to the grid");
        f = Focus::grid;
        check(!lateral(f, 0, false) && f == Focus::grid, "right inside the grid does nothing");
    }

    // ---- sidebar movement wraps over the four entries ----
    {
        int a = 0;
        sidebar_move(a, true);  check(a == 1, "sidebar down 0->1");
        sidebar_move(a, true);  check(a == 2, "sidebar down 1->2");
        sidebar_move(a, true);  check(a == 3, "sidebar down 2->3");
        sidebar_move(a, true);  check(a == 0, "sidebar down wraps 3->0");
        sidebar_move(a, false); check(a == 3, "sidebar up wraps 0->3");
        // targets: 0/1 library, 2 download queue, 3 storage management
        check(sidebar_target(0) == 0 && sidebar_target(1) == 1, "entry 1 targets the installed view");
        check(sidebar_target(2) == 2, "entry 2 opens the download queue");
        check(sidebar_target(3) == 3, "entry 3 opens storage management");
    }

    // ---- sort cycling wraps over the existing three modes ----
    {
        check(cycle_sort(0, 3) == 1, "sort 0->1 (recently played exists)");
        check(cycle_sort(1, 3) == 2, "sort 1->2");
        check(cycle_sort(2, 3) == 0, "sort 2->0 wraps");
        check(cycle_sort(0, 0) == 0, "no modes keeps 0");
    }

    // ---- queue row status copy per download state ----
    {
        check(!std::strcmp(dl_status_text(1, 62, ""), "Downloading"), "state 1 reads Downloading");
        check(!std::strcmp(dl_status_text(2, 100, ""), "Installed"), "state 2 reads Installed");
        check(!std::strcmp(dl_status_text(3, 0, "Cannot reach Steam"), "Cannot reach Steam"),
              "state 3 shows the download message");
        check(!std::strcmp(dl_status_text(3, 0, ""), "Failed"), "state 3 without a message reads Failed");
        check(!std::strcmp(dl_status_text(4, 0, ""), "Cancelled"), "state 4 reads Cancelled");
        check(!std::strcmp(dl_status_text(0, 0, ""), "Waiting"), "idle reads Waiting");
        check(!std::strcmp(dl_status_text(0, 40, ""), "Queued"), "idle with progress reads Queued");
    }

    // ---- game screen focus: slots and icon buttons ----
    {
        check(focus_count(true) == 4 && focus_count(false) == 3, "installed has 4 slots, otherwise 3");
        // installed: trash/cloud/cog at slots 1/2/3
        check(icon_focus_slot(true, 0) == 1 && icon_focus_slot(true, 1) == 2 && icon_focus_slot(true, 2) == 3,
              "installed icon buttons sit at slots 1..3");
        check(icon_for_slot(true, 0) == -1, "slot 0 is the primary button");
        check(icon_for_slot(true, 1) == 0 && icon_for_slot(true, 2) == 1 && icon_for_slot(true, 3) == 2,
              "installed slots map to trash/cloud/cog");
        // not installed: cloud/cog at slots 2/3, no trash
        check(icon_focus_slot(false, 0) == 2 && icon_focus_slot(false, 1) == 3,
              "not-installed icon buttons sit at slots 2/3");
        check(icon_for_slot(false, 1) == -1, "slot 1 does not exist when not installed");
        check(icon_for_slot(false, 2) == 0 && icon_for_slot(false, 3) == 1,
              "not-installed slots map to cloud/cog");
    }

    // ---- logout reachability ----
    {
        check(logout_request(false, Focus::grid, 0, 0, true), "B on the grid asks to log out");
        check(!logout_request(false, Focus::sidebar, 0, 0, true), "B in the sidebar does not");
        check(!logout_request(true, Focus::grid, 0, 0, true), "an open confirm sheet does not re-open");
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
