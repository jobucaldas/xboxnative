// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace wxl::strict_policy {
enum class Classification : std::uint8_t { missing, probe_only, callable };
struct Decision { Classification classification; std::uintptr_t address; };

// Only an explicitly callable, non-null target may be invoked by a resolver.
constexpr bool may_call(Decision d) noexcept {
    return d.classification == Classification::callable && d.address != 0;
}
} // namespace wxl::strict_policy
