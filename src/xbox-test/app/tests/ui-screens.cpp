// SPDX-License-Identifier: GPL-3.0-or-later
// Native test for the download-queue / storage screens' navigation contract
// (ui_nav.h): the sidebar entries keep their targets from any screen, the
// focus walk between the sidebar and the page's own rows wraps, and the
// queue-row status copy matches what main.cpp's row model draws. Assert-based,
// no rendering; run through tests/ui-screens.sh.
#include <cstdio>
#include <cstring>

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

    // ---- the sidebar entries the queue/storage screens hang off ----
    {
        // new nav order (review fix 3): Search(0), All games(1), Installed(2),
        // Download queue(3), Storage management(4)
        check(sidebar_target(3) == 3, "entry 3 opens the download queue");
        check(sidebar_target(4) == 4, "entry 4 opens storage management");
        int a = 3;
        sidebar_move(a, true);  check(a == 4, "sidebar down 3->4 (queue -> storage)");
        sidebar_move(a, false); check(a == 3, "sidebar up 4->3");
        sidebar_move(a, true);  sidebar_move(a, true);  check(a == 0, "sidebar down wraps 4->0");
    }

    // ---- focus walk between the sidebar and the page rows ----
    {
        Focus f = Focus::sidebar;
        check(lateral(f, 0, false) && f == Focus::grid, "right from the sidebar focuses the rows");
        check(lateral(f, 0, true) && f == Focus::sidebar, "left from the rows returns to the sidebar");
        f = Focus::grid;
        check(!lateral(f, 3, true) && f == Focus::grid, "left inside the rows with no sidebar does nothing");
    }

    // ---- queue row copy for every download state the queue screen draws ----
    {
        check(!std::strcmp(dl_status_text(1, 40, ""), "Downloading"), "running row reads Downloading");
        check(!std::strcmp(dl_status_text(2, 100, ""), "Installed"), "finished row reads Installed");
        check(!std::strcmp(dl_status_text(3, 0, "Cannot reach Steam"), "Cannot reach Steam"),
              "failed row shows the download message");
        check(!std::strcmp(dl_status_text(4, 0, ""), "Cancelled"), "cancelled row reads Cancelled");
        check(!std::strcmp(dl_status_text(0, 0, ""), "Waiting"), "idle row reads Waiting");
        check(!std::strcmp(dl_status_text(0, 40, ""), "Queued"), "queued row reads Queued");
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
