// SPDX-License-Identifier: GPL-3.0-or-later
// Host-compiler check of the DPI_AWARENESS_CONTEXT encoding and validation
// rules, no Windows headers. The expected values are Wine 10's: the NTUSER_DPI_*
// constants in include/ntuser.h and the accept/reject decisions of
// is_valid_dpi_awareness_context in dlls/user32/sysparams.c.
#include "../dpi_context.h"
#include <cstdio>
#include <initializer_list>

using namespace u32c_dpi;
static int failures;
static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; }
}

int main() {
    // The five encoded contexts Wine builds for the public constants.
    check(unaware == 0x6010, "NTUSER_DPI_UNAWARE");
    check(system_aware == 0x6011, "NTUSER_DPI_SYSTEM_AWARE");
    check(per_monitor_aware == 0x12, "NTUSER_DPI_PER_MONITOR_AWARE");
    check(per_monitor_aware_v2 == 0x22, "NTUSER_DPI_PER_MONITOR_AWARE_V2");
    check(per_unaware_gdiscaled == 0x40006010, "NTUSER_DPI_PER_UNAWARE_GDISCALED");

    // get_ntuser_dpi_context: public constants translate, encodings pass through.
    check(value(context_unaware) == unaware, "value(UNAWARE)");
    check(value(context_system_aware) == system_aware, "value(SYSTEM_AWARE)");
    check(value(context_per_monitor_aware) == per_monitor_aware, "value(PER_MONITOR_AWARE)");
    check(value(context_per_monitor_aware_v2) == per_monitor_aware_v2, "value(PER_MONITOR_AWARE_V2)");
    check(value(context_unaware_gdiscaled) == per_unaware_gdiscaled, "value(UNAWARE_GDISCALED)");
    check(value(0x6010) == 0x6010 && value(0) == 0 && value(0x1234) == 0x1234, "value passes encodings through");

    // Bit accessors.
    check(get_awareness(unaware) == awareness_unaware, "awareness(unaware)");
    check(get_awareness(system_aware) == awareness_system_aware, "awareness(system)");
    check(get_awareness(per_monitor_aware_v2) == awareness_per_monitor_aware, "awareness(per-monitor)");
    check(get_version(unaware) == 1 && get_version(per_monitor_aware_v2) == 2, "version bits");
    check(get_dpi(unaware) == 96 && get_dpi(per_monitor_aware) == 0, "dpi bits");
    check(get_flags(per_unaware_gdiscaled) == flag_gdiscaled, "gdiscaled flag");
    check(get_flags(0x80006011) == flag_process, "process flag");

    // is_valid_dpi_awareness_context: the five constants are valid at 96 DPI.
    for (unsigned c : {unaware, system_aware, per_monitor_aware, per_monitor_aware_v2, per_unaware_gdiscaled})
        check(valid(c, 96), "constant valid at 96 dpi");
    check(valid(unaware, 0) && valid(system_aware, 0) && valid(per_monitor_aware, 0),
          "valid with dpi check skipped");
    // System-aware is pinned to the system DPI; per-monitor must carry no DPI.
    check(!valid(system_aware, 120), "system-aware rejects a different dpi");
    check(!valid(make_context(awareness_per_monitor_aware, 1, 96, 0), 0), "per-monitor rejects a dpi");
    // Versions: unaware and system-aware are version 1 only; per-monitor is 1 or 2.
    check(!valid(make_context(awareness_unaware, 2, 96, 0), 0), "unaware rejects version 2");
    check(!valid(make_context(awareness_system_aware, 2, 96, 0), 0), "system-aware rejects version 2");
    check(!valid(make_context(awareness_per_monitor_aware, 3, 0, 0), 0), "per-monitor rejects version 3");
    // Unaware is pinned to 96 DPI.
    check(!valid(make_context(awareness_unaware, 1, 120, 0), 0), "unaware rejects a different dpi");
    // GDISCALED is only meaningful for unaware.
    check(!valid(make_context(awareness_system_aware, 1, 96, flag_gdiscaled), 0), "system-aware rejects gdiscaled");
    check(!valid(make_context(awareness_per_monitor_aware, 1, 0, flag_gdiscaled), 0), "per-monitor rejects gdiscaled");
    // Unknown awareness values and stray flag bits are rejected.
    check(!valid(make_context(3, 1, 0, 0), 0), "unknown awareness");
    check(!valid(0x1234, 0) && !valid(0, 0), "garbage and zero");
    check(!valid(make_context(awareness_unaware, 1, 96, 0x10000), 0), "stray flag bit");
    // The PROCESS flag is part of the valid mask (it is how a thread reports
    // "following the process"), so it does not invalidate a context.
    check(valid(make_context(awareness_system_aware, 1, 96, flag_process), 96), "process flag allowed");

    // AreDpiAwarenessContextsEqual ignores the PROCESS flag, and zero matches nothing.
    check(equal(unaware, unaware) && equal(system_aware, system_aware), "equal to itself");
    check(equal(0x80006011, system_aware) && equal(system_aware, 0x80006011), "equal ignores the process flag");
    check(!equal(unaware, system_aware) && !equal(per_monitor_aware, per_monitor_aware_v2), "different contexts");
    check(!equal(0, 0) && !equal(0, unaware) && !equal(unaware, 0), "zero is never equal");

    if (failures) return 1;
    std::printf("PASS: DPI awareness context encoding/validation (%d checks)\n", 40);
    return 0;
}
