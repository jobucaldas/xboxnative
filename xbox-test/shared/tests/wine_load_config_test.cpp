// SPDX-License-Identifier: GPL-3.0-or-later
#include "../wine_load_config.h"
#include <array>
#include <cassert>
#include <cstdio>
int main() {
    std::array<unsigned char, 0x141> bytes{};
    const std::uint32_t size=0x140;
    std::memcpy(bytes.data(), &size, sizeof size);
    assert(wxl::inert_wine_load_config(bytes.data(), size, 0));
    assert(!wxl::inert_wine_load_config(nullptr, size, 0));
    assert(!wxl::inert_wine_load_config(bytes.data(), size-1, 0));
    assert(!wxl::inert_wine_load_config(bytes.data(), size+1, 0));
    assert(!wxl::inert_wine_load_config(bytes.data(), size, 0x4000));
    for (std::size_t i=0; i<size; ++i) {
        for (unsigned bit=0; bit<8; ++bit) {
            bytes[i]^=1u<<bit;
            assert(!wxl::inert_wine_load_config(bytes.data(), size, 0));
            bytes[i]^=1u<<bit;
        }
    }
    puts("WINE-LOAD-CONFIG PASS: inert-only, lengths, CFG, 2560 single-bit mutations");
}
