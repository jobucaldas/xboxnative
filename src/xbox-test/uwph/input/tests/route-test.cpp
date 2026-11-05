// SPDX-License-Identifier: GPL-3.0-or-later
#include "../route.h"
#include <cstdio>

static bool check(bool ok, const char* what) {
    if (!ok) std::fprintf(stderr, "FAIL %s\n", what);
    return ok;
}
int main() {
    using namespace wxl::input;
    Sink launcher, guest; Router r(launcher, guest);
    r.press("A"); r.release();
    if (!check(launcher.events.size()==2 && guest.events.empty(), "launcher ordered pair")) return 1;
    if (!check(launcher.events[0].edge==Edge::press && launcher.events[1].edge==Edge::release, "launcher order")) return 1;
    r.focus(Focus::guest); r.press("B"); r.release();
    if (!check(guest.events.size()==2 && launcher.events.size()==2, "guest only when focused")) return 1;
    if (!check(guest.events[0].edge==Edge::press && guest.events[1].edge==Edge::release, "guest order")) return 1;
    r.focus(Focus::none); r.press("C"); r.release();
    if (!check(guest.events.size()==2 && launcher.events.size()==2, "inactive guest receives nothing")) return 1;
    r.focus(Focus::guest); r.press("D"); r.focus(Focus::none);
    if (!check(guest.events.size()==4 && guest.events[2].edge==Edge::press && guest.events[3].edge==Edge::release, "focus loss releases held button")) return 1;
    const auto guest_before = guest.events.size();
    r.focus(Focus::launcher); r.press("L"); r.focus(Focus::guest);
    if (!check(launcher.events.size()==4 && launcher.events[2].edge==Edge::press && launcher.events[3].edge==Edge::release, "launcher-to-guest releases original sink")) return 1;
    if (!check(guest.events.size()==guest_before, "launcher-to-guest no phantom guest release")) return 1;
    r.press("G"); r.focus(Focus::launcher);
    if (!check(guest.events.size()==guest_before+2 && guest.events[guest_before].edge==Edge::press && guest.events[guest_before+1].edge==Edge::release, "guest-to-launcher releases original sink")) return 1;
    const auto launcher_before = launcher.events.size();
    r.press("N"); r.focus(Focus::none);
    if (!check(launcher.events.size()==launcher_before+2 && launcher.events[launcher_before].edge==Edge::press && launcher.events[launcher_before+1].edge==Edge::release, "launcher-to-none releases on focus loss")) return 1;
    r.press("X"); r.focus(Focus::launcher); r.focus(Focus::launcher); r.release();
    if (!check(launcher.events.size()==launcher_before+2, "release after loss and repeated focus do not duplicate")) return 1;
    // Exhaust all focus transitions while a button is held, including no-op
    // transitions. This synthetic router intentionally permits only one hold.
    for (Focus from : {Focus::launcher, Focus::guest, Focus::none}) {
        for (Focus to : {Focus::launcher, Focus::guest, Focus::none}) {
            Sink l, g; Router route(l, g);
            route.focus(from);
            route.release(); // no prior press
            route.press("held");
            route.press("held"); // repeat must not duplicate the press
            route.press("other"); // cannot replace the held button
            route.focus(to);
            const auto& original = from == Focus::guest ? g.events : l.events;
            const auto& other = from == Focus::guest ? l.events : g.events;
            const std::size_t before_release = from == Focus::none ? 0 : (from == to ? 1 : 2);
            if (!check(original.size() == before_release && other.empty(), "focus transition routing")) return 1;
            route.release();
            route.release(); // redundant release must be silent
            if (from == Focus::none) {
                if (!check(l.events.empty() && g.events.empty(), "inactive presses are not buffered")) return 1;
            } else if (!check(original.size() == 2 && other.empty()
                       && original[0].button == "held" && original[0].edge == Edge::press
                       && original[1].button == "held" && original[1].edge == Edge::release,
                       "exact original-sink button pair")) return 1;
            l.events.clear(); g.events.clear();
            route.press("next"); route.release();
            const auto& active = to == Focus::guest ? g.events : l.events;
            const auto& inactive = to == Focus::guest ? l.events : g.events;
            if (to == Focus::none) {
                if (!check(l.events.empty() && g.events.empty(), "unfocused new press dropped")) return 1;
            } else if (!check(active.size() == 2 && inactive.empty()
                       && active[0].button == "next" && active[0].edge == Edge::press
                       && active[1].button == "next" && active[1].edge == Edge::release,
                       "new press follows current focus")) return 1;
        }
    }
    std::puts("PASS input routing fixture (ordered pairs, 9 focus transitions, repeated input, button identity)");
}
