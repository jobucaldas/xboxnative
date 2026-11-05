// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace launch_status {
enum class Outcome { starting, running, completed, failed, timedOut };

// A window/device milestone is progress, never a terminal success. Completion
// wins even when a previously reached milestone remains set after a failure.
constexpr Outcome observe(bool finished, bool success, int milestone, bool expired) {
    if(finished)return success?Outcome::completed:Outcome::failed;
    if(milestone)return Outcome::running;
    return expired?Outcome::timedOut:Outcome::starting;
}
constexpr bool terminal(Outcome outcome) {
    return outcome==Outcome::completed || outcome==Outcome::failed || outcome==Outcome::timedOut;
}
}
