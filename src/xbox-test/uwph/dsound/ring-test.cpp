#include "ring.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    uint8_t bytes[16]{};
    dsound_core::Ring ring(bytes, sizeof bytes, 2);
    dsound_core::Regions regions{};
    assert(ring.lock(12, 8, false, regions));
    assert(regions.first_bytes == 4 && regions.second_bytes == 4 && regions.second_offset == 0);
    assert(!ring.lock(0, 2, false, regions));
    assert(!ring.unlock(12, 6));
    assert(ring.unlock(12, 8));
    assert(ring.cursor(3, 12, true) == 2);
    assert(ring.cursor(3, 12, false) == 16);
    assert(ring.crossed(0, 2, 12, 14, false));
    assert(!ring.crossed(0, 0, 12, 14, false));
    assert(ring.crossed(0, 3, 12, 0, true));
    assert(ring.crossed(0, 3, 12, 2, true));
    assert(!ring.crossed(1, 2, 12, 2, true));
    assert(ring.lock(8, 16, true, regions));
    assert(regions.first_bytes == 16 && regions.second_bytes == 0);
    assert(ring.unlock(0, 16));
    std::puts("PASS: DirectSound ring lock wrap and consumed-byte cursors");
}
