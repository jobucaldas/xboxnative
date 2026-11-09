// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for what the detail page does when a download finishes:
// it refreshes the install state and says so. It never launches the game, so
// the player presses A (Action::play) to start it.
#include "../detail_input.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace detail_input;

int main() {
    // A finished, verified install only refreshes the page.
    assert(after_download(true, true) == AfterDownload::refresh);
    // A failed or cancelled download, or one that left no marker, does nothing.
    assert(after_download(false, false) == AfterDownload::none);
    assert(after_download(false, true) == AfterDownload::none);   // cancelled after the marker existed
    assert(after_download(true, false) == AfterDownload::none);   // verified but no marker written

    // The page says the game is ready and that A starts it; it never claims a launch.
    assert(!std::strcmp(finished_text(), "Installed. Press A to play"));
    assert(std::strstr(finished_text(), "launch") == nullptr);
    assert(std::strstr(finished_text(), "starting") == nullptr);

    // After the refresh the button is PLAY, so the next A press is the launch.
    State ready; ready.installed = true; ready.download = Download::none;
    assert(press_a(ready) == Action::play);
    assert(!std::strcmp(label(ready), "PLAY"));
    assert(!std::strcmp(hint(ready), finished_text()));
    std::puts("DETAIL-AFTER-DOWNLOAD-OK: a finished download refreshes the page and never launches");
    return 0;
}
