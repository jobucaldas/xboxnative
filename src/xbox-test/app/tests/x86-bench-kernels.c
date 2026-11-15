// SPDX-License-Identifier: GPL-3.0-or-later
// Shared native and i686 benchmark kernels. Build this source once for the host
// reference and once as a freestanding PE32 DLL for the interpreter.
#include <stdint.h>
#if defined(__i386__)
#include <xmmintrin.h>
#include <emmintrin.h>
#define EXPORT __attribute__((dllexport))
#else
#include <immintrin.h>
#define EXPORT
#endif

static volatile uint32_t sink;
static uint32_t src[4096], dst[4096];

EXPORT uint32_t bench_integer(void) {
    uint32_t x = 0x12345678u;
    for (uint32_t i = 0; i < 200000; ++i) x = (x ^ (i + 0x9e3779b9u)) * 33u + (x >> 7);
    sink = x; return x;
}

EXPORT uint32_t bench_memcpy_memset(void) {
    for (unsigned i = 0; i < 4096; ++i) src[i] = i * 17u + 3u;
    for (unsigned r = 0; r < 256; ++r) {
        const uint32_t* s = src; uint32_t* d = dst; uint32_t n = 4096;
        __asm__ volatile("cld; rep movsl" : "+S"(s), "+D"(d), "+c"(n) : : "memory");
        d = (uint32_t*)dst; n = sizeof dst; uint32_t v = r;
        __asm__ volatile("cld; rep stosb" : "+D"(d), "+c"(n) : "a"(v) : "memory");
    }
    sink = dst[0] ^ dst[4095]; return sink;
}

EXPORT uint32_t bench_float_sse2(void) {
    volatile double scalar = 1.25;
    for (unsigned i = 0; i < 10000; ++i) {
        volatile double product = scalar * 1.00001;
        scalar = product + 0.00001;
    }
    __m128 a = _mm_set_ps(4.f, 3.f, 2.f, 1.f);
    __m128 b = _mm_set1_ps(0.0001f);
    for (unsigned i = 0; i < 30000; ++i) a = _mm_add_ps(_mm_mul_ps(a, b), _mm_set1_ps(1.0001f));
    float out[4]; _mm_storeu_ps(out, a);
    uint32_t v; uint64_t d; __builtin_memcpy(&v, &out[0], sizeof v); __builtin_memcpy(&d, (const void*)&scalar, sizeof d);
    sink = v ^ (uint32_t)d ^ (uint32_t)(d >> 32); return sink;
}

__attribute__((noinline)) static uint32_t recurse(volatile unsigned n) { return n ? n + recurse(n - 1) : 1; }
EXPORT uint32_t bench_calls_recursion(void) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 10000; ++i) v += recurse(12);
    sink = v; return v;
}

EXPORT uint32_t bench_sprite_blit(void) {
    for (unsigned i = 0; i < 4096; ++i) src[i] = 0x80402010u + i * 13u;
    for (unsigned frame = 0; frame < 8; ++frame) {
        for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x) {
            uint32_t p = src[y * 64 + x], q = dst[y * 64 + x];
            uint32_t a = p >> 24;
            dst[y * 64 + x] = (((((p & 255) * a + (q & 255) * (255 - a)) >> 8) & 255) |
                (((((p >> 8) & 255) * a + ((q >> 8) & 255) * (255 - a)) >> 8) & 255) << 8 |
                (((((p >> 16) & 255) * a + ((q >> 16) & 255) * (255 - a)) >> 8) & 255) << 16 | 0xff000000u);
        }
    }
    sink = dst[4095]; return sink;
}
