// SPDX-License-Identifier: GPL-3.0-or-later
// x87 and SSE/SSE2 register state for the PE32 interpreter (stage 2).
//
// x87 registers hold the 80-bit extended value in memory byte order, so a
// 10-byte load/store round-trips exactly. Arithmetic goes through `Wide`:
// on the off-device host (x86-64 Linux) long double *is* the x87 extended
// type, so every operation is the hardware operation; where long double is
// 64-bit (MSVC/clang-cl, the console build) `wide_is_extended` is false and
// x87 arithmetic is done in double, which is a documented precision limit of
// that build, not a silent one. SSE is exact on every host: the register is
// the 128-bit value.
#pragma once
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace x86 {
#if defined(LDBL_MANT_DIG) && (LDBL_MANT_DIG >= 64)
typedef long double Wide;
static const bool wide_is_extended = true;
#else
typedef double Wide;
static const bool wide_is_extended = false;
#endif

struct Fpu {
    uint8_t st[8][10];  // 80-bit extended, memory order: 64-bit significand, then sign/exponent
    bool empty[8];
    int top;
    uint16_t cw;  // control word (0x037f after fninit: round to nearest, 64-bit precision)
    uint16_t sw;  // status word; C0-C3 are kept, TOP is derived from `top`
    Fpu() { init(); }
    void init() {
        std::memset(st, 0, sizeof st);
        for (bool& e : empty) e = true;
        top = 0; cw = 0x037f; sw = 0;
    }
    static Wide to_wide(const uint8_t* b) {
        if constexpr (wide_is_extended) {
            Wide v = 0;
            std::memcpy(&v, b, 10);
            return v;
        } else {
            uint64_t sig; uint16_t se;
            std::memcpy(&sig, b, 8); std::memcpy(&se, b + 8, 2);
            int e = se & 0x7fff;
            double v;
            if (e == 0x7fff) v = (sig & 0x7fffffffffffffffull) ? std::nan("") : HUGE_VAL;
            else if (sig == 0) v = 0;
            else v = std::ldexp(double(sig), e - 16383 - 63);  // sig * 2^(e-16383-63)
            return (se & 0x8000) ? -v : v;
        }
    }
    static void from_wide(uint8_t* b, Wide v) {
        if constexpr (wide_is_extended) {
            std::memcpy(b, &v, 10);
            return;
        } else {
            uint16_t se = 0;
            if (std::signbit(v)) { se = 0x8000; v = -v; }
            uint64_t sig = 0; int e = 0;
            if (std::isnan(v)) { se |= 0x7fff; sig = 0xc000000000000000ull; }
            else if (std::isinf(v)) { se |= 0x7fff; sig = 0x8000000000000000ull; }
            else if (v != 0) {
                int e2 = 0;
                Wide m = std::frexp(v, &e2);   // v = m * 2^e2, 0.5 <= m < 1
                Wide s = std::ldexp(m, 64);    // [2^63, 2^64)
                sig = uint64_t(s);
                Wide frac = s - Wide(sig);     // round to nearest, ties to even
                if (frac > Wide(0.5) || (frac == Wide(0.5) && (sig & 1))) ++sig;
                e = e2 + 16382;                // v = sig * 2^(e2-64) = sig * 2^(e-16383-63)
                if (sig == 0) { sig = 0x8000000000000000ull; ++e; }
                if (e >= 0x7fff) { se |= 0x7fff; sig = 0x8000000000000000ull; }
                else if (e <= 0) {             // denormal
                    int sh = 1 - e;
                    if (sh >= 64) { sig = 0; e = 0; }
                    else {
                        uint64_t half = uint64_t(1) << (sh - 1), mask = (uint64_t(1) << sh) - 1;
                        uint64_t rem = sig & mask;
                        sig >>= sh;
                        if (rem > half || (rem == half && (sig & 1))) ++sig;
                        e = 0;
                    }
                }
            }
            se |= uint16_t(e);
            std::memcpy(b, &sig, 8); std::memcpy(b + 8, &se, 2);
        }
    }
    int idx(int i) const { return (top + i) & 7; }
    Wide get(int i) const { return to_wide(st[idx(i)]); }
    void set(int i, Wide v) { from_wide(st[idx(i)], v); empty[idx(i)] = false; }
    void push(Wide v) { top = (top + 7) & 7; set(0, v); }
    void pop() { empty[idx(0)] = true; top = (top + 1) & 7; }
    // fxch st(i) swaps two physical slots. Rotating TOP would move every
    // register in between, which the CRT's digit loops depend on not happening.
    void swap(int i, int j) {
        int a = idx(i), b = idx(j);
        uint8_t t[10]; std::memcpy(t, st[a], 10);
        std::memcpy(st[a], st[b], 10); std::memcpy(st[b], t, 10);
        bool e = empty[a]; empty[a] = empty[b]; empty[b] = e;
    }
    bool is_empty(int i) const { return empty[idx(i)]; }
    // Round to an integer in the control word's rounding mode (RC bits 10-11).
    Wide round_int(Wide v) const {
        switch ((cw >> 10) & 3) {
        case 0: return std::nearbyint(v);  // to nearest, ties to even
        case 1: return std::floor(v);      // toward -inf
        case 2: return std::ceil(v);       // toward +inf
        default: return std::trunc(v);     // toward zero
        }
    }
    // FCOM/FUCOM: C0/C2/C3 (unordered sets all three).
    void compare(Wide a, Wide b) {
        sw &= ~uint16_t(0x4500);  // C0 (0x100), C2 (0x400), C3 (0x4000)
        if (std::isnan(a) || std::isnan(b)) { sw |= 0x4500; return; }
        if (a > b) return;                       // greater: all clear
        if (a < b) { sw |= 0x0100; return; }     // C0
        sw |= 0x4000;                            // C3: equal
    }
    uint16_t status() const { return uint16_t((sw & ~uint16_t(0x3800)) | (uint16_t(top) << 11)); }
};

// SSE registers. Exact on every host: the bytes are the register.
struct Sse {
    uint32_t r[8][4];
    Sse() { std::memset(r, 0, sizeof r); }
    float fs(int i) const { float f; std::memcpy(&f, &r[i][0], 4); return f; }
    void setfs(int i, float f) { std::memcpy(&r[i][0], &f, 4); }
    double fd(int i) const { double d; std::memcpy(&d, &r[i][0], 8); return d; }
    void setfd(int i, double d) { std::memcpy(&r[i][0], &d, 8); }
    uint32_t u32(int i) const { return r[i][0]; }
    void setu32(int i, uint32_t v) { r[i][0] = v; }
    uint64_t u64(int i) const { uint64_t v; std::memcpy(&v, &r[i][0], 8); return v; }
    void setu64(int i, uint64_t v) { std::memcpy(&r[i][0], &v, 8); }
};
}
