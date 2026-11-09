// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the detail-page buttons: B never cancels a download,
// A cancels one on a second press, and the labels say so.
#include "../detail_input.h"
#include "../launch_button.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace detail_input;

static State st(bool installed, Download d, bool armed = false) { State s; s.installed = installed; s.download = d; s.cancelArmed = armed; return s; }

int main() {
    // B always goes back; it never asks for or performs a cancel.
    for (bool installed : {false, true})
        for (Download d : {Download::none, Download::queued, Download::running}) {
            assert(press_b(st(installed, d)) == Action::back);
            assert(press_b(st(installed, d, true)) == Action::back);
        }

    // A with no download: install when missing, play when installed.
    assert(press_a(st(false, Download::none)) == Action::install);
    assert(press_a(st(true, Download::none)) == Action::play);
    assert(press_a(st(true, Download::none, true)) == Action::play);   // a stale arm never cancels anything

    // A while a download runs or is queued: ask once, cancel on the second press.
    for (Download d : {Download::queued, Download::running}) {
        for (bool installed : {false, true}) {
            assert(press_a(st(installed, d)) == Action::ask_cancel);
            assert(press_a(st(installed, d, true)) == Action::cancel);
        }
    }

    // The button and the hint under it name the action the press will take.
    assert(!std::strcmp(label(st(false, Download::none)), "INSTALL"));
    assert(!std::strcmp(label(st(true, Download::none)), "PLAY"));
    for (Download d : {Download::queued, Download::running}) {
        assert(!std::strcmp(label(st(false, d)), "INSTALLING... A: cancel"));
        assert(!std::strcmp(hint(st(false, d)), "B: back, download keeps running"));
        assert(!std::strcmp(hint(st(false, d, true)), "press A again to cancel"));
        assert(std::strstr(label(st(false, d)), "B:") == nullptr);   // the button never offers B as a cancel
    }
    assert(!std::strcmp(hint(st(false, Download::none)), "press A"));
    assert(cancel_window > 0);
    std::puts("DETAIL-INPUT-OK: B goes back only; A cancels a download on the second press");

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
