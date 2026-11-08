// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose-built 32-bit x86 integer interpreter for the Play PE32 lane. It is a
// bounded subset (what freestanding/-O1 i686 code and CRT startup paths need
// first): anything outside it raises an explicit Fault naming the opcode bytes,
// so a milestone subset is never mistaken for general x86 support. There is no
// x87/MMX/SSE, no segmentation beyond FS/GS bases, and no interrupts.
// Calls into the host leave emulation at a "thunk" stub (see ThunkTable): the
// interpreter notices EIP inside the thunk page before fetching, runs the host
// handler against the guest stack, then performs the callee-side return.
#pragma once
#include <functional>
#include <map>
#include <string>
#include "x86_mem.h"
namespace x86 {
enum Reg { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };
enum Flag : uint32_t { CF = 1, PF = 4, AF = 0x10, ZF = 0x40, SF = 0x80, DF = 0x400, OF = 0x800 };
enum class Conv { Stdcall, Cdecl };
struct Cpu;
struct Thunk {
    std::string dll, name; Conv conv; unsigned argc;  // argc = dwords popped by a stdcall return
    std::function<uint32_t(Cpu&)> fn;                 // return value goes to EAX
};
struct ThunkTable {
    uint32_t base = 0x7FFE0000, stride = 16;
    std::vector<Thunk> entries;
    uint32_t add(Thunk t) { entries.push_back(std::move(t)); return base + uint32_t(entries.size() - 1) * stride; }
    uint32_t span() const { return uint32_t(entries.size() + 1) * stride; }  // +1: process-return sentinel
    uint32_t sentinel() const { return base + uint32_t(entries.size()) * stride; }
    const Thunk* at(uint32_t eip) const {
        if (eip < base || eip >= base + uint32_t(entries.size()) * stride || (eip - base) % stride) return nullptr;
        return &entries[(eip - base) / stride];
    }
};
struct Cpu {
    Memory& mem; ThunkTable& thunks;
    uint32_t reg[8] = {}, eip = 0, eflags = 2, fs_base = 0, gs_base = 0;
    uint64_t executed = 0, limit = 500ull * 1000 * 1000;
    bool exited = false; uint32_t exit_code = 0;
    unsigned depth = 0;
    Cpu(Memory& m, ThunkTable& t) : mem(m), thunks(t) {}

    // --- host-facing helpers (thunk handlers) ---
    uint32_t arg(unsigned i) const { return mem.r32(reg[ESP] + 4 + 4 * i); }  // valid inside a thunk handler
    void push(uint32_t v) { reg[ESP] -= 4; mem.w32(reg[ESP], v); }
    uint32_t pop() { uint32_t v = mem.r32(reg[ESP]); reg[ESP] += 4; return v; }
    // Host -> guest call (callbacks: WndProc, qsort comparator, TLS callbacks, DllMain...). Re-entrant.
    // Callee-saved registers and ESP are restored; EAX is the result either calling convention.
    uint32_t call(uint32_t fn, std::initializer_list<uint32_t> args) {
        if (exited) return 0;
        if (depth >= 64) throw Fault(FaultKind::Unsupported, eip, fn, "guest callback nesting too deep");
        uint32_t saved_eip = eip, saved_esp = reg[ESP], keep[8]; memcpy(keep, reg, sizeof keep);
        for (auto it = args.end(); it != args.begin();) push(*--it);
        push(thunks.sentinel()); eip = fn; ++depth;
        try { run(thunks.sentinel()); } catch (...) { --depth; throw; }
        --depth; uint32_t r = reg[EAX];
        for (int i : {EBX, EBP, ESI, EDI}) reg[i] = keep[i];
        reg[ESP] = saved_esp; eip = saved_eip; reg[ECX] = keep[ECX]; reg[EDX] = keep[EDX];
        return r;
    }
    void run(uint32_t stop) {
        while (!exited && eip != stop) step();
    }

    // --- interpreter ---
    uint32_t mask(unsigned n) const { return n == 4 ? 0xffffffffu : n == 2 ? 0xffffu : 0xffu; }
    uint32_t msb(unsigned n) const { return 1u << (8 * n - 1); }
    uint32_t sx(uint32_t v, unsigned n) const { return n == 4 ? v : n == 2 ? uint32_t(int32_t(int16_t(v))) : uint32_t(int32_t(int8_t(v))); }
    void setf(uint32_t f, bool on) { eflags = on ? (eflags | f) : (eflags & ~f); }
    bool f(uint32_t m) const { return (eflags & m) != 0; }
    void szp(uint32_t r, unsigned n) {
        r &= mask(n); setf(ZF, !r); setf(SF, r & msb(n));
        uint8_t b = uint8_t(r); b ^= b >> 4; b ^= b >> 2; b ^= b >> 1; setf(PF, !(b & 1));
    }
    uint32_t getreg(unsigned i, unsigned n) const {
        if (n == 1) return i < 4 ? (reg[i] & 0xff) : ((reg[i - 4] >> 8) & 0xff);
        return reg[i] & mask(n);
    }
    void setreg(unsigned i, unsigned n, uint32_t v) {
        if (n == 4) reg[i] = v;
        else if (n == 2) reg[i] = (reg[i] & 0xffff0000u) | (v & 0xffff);
        else if (i < 4) reg[i] = (reg[i] & ~0xffu) | (v & 0xff);
        else reg[i - 4] = (reg[i - 4] & ~0xff00u) | ((v & 0xff) << 8);
    }
    uint32_t alu(unsigned op, uint32_t a, uint32_t b, unsigned n) {
        uint32_t m = mask(n), c = (op == 2 || op == 3) && f(CF) ? 1 : 0; uint64_t r;
        a &= m; b &= m;
        switch (op) {
        case 0: case 2: r = uint64_t(a) + b + c; setf(CF, r >> (8 * n) & 1); setf(OF, (a ^ r) & (b ^ r) & msb(n)); setf(AF, (a ^ b ^ r) & 0x10); break;
        case 3: case 5: case 7: r = uint64_t(a) - b - c; setf(CF, uint64_t(b) + c > a); setf(OF, (a ^ b) & (a ^ r) & msb(n)); setf(AF, (a ^ b ^ r) & 0x10); break;
        case 1: r = a | b; setf(CF, 0); setf(OF, 0); setf(AF, 0); break;
        case 4: r = a & b; setf(CF, 0); setf(OF, 0); setf(AF, 0); break;
        default: r = a ^ b; setf(CF, 0); setf(OF, 0); setf(AF, 0); break;
        }
        szp(uint32_t(r), n); return uint32_t(r) & m;
    }
    uint32_t incdec(bool dec, uint32_t a, unsigned n) {
        bool cf = f(CF); uint32_t r = alu(dec ? 5 : 0, a, 1, n); setf(CF, cf); return r;
    }
    uint32_t shift(unsigned op, uint32_t a, unsigned cnt, unsigned n) {
        cnt &= 31; if (!cnt) return a & mask(n);
        unsigned bits = 8 * n; uint32_t m = mask(n); a &= m; uint32_t r; bool cf;
        switch (op) {
        case 0: { unsigned k = cnt % bits; r = k ? ((a << k) | (a >> (bits - k))) & m : a; cf = r & 1; setf(CF, cf); if (cnt == 1) setf(OF, bool(r & msb(n)) != cf); return r; }
        case 1: { unsigned k = cnt % bits; r = k ? ((a >> k) | (a << (bits - k))) & m : a; cf = r & msb(n); setf(CF, cf); if (cnt == 1) setf(OF, bool(r & msb(n)) != bool(r & (msb(n) >> 1))); return r; }
        case 4: case 6: { uint64_t t = uint64_t(a) << cnt; r = uint32_t(t) & m; setf(CF, t >> bits & 1); setf(OF, bool(r & msb(n)) != bool(t >> bits & 1)); break; }
        case 5: r = a >> cnt; setf(CF, (a >> (cnt - 1)) & 1); setf(OF, a & msb(n)); break;
        case 7: { int32_t s = int32_t(sx(a, n)); r = uint32_t(s >> cnt) & m; setf(CF, (uint32_t(s) >> (cnt - 1)) & 1); setf(OF, 0); break; }
        default: unsupported("rcl/rcr");
        }
        szp(r, n); setf(AF, 0); return r;
    }
    bool cond(unsigned c) const {
        bool r;
        switch (c >> 1) {
        case 0: r = f(OF); break; case 1: r = f(CF); break; case 2: r = f(ZF); break; case 3: r = f(CF) || f(ZF); break;
        case 4: r = f(SF); break; case 5: r = f(PF); break; case 6: r = f(SF) != f(OF); break;
        default: r = f(ZF) || (f(SF) != f(OF)); break;
        }
        return c & 1 ? !r : r;
    }

    // Instruction decode state (valid for the instruction being executed).
    uint32_t start = 0, seg = 0; unsigned osz = 4, rep = 0;
    uint8_t fetch() { mem.fault_eip = start; return mem.fetch8(eip++); }
    uint32_t fetchn(unsigned n) { uint32_t v = 0; for (unsigned i = 0; i < n; ++i) v |= uint32_t(fetch()) << (8 * i); return v; }
    [[noreturn]] void unsupported(const char* what) {
        char b[160]; int k = snprintf(b, sizeof b, "unsupported x86 instruction (%s) at 0x%08x:", what, start);
        for (unsigned i = 0; i < 6 && k < int(sizeof b) - 4; ++i) { try { k += snprintf(b + k, sizeof b - k, " %02x", mem.fetch8(start + i)); } catch (...) { break; } }
        throw Fault(FaultKind::Unsupported, start, 0, b);
    }
    struct Rm { bool isreg; uint32_t v; unsigned regno; uint32_t ea; };
    uint32_t modrm_reg = 0;
    Rm modrm() {
        uint8_t m = fetch(); unsigned mod = m >> 6, rm = m & 7; modrm_reg = (m >> 3) & 7;
        if (mod == 3) return {true, 0, rm, 0};
        uint32_t ea = 0;
        if (rm == 4) {
            uint8_t sib = fetch(); unsigned sc = sib >> 6, idx = (sib >> 3) & 7, base = sib & 7;
            if (idx != 4) ea += reg[idx] << sc;
            if (base == 5 && mod == 0) ea += fetchn(4); else ea += reg[base];
        } else if (rm == 5 && mod == 0) ea = fetchn(4);
        else ea = reg[rm];
        if (mod == 1) ea += uint32_t(int32_t(int8_t(fetch())));
        else if (mod == 2) ea += fetchn(4);
        return {false, 0, 0, ea};
    }
    uint32_t rd(const Rm& r, unsigned n) { return r.isreg ? getreg(r.regno, n) : mem.r(r.ea + seg, n); }
    void wr(const Rm& r, unsigned n, uint32_t v) { if (r.isreg) setreg(r.regno, n, v); else mem.w(r.ea + seg, v, n); }

    void step() {
        if (++executed > limit) throw Fault(FaultKind::InstructionLimit, eip, 0, "instruction limit exceeded");
        if (const Thunk* t = thunks.at(eip)) { host_call(*t); return; }
        if (eip >= thunks.base && eip < thunks.base + thunks.span()) { mem.fault_eip = eip; throw Fault(FaultKind::BadThunk, eip, eip, "execution inside thunk page off a stub boundary"); }
        start = eip; seg = 0; osz = 4; rep = 0; mem.fault_eip = eip;
        uint8_t op;
        for (;;) {  // prefixes
            op = fetch();
            if (op == 0x66) osz = 2; else if (op == 0xF3 || op == 0xF2) rep = op;
            else if (op == 0x64) seg = fs_base; else if (op == 0x65) seg = gs_base;
            else if (op == 0x2E || op == 0x36 || op == 0x3E || op == 0x26) {}
            else break;
        }
        unsigned n = osz;
        if (op < 0x40 && (op & 7) < 6) {  // 00-3F ALU forms
            unsigned aop = op >> 3, form = op & 7;
            if (form < 4) {
                unsigned sz = (form & 1) ? n : 1; Rm r = modrm(); uint32_t a, b;
                if (form < 2) { a = rd(r, sz); b = getreg(modrm_reg, sz); uint32_t v = alu(aop, a, b, sz); if (aop != 7) wr(r, sz, v); }
                else { a = getreg(modrm_reg, sz); b = rd(r, sz); uint32_t v = alu(aop, a, b, sz); if (aop != 7) setreg(modrm_reg, sz, v); }
            } else {
                unsigned sz = form == 4 ? 1 : n; uint32_t imm = fetchn(sz); uint32_t v = alu(aop, getreg(0, sz), imm, sz);
                if (aop != 7) setreg(0, sz, v);
            }
            return;
        }
        switch (op) {
        case 0x0F: step0f(); return;
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
            setreg(op & 7, n, incdec(false, getreg(op & 7, n), n)); return;
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
            setreg(op & 7, n, incdec(true, getreg(op & 7, n), n)); return;
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            if (n != 4) unsupported("16-bit push");
            push(reg[op & 7]); return;
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            if (n != 4) unsupported("16-bit pop");
            { uint32_t v = pop(); reg[op & 7] = v; } return;
        case 0x68: push(fetchn(4)); return;
        case 0x6A: push(sx(fetch(), 1)); return;
        case 0x69: case 0x6B: {
            Rm r = modrm(); uint32_t a = rd(r, n), b = op == 0x69 ? fetchn(n == 2 ? 2 : 4) : sx(fetch(), 1);
            setreg(modrm_reg, n, imul2(a, b, n)); return; }
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
            int32_t d = int8_t(fetch()); if (cond(op & 15)) eip += d; return; }
        case 0x80: case 0x81: case 0x82: case 0x83: {
            unsigned sz = (op & 1) ? n : 1; Rm r = modrm(); unsigned aop = modrm_reg;
            uint32_t imm = op == 0x83 ? sx(fetch(), 1) : fetchn(sz);
            uint32_t v = alu(aop, rd(r, sz), imm, sz); if (aop != 7) wr(r, sz, v); return; }
        case 0x84: case 0x85: { unsigned sz = (op & 1) ? n : 1; Rm r = modrm(); alu(4, rd(r, sz), getreg(modrm_reg, sz), sz); return; }
        case 0x86: case 0x87: { unsigned sz = (op & 1) ? n : 1; Rm r = modrm(); uint32_t a = rd(r, sz), b = getreg(modrm_reg, sz); wr(r, sz, b); setreg(modrm_reg, sz, a); return; }
        case 0x88: case 0x89: { unsigned sz = (op & 1) ? n : 1; Rm r = modrm(); wr(r, sz, getreg(modrm_reg, sz)); return; }
        case 0x8A: case 0x8B: { unsigned sz = (op & 1) ? n : 1; Rm r = modrm(); setreg(modrm_reg, sz, rd(r, sz)); return; }
        case 0x8D: { Rm r = modrm(); if (r.isreg) unsupported("lea reg"); setreg(modrm_reg, n, r.ea); return; }
        case 0x8F: { Rm r = modrm(); if (modrm_reg) unsupported("8f /n"); uint32_t v = pop(); wr(r, 4, v); return; }
        case 0x90: return;  // nop / pause
        case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: { uint32_t a = getreg(0, n), b = getreg(op & 7, n); setreg(0, n, b); setreg(op & 7, n, a); return; }
        case 0x98: if (n == 4) reg[EAX] = sx(reg[EAX], 2); else setreg(0, 2, sx(reg[EAX], 1)); return;
        case 0x99: if (n == 4) reg[EDX] = int32_t(reg[EAX]) < 0 ? 0xffffffffu : 0; else setreg(2, 2, int16_t(reg[EAX]) < 0 ? 0xffff : 0); return;
        case 0xA0: case 0xA1: { unsigned sz = (op & 1) ? n : 1; setreg(0, sz, mem.r(fetchn(4) + seg, sz)); return; }
        case 0xA2: case 0xA3: { unsigned sz = (op & 1) ? n : 1; mem.w(fetchn(4) + seg, getreg(0, sz), sz); return; }
        case 0xA4: case 0xA5: case 0xAA: case 0xAB: case 0xAC: case 0xAD: stringop(op, (op & 1) ? n : 1); return;
        case 0xA8: case 0xA9: { unsigned sz = (op & 1) ? n : 1; alu(4, getreg(0, sz), fetchn(sz), sz); return; }
        case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7: setreg(op & 7, 1, fetch()); return;
        case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF: setreg(op & 7, n, fetchn(n)); return;
        case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
            unsigned sz = (op & 1) ? n : 1; Rm r = modrm(); unsigned sop = modrm_reg;
            unsigned cnt = op < 0xD0 ? fetch() : op < 0xD2 ? 1 : reg[ECX] & 0xff;
            wr(r, sz, shift(sop, rd(r, sz), cnt, sz)); return; }
        case 0xC2: { uint16_t pop_n = uint16_t(fetchn(2)); eip = pop(); reg[ESP] += pop_n; return; }
        case 0xC3: eip = pop(); return;
        case 0xC6: case 0xC7: { unsigned sz = (op & 1) ? n : 1; Rm r = modrm(); if (modrm_reg) unsupported("c6/c7 /n"); wr(r, sz, fetchn(sz)); return; }
        case 0xC9: reg[ESP] = reg[EBP]; reg[EBP] = pop(); return;
        case 0xCC: throw Fault(FaultKind::Breakpoint, start, start, "int3 breakpoint");
        case 0xE8: { int32_t d = int32_t(fetchn(4)); push(eip); eip += d; return; }
        case 0xE9: { int32_t d = int32_t(fetchn(4)); eip += d; return; }
        case 0xEB: { int32_t d = int8_t(fetch()); eip += d; return; }
        case 0xF4: unsupported("hlt");
        case 0xF8: setf(CF, 0); return; case 0xF9: setf(CF, 1); return;
        case 0xFC: setf(DF, 0); return; case 0xFD: setf(DF, 1); return;
        case 0xF6: case 0xF7: group3((op & 1) ? n : 1); return;
        case 0xFE: case 0xFF: group45(op, n); return;
        default: unsupported("opcode");
        }
    }
    uint32_t imul2(uint32_t a, uint32_t b, unsigned n) {
        int64_t p = int64_t(int32_t(sx(a, n))) * int32_t(sx(b, n)); uint32_t r = uint32_t(p) & mask(n);
        bool ov = p != int64_t(int32_t(sx(r, n))); setf(CF, ov); setf(OF, ov); szp(r, n); setf(AF, 0); return r;
    }
    void stringop(uint8_t op, unsigned sz) {
        int step = f(DF) ? -int(sz) : int(sz);
        bool repeat = rep == 0xF3;
        if (rep == 0xF2) unsupported("repne string op");
        do {
            if (repeat && !reg[ECX]) break;
            if (op <= 0xA5) { mem.w(reg[EDI], mem.r(reg[ESI] + seg, sz), sz); reg[ESI] += step; reg[EDI] += step; }
            else if (op <= 0xAB) { mem.w(reg[EDI], getreg(0, sz), sz); reg[EDI] += step; }
            else { setreg(0, sz, mem.r(reg[ESI] + seg, sz)); reg[ESI] += step; }
            if (repeat) --reg[ECX];
        } while (repeat && reg[ECX]);
    }
    void group3(unsigned sz) {
        Rm r = modrm(); unsigned sop = modrm_reg; uint32_t v = rd(r, sz);
        switch (sop) {
        case 0: case 1: alu(4, v, fetchn(sz), sz); return;
        case 2: wr(r, sz, ~v & mask(sz)); return;
        case 3: { uint32_t res = alu(5, 0, v, sz); setf(CF, v != 0); wr(r, sz, res); return; }
        case 4: case 5: {
            bool sg = sop == 5; uint64_t p; uint32_t lo, hi;
            if (sg) p = uint64_t(int64_t(int32_t(sx(getreg(0, sz), sz))) * int32_t(sx(v, sz)));
            else p = uint64_t(getreg(0, sz)) * v;
            lo = uint32_t(p) & mask(sz); hi = uint32_t(p >> (8 * sz)) & mask(sz);
            if (sz == 1) setreg(0, 2, uint32_t(p)); else { setreg(0, sz, lo); setreg(2, sz, hi); }
            bool ov = sg ? int64_t(p) != int64_t(int32_t(sx(lo, sz))) : hi != 0;
            setf(CF, ov); setf(OF, ov); return; }
        default: {
            bool sg = sop == 7; uint32_t d = v & mask(sz); if (!d) divfault();
            uint64_t num = sz == 1 ? getreg(0, 2) : sz == 2 ? (uint64_t(getreg(2, 2)) << 16 | getreg(0, 2)) : (uint64_t(reg[EDX]) << 32 | reg[EAX]);
            uint64_t q, rem; unsigned bits = 8 * sz;
            if (sg) {
                int64_t sn = sz == 1 ? int16_t(num) : sz == 2 ? int32_t(num) : int64_t(num), sd = int32_t(sx(d, sz));
                if (sn == INT64_MIN && sd == -1) divfault();
                int64_t sq = sn / sd, sr = sn % sd; int64_t lim = int64_t(1) << (bits - 1);
                if (sq >= lim || sq < -lim) divfault();
                q = uint64_t(sq); rem = uint64_t(sr);
            } else { q = num / d; rem = num % d; if (q >> bits) divfault(); }
            if (sz == 1) setreg(0, 2, uint32_t((rem & 0xff) << 8 | (q & 0xff)));
            else { setreg(0, sz, uint32_t(q)); setreg(2, sz, uint32_t(rem)); }
            return; }
        }
    }
    [[noreturn]] void divfault() { throw Fault(FaultKind::DivideError, start, 0, "divide error"); }
    void group45(uint8_t op, unsigned n) {
        unsigned sz = (op & 1) ? n : 1; Rm r = modrm(); unsigned sop = modrm_reg;
        if (sop < 2) { wr(r, sz, incdec(sop == 1, rd(r, sz), sz)); return; }
        if (op == 0xFE) unsupported("fe /n");
        switch (sop) {
        case 2: { uint32_t t = rd(r, 4); push(eip); eip = t; return; }
        case 4: eip = rd(r, 4); return;
        case 6: { uint32_t v = rd(r, 4); push(v); return; }
        default: unsupported("ff far/ill");
        }
    }
    void step0f() {
        unsigned n = osz; uint8_t op = fetch();
        if (op >= 0x80 && op <= 0x8F) { int32_t d = int32_t(fetchn(4)); if (cond(op & 15)) eip += d; return; }
        if (op >= 0x90 && op <= 0x9F) { Rm r = modrm(); wr(r, 1, cond(op & 15)); return; }
        if (op >= 0x40 && op <= 0x4F) { Rm r = modrm(); uint32_t v = rd(r, n); if (cond(op & 15)) setreg(modrm_reg, n, v); return; }
        switch (op) {
        case 0x0B: throw Fault(FaultKind::InvalidOpcode, start, 0, "ud2");
        case 0x1F: { modrm(); return; }
        case 0xAF: { Rm r = modrm(); setreg(modrm_reg, n, imul2(getreg(modrm_reg, n), rd(r, n), n)); return; }
        case 0xB6: case 0xB7: { unsigned s = op == 0xB6 ? 1 : 2; Rm r = modrm(); setreg(modrm_reg, n, rd(r, s)); return; }
        case 0xBE: case 0xBF: { unsigned s = op == 0xBE ? 1 : 2; Rm r = modrm(); setreg(modrm_reg, n, sx(rd(r, s), s) & mask(n)); return; }
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF: {
            uint32_t v = reg[op & 7]; reg[op & 7] = v >> 24 | (v >> 8 & 0xff00) | (v << 8 & 0xff0000) | v << 24; return; }
        default: unsupported("0f opcode");
        }
    }
    void host_call(const Thunk& t) {
        mem.fault_eip = eip;
        uint32_t ret = mem.r32(reg[ESP]);
        uint32_t r = t.fn(*this);
        if (exited) return;
        reg[EAX] = r;
        reg[ESP] += 4 + (t.conv == Conv::Stdcall ? 4 * t.argc : 0); eip = ret;
    }
};
}
