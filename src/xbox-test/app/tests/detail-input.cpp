// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the detail-page buttons: B and the d-pad never cancel a
// download, one A press cancels it, the press that started the install cannot,
// and the labels say so.
#include "../detail_input.h"
#include "../launch_button.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace detail_input;

static State st(bool installed, Download d, double sinceStart = 1e9) { State s; s.installed = installed; s.download = d; s.sinceStart = sinceStart; return s; }

int main() {
    // B always goes back; it never cancels.
    for (bool installed : {false, true})
        for (Download d : {Download::none, Download::queued, Download::running})
            for (double since : {0.0, 0.5, 1e9})
                assert(press_b(st(installed, d, since)) == Action::back);

    // The d-pad scrolls; it never cancels either.
    for (bool installed : {false, true})
        for (Download d : {Download::none, Download::queued, Download::running})
            for (double since : {0.0, 0.5, 1e9})
                assert(press_dpad(st(installed, d, since)) == Action::ignore);

    // A with no download: install when missing, play when installed.
    assert(press_a(st(false, Download::none)) == Action::install);
    assert(press_a(st(true, Download::none)) == Action::play);
    assert(press_a(st(true, Download::none, 0.0)) == Action::play);   // the start guard never blocks play

    // A while a download runs or is queued: ONE press cancels it.
    for (Download d : {Download::queued, Download::running})
        for (bool installed : {false, true}) {
            assert(press_a(st(installed, d)) == Action::cancel);
            assert(press_a(st(installed, d, cancel_guard)) == Action::cancel);
        }

    // The press that started the install cannot also cancel it: A is ignored
    // until cancel_guard seconds have passed (it needs a release anyway).
    assert(cancel_guard > 0);
    for (Download d : {Download::queued, Download::running})
        for (double since : {0.0, 0.25, cancel_guard - 0.01})
            assert(press_a(st(false, d, since)) == Action::ignore);

    // The button and the hint under it name the action the press will take.
    assert(!std::strcmp(label(st(false, Download::none)), "INSTALL"));
    assert(!std::strcmp(label(st(true, Download::none)), "PLAY"));
    for (Download d : {Download::queued, Download::running}) {
        assert(!std::strcmp(label(st(false, d)), "INSTALLING... A: cancel"));
        assert(!std::strcmp(hint(st(false, d)), "B: back, download keeps running"));
        assert(std::strstr(label(st(false, d)), "B:") == nullptr);   // the button never offers B as a cancel
        assert(std::strstr(hint(st(false, d)), "again") == nullptr); // no second press is ever asked for
    }
    assert(!std::strcmp(hint(st(false, Download::none)), "press A"));
    assert(!std::strcmp(hint(st(true, Download::none)), "Installed. Press A to play"));
    std::puts("DETAIL-INPUT-OK: B and the d-pad go back/scroll only; one A press cancels, not the start press");

    // Every label the detail button can show fits its padding, centered.
    for (const char* l : {"GUEST RUNNING", "LAUNCHING...", "RESTART REQUIRED", "DELETING...", "INSTALLING... A: cancel", "PLAY", "INSTALL"}) {
        auto layout = launch_button::label_layout(std::strlen(l));
        int textWidth = static_cast<int>(std::strlen(l)) * 8 * layout.scale;
        assert(layout.x >= launch_button::padding);
        assert(layout.x + textWidth <= launch_button::width - launch_button::padding);
        assert(layout.x * 2 + textWidth == launch_button::width);
        assert(layout.y >= 28 && layout.y + 8 * layout.scale <= 68);
    }
    std::puts("DETAIL-BUTTON-OK: every detail action label fits padding and stays centered");
    return 0;
}
