// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace wxl {
// msvc_x64 is Play's policy: an inert Wine record, the cookie-only MSVC record,
// or an MSVC record whose only extras are GuardFlags==CF_INSTRUMENTED and a
// VolatileMetadataPointer. Wine 10 (dlls/ntdll/loader.c update_load_config)
// initializes only the security cookie on x86_64 and never touches guard
// slots, GuardFlags or volatile metadata; msvc_x64 does exactly the same.
enum class LoadConfig { reject, inert_wine, msvc_x64_cookie, msvc_x64 };
// Wine 10 winebuild emits a 0x140-byte x64 record even when it requests no
// loader work. Do not accept a cookie, CFG/XFG, dynamic relocations, or unknown
// extensions: supporting those requires actual initialization, not ignoring it.
inline bool inert_wine_load_config(const unsigned char* bytes, std::size_t size,
                                   unsigned dll_characteristics) {
    if (!bytes || size != 0x140 || (dll_characteristics & 0x4000)) return false; // GUARD_CF
    std::uint32_t declared = 0;
    std::memcpy(&declared, bytes, sizeof declared);
    if (declared != size) return false;
    for (std::size_t i = sizeof declared; i < size; ++i)
        if (bytes[i]) return false;
    return true;
}
}
