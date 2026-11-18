// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>

namespace guest_overlay_input {
// RawGameController::FromGameController(Gamepad) exposes the four thumbstick
// axes followed by the two trigger axes. Sticks rest at center; triggers rest
// at zero. Do not infer an axis kind from its current sample: a left-held stick
// is zero too.
constexpr double neutral_axis(uint32_t index) {
    return index < 4 ? 0.5 : 0.0;
}

inline bool at_rest(uint32_t buttonCount, const bool* buttons,
                    uint32_t axisCount, const double* axes) {
    for (uint32_t i = 0; buttons && i < buttonCount; ++i)
        if (buttons[i]) return false;
    if (axisCount && !axes) return false;
    for (uint32_t i = 0; i < axisCount; ++i)
        if (std::abs(axes[i] - neutral_axis(i)) > 0.1) return false;
    return true;
}
}
