// SPDX-License-Identifier: GPL-3.0-or-later
#include "../resolve-policy.h"
#include <array>
#include <cstdio>

int main() {
    using namespace wxl::strict_policy;
    constexpr std::uintptr_t target = 0x1234;
    const std::array cases{
        Decision{Classification::callable, target},
        Decision{Classification::callable, 0},
        Decision{Classification::probe_only, target},
        Decision{Classification::probe_only, 0},
        Decision{Classification::missing, target},
        Decision{Classification::missing, 0},
    };
    constexpr std::array<bool, 6> expected{true, false, false, false, false, false};
    for (std::size_t i = 0; i < cases.size(); ++i) {
        if (may_call(cases[i]) != expected[i]) {
            std::fprintf(stderr, "FAIL policy case %zu\n", i);
            return 1;
        }
    }
    std::puts("PASS strict resolver classification policy (6 cases)");
    return 0;
}
