// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace launch_arguments {
// ASCII arguments selected once per request and carried to the runtime.
// These existing Unity options are Megabonk-specific, not a generic launch config.
inline constexpr const char* for_app(std::uint32_t appid) {
    return appid==3405340 ? "-screen-fullscreen 0 -force-d3d11 -logFile vis-player.log" : "";
}
}
