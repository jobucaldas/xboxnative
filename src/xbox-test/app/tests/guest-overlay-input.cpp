// SPDX-License-Identifier: GPL-3.0-or-later
#include "../guest_overlay_input.h"
#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
    using guest_overlay_input::at_rest;
    using guest_overlay_input::neutral_axis;
    const bool released[1] = {false};
    const bool pressed[1] = {true};
    const double centered[6] = {0.5, 0.5, 0.5, 0.5, 0.0, 0.0};
    const double leftHeld[6] = {0.0, 0.5, 0.5, 0.5, 0.0, 0.0};
    const double triggerHeld[6] = {0.5, 0.5, 0.5, 0.5, 0.7, 0.0};
    CHECK(neutral_axis(0) == 0.5 && neutral_axis(3) == 0.5);
    CHECK(neutral_axis(4) == 0.0 && neutral_axis(5) == 0.0);
    CHECK(at_rest(1, released, 6, centered));
    CHECK(!at_rest(1, pressed, 6, centered));
    CHECK(!at_rest(1, released, 6, leftHeld));
    CHECK(!at_rest(1, released, 6, triggerHeld));
    CHECK(!at_rest(0, nullptr, 1, nullptr));
    std::puts("PASS guest overlay raw-pad rest axes distinguish sticks and triggers");
    return 0;
}
