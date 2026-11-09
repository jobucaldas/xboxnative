// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>

namespace launch_button {
constexpr int width=616;
constexpr int padding=16;
struct Label { int x, y, scale; };

// The bitmap font occupies eight pixels per character. Fit all current action
// labels inside the button padding, preserving the original 40px text band.
constexpr Label label_layout(std::size_t characters) {
    int scale=characters*8*5>width-2*padding?characters*8*4>width-2*padding?3:4:5;
    int textWidth=static_cast<int>(characters)*8*scale;
    return {(width-textWidth)/2,28+(5-scale)*4,scale};
}
}
