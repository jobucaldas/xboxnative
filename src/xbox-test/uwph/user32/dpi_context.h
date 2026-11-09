// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The DPI_AWARENESS_CONTEXT encoding and validation rules, with no Windows
// dependency so a host compiler can check them in CI. Ported from Wine 10
// user32 get_ntuser_dpi_context/is_valid_dpi_awareness_context and the
// NTUSER_DPI_CONTEXT_* macros in include/ntuser.h; dpi.h wraps these in the
// Windows-facing entry points and static_asserts the public context constants
// still carry the values assumed here.
namespace u32c_dpi {
// Public DPI_AWARENESS_CONTEXT constants (windef.h), as integers.
constexpr unsigned long long context_unaware = ~0ull;             // -1
constexpr unsigned long long context_system_aware = ~1ull;        // -2
constexpr unsigned long long context_per_monitor_aware = ~2ull;   // -3
constexpr unsigned long long context_per_monitor_aware_v2 = ~3ull;// -4
constexpr unsigned long long context_unaware_gdiscaled = ~4ull;   // -5

constexpr unsigned awareness_invalid = ~0u;   // DPI_AWARENESS_INVALID (-1)
constexpr unsigned awareness_unaware = 0;
constexpr unsigned awareness_system_aware = 1;
constexpr unsigned awareness_per_monitor_aware = 2;

constexpr unsigned default_screen_dpi = 96;   // USER_DEFAULT_SCREEN_DPI

// NTUSER_DPI_CONTEXT bit layout.
constexpr unsigned make_context(unsigned awareness, unsigned version, unsigned dpi, unsigned flags) {
    return awareness | (version << 4) | (dpi << 8) | flags;
}
constexpr unsigned get_awareness(unsigned context) { return context & 0x0f; }
constexpr unsigned get_version(unsigned context) { return (context & 0xf0) >> 4; }
constexpr unsigned get_dpi(unsigned context) { return (context & 0x1ff00) >> 8; }
constexpr unsigned get_flags(unsigned context) { return context & 0xfffe0000u; }
constexpr unsigned flag_gdiscaled = 0x40000000u;
constexpr unsigned flag_process = 0x80000000u;
constexpr unsigned flag_valid_mask = flag_process | flag_gdiscaled;

constexpr unsigned unaware = make_context(awareness_unaware, 1, default_screen_dpi, 0);
constexpr unsigned system_aware = make_context(awareness_system_aware, 1, default_screen_dpi, 0);
constexpr unsigned per_monitor_aware = make_context(awareness_per_monitor_aware, 1, 0, 0);
constexpr unsigned per_monitor_aware_v2 = make_context(awareness_per_monitor_aware, 2, 0, 0);
constexpr unsigned per_unaware_gdiscaled = make_context(awareness_unaware, 1, default_screen_dpi, flag_gdiscaled);

// get_ntuser_dpi_context: the public constants map to the internal encoding;
// anything else is already an encoded context and passes through.
constexpr unsigned value(unsigned long long context) {
    if (context == context_unaware) return unaware;
    if (context == context_system_aware) return system_aware;
    if (context == context_per_monitor_aware) return per_monitor_aware;
    if (context == context_per_monitor_aware_v2) return per_monitor_aware_v2;
    if (context == context_unaware_gdiscaled) return per_unaware_gdiscaled;
    return static_cast<unsigned>(context);
}

// is_valid_dpi_awareness_context. `dpi` is the system DPI (96 here); a zero
// `dpi` skips the system-aware DPI check, as Wine's callers do when they only
// validate the encoding.
constexpr bool valid(unsigned context, unsigned dpi) {
    switch (get_awareness(context)) {
    case awareness_unaware:
        if (get_flags(context) & ~flag_valid_mask) return false;
        if (get_version(context) != 1) return false;
        return get_dpi(context) == default_screen_dpi;
    case awareness_system_aware:
        if (get_flags(context) & ~flag_valid_mask) return false;
        if (get_flags(context) & flag_gdiscaled) return false;
        if (get_version(context) != 1) return false;
        return !dpi || get_dpi(context) == dpi;
    case awareness_per_monitor_aware:
        if (get_flags(context) & ~flag_valid_mask) return false;
        if (get_flags(context) & flag_gdiscaled) return false;
        if (get_version(context) != 1 && get_version(context) != 2) return false;
        return get_dpi(context) == 0;
    }
    return false;
}

// AreDpiAwarenessContextsEqual: the PROCESS flag is not part of identity.
constexpr bool equal(unsigned a, unsigned b) {
    if (!a || !b) return false;
    return (a & ~flag_process) == (b & ~flag_process);
}
}
