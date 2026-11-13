// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose-built 32-bit x86 interpreter for the Play PE32 lane. It is a bounded
// subset (what MSVC/mingw-built i686 code and CRT startup paths need): anything
// outside it raises an explicit Fault naming the opcode bytes, so a milestone
// subset is never mistaken for general x86 support. Segmentation is limited to
// FS/GS bases; there are no interrupts and no MMX. x87 and SSE/SSE2 are present
// (stage 2); x87 arithmetic is bit-exact only where long double is the x87
// extended type (see x86_fpu.h). Calls into the host leave emulation at a
// "thunk" stub (see ThunkTable): the interpreter notices EIP inside the thunk
// page before fetching, runs the host handler against the guest stack, then
// performs the callee-side return.
#pragma once
#include <functional>
#include <map>
#include <string>
#include "x86_fpu.h"
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
    uint32_t capacity = 0;  // stubs the mapped page can hold (0 = not mapped yet)
    std::vector<Thunk> entries;
    uint32_t add(Thunk t) {
        if (capacity && entries.size() >= capacity) throw std::runtime_error("x86 thunk page full");
        entries.push_back(std::move(t));
        return base + uint32_t(entries.size() - 1) * stride;
    }
    // The process-return sentinel is the last slot of the mapped page and never
    // moves: GetProcAddress can add stubs while a host->guest call is running,
    // and a sentinel derived from entries.size() would then land on the address
    // that call is waiting to return to.
    uint32_t sentinel() const { return capacity ? base + (capacity - 1) * stride : base + uint32_t(entries.size()) * stride; }
    uint32_t span() const { return uint32_t(entries.size() + 1) * stride; }
    const Thunk* at(uint32_t eip) const {
        if (eip < base || eip >= base + uint32_t(entries.size()) * stride || (eip - base) % stride) return nullptr;
        return &entries[(eip - base) / stride];
    }
};
struct Cpu {
    Memory& mem; ThunkTable& thunks;
    uint32_t reg[8] = {}, eip = 0, eflags = 2, fs_base = 0, gs_base = 0;
    uint64_t executed = 0, limit = 500ull * 1000 * 1000, tsc = 0;
    bool exited = false; uint32_t exit_code = 0;
    unsigned depth = 0;
    Fpu fpu; Sse xmm; uint32_t mxcsr = 0x1f80;
    // MXCSR_MASK (fxsave +28) is CPU-specific silicon state, not an architectural
    // constant: e.g. this dev machine's CPU reads 0x0000ffff while a Zen runner
    // reads 0x0002ffff (bit 17 = FIZ-Z, writable). Probe the host's real mask
    // once with a genuine fxsave so the emulator reports what this CPU reports.
    uint32_t mxcsr_mask = [] {
        alignas(16) uint8_t buf[512] = {};
        asm volatile("fxsave %0" :: "m"(buf) : "memory");
        uint32_t mask; std::memcpy(&mask, buf + 28, 4);
        return mask ? mask : 0x0000ffffu;  // pre-SSE2 CPUs may leave it 0
    }();
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
    uint32_t start = 0, seg = 0; unsigned osz = 4, rep = 0; bool lock = false, p66 = false;
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
        start = eip; seg = 0; osz = 4; rep = 0; lock = false; p66 = false; mem.fault_eip = eip;
        uint8_t op;
        for (;;) {  // prefixes
            op = fetch();
            if (op == 0x66) { osz = 2; p66 = true; } else if (op == 0xF3 || op == 0xF2) rep = op;
            else if (op == 0x64) seg = fs_base; else if (op == 0x65) seg = gs_base;
            else if (op == 0xF0) lock = true;
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
        case 0x60: {  // pushad
            if (n != 4) unsupported("pushad 16-bit");
            uint32_t sp = reg[ESP];
            for (int i : {EAX, ECX, EDX, EBX}) push(reg[i]);
            push(sp); for (int i : {EBP, ESI, EDI}) push(reg[i]); return; }
        case 0x61: {  // popad
            if (n != 4) unsupported("popad 16-bit");
            for (int i : {EDI, ESI, EBP}) reg[i] = pop();
            pop(); for (int i : {EBX, EDX, ECX, EAX}) reg[i] = pop(); return; }
        case 0x62: unsupported("bound");
        case 0x63: unsupported("arpl");
        case 0x6C: case 0x6D: case 0x6E: case 0x6F: unsupported("ins/outs");
        case 0x9B: return;  // fwait: the interpreter is synchronous
        case 0x9C: {  // pushfd/pushfw
            uint32_t v = eflags | 2u; if (n == 2) push(v & 0xffff); else push(v); return; }
        case 0x9D: {  // popfd/popfw
            uint32_t v = n == 2 ? (pop() & 0xffff) : pop();
            eflags = (eflags & ~0x0cd5u) | (v & 0x0cd5u) | 2u; return; }
        case 0x9E: {  // sahf
            uint32_t ah = (reg[EAX] >> 8) & 0xff;
            eflags = (eflags & ~0xd5u) | (ah & 0xd5u) | 2u; return; }
        case 0x9F: reg[EAX] = (reg[EAX] & 0xffff00ffu) | ((eflags & 0xd5u) << 8); return;  // lahf
        case 0xC8: {  // enter imm16, imm8
            if (n != 4) unsupported("enter 16-bit");
            uint16_t alloc = uint16_t(fetchn(2)); unsigned level = fetch() & 31;
            push(reg[EBP]); uint32_t frame = reg[ESP];
            if (level > 0) for (unsigned i = 1; i < level; ++i) { reg[EBP] -= 4; push(mem.r32(reg[EBP])); }
            if (level > 0) push(frame);
            reg[EBP] = frame; reg[ESP] -= alloc; return; }
        case 0xCA: { uint16_t pop_n = uint16_t(fetchn(2)); eip = pop(); reg[ESP] += pop_n; return; }
        case 0xCB: eip = pop(); return;  // retf: flat model, same as ret
        case 0xCD: unsupported("int imm8");
        case 0xCE: unsupported("into");
        case 0xCF: unsupported("iret");
        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
            x87(op); return;
        case 0xE0: case 0xE1: case 0xE2: {  // loopne/loope/loop rel8
            int32_t d = int8_t(fetch()); --reg[ECX];
            bool take = op == 0xE2 ? reg[ECX] != 0 : op == 0xE1 ? (reg[ECX] != 0 && f(ZF)) : (reg[ECX] != 0 && !f(ZF));
            if (take) eip += d;
            return; }
        case 0xE3: { int32_t d = int8_t(fetch()); if (!reg[ECX]) eip += d; return; }  // jecxz
        case 0xE4: case 0xE5: case 0xE6: case 0xE7: case 0xEC: case 0xED: case 0xEE: case 0xEF:
            unsupported("in/out");
        case 0xF5: setf(CF, !f(CF)); return;  // cmc
        case 0xFA: case 0xFB: return;  // cli/sti: no interrupts in the interpreter
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
        case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
            stringop(op, (op & 1) ? n : 1); return;
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
        bool repeat = rep != 0;
        bool repne = rep == 0xF2;
        do {
            if (repeat && !reg[ECX]) break;
            if (op <= 0xA7) {  // movs copies; cmps compares and sets flags only
                // A segment override applies to DS:ESI only; ES:EDI is fixed.
                uint32_t a = mem.r(reg[ESI] + seg, sz), b = mem.r(reg[EDI], sz);
                if (op <= 0xA5) mem.w(reg[EDI], a, sz);
                else alu(7, a, b, sz);  // cmps: sub for flags only
                reg[ESI] += step; reg[EDI] += step;
            } else if (op <= 0xAB) { mem.w(reg[EDI], getreg(0, sz), sz); reg[EDI] += step; }
            else if (op <= 0xAD) { setreg(0, sz, mem.r(reg[ESI] + seg, sz)); reg[ESI] += step; }
            else {  // scas: cmp al/ax/eax against [edi] (ES-based, no override)
                uint32_t b = mem.r(reg[EDI], sz);
                alu(7, getreg(0, sz), b, sz);
                reg[EDI] += step;
            }
            if (repeat) --reg[ECX];
        } while (repeat && reg[ECX] &&
                 (((op >= 0xA6 && op <= 0xA7) || (op >= 0xAE && op <= 0xAF)) ? !(repne ? f(ZF) : !f(ZF)) : true));
        // rep movs/stos/lods run to ECX=0; only cmps/scas set ZF and stop early
        // (repe on ZF=1, repne on ZF=0). The old code only ran F3 movs-class ops.
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
        case 0xBC: case 0xBD: {  // bsf/bsr (and tzcnt/lzcnt under the F3 prefix)
            Rm r = modrm(); uint32_t v = rd(r, n) & mask(n);
            bool f3 = rep == 0xF3;
            if (!v) {
                if (f3) setreg(modrm_reg, n, n == 2 ? 16 : 32);  // tzcnt/lzcnt: no zero flag
                else { setf(ZF, 1); setreg(modrm_reg, n, 0); }
                return;
            }
            unsigned bits = 8 * n, i = 0;
            if (op == 0xBC) { while (!((v >> i) & 1)) ++i; } else { i = bits - 1; while (!((v >> i) & 1)) --i; }
            setreg(modrm_reg, n, i); setf(ZF, 0); return; }
        case 0xA3: { Rm r = modrm(); bt(r, modrm_reg, n, 0); return; }
        case 0xAB: { Rm r = modrm(); bt(r, modrm_reg, n, 1); return; }
        case 0xB3: { Rm r = modrm(); bt(r, modrm_reg, n, 2); return; }
        case 0xBB: { Rm r = modrm(); bt(r, modrm_reg, n, 3); return; }
        case 0xA4: case 0xA5: {  // shld r/m, r, imm8/cl
            Rm r = modrm(); unsigned cnt = op == 0xA4 ? fetch() : (reg[ECX] & 31); shld(r, modrm_reg, n, cnt, false); return; }
        case 0xAC: case 0xAD: {  // shrd r/m, r, imm8/cl
            Rm r = modrm(); unsigned cnt = op == 0xAC ? fetch() : (reg[ECX] & 31); shld(r, modrm_reg, n, cnt, true); return; }
        case 0xBA: {  // bt/bts/btr/btc r/m, imm8
            // The bit index is the encoded immediate, not a register: bt() reads
            // a register for the other form, so the immediate gets its own path.
            Rm r = modrm(); uint32_t bit = fetch(); bt_imm(r, bit, n, modrm_reg & 3); return; }
        case 0xB0: case 0xB1: {  // cmpxchg r/m, r
            unsigned sz = (op & 1) ? n : 1; Rm r = modrm();
            uint32_t dst = rd(r, sz), src = getreg(modrm_reg, sz);
            uint32_t res = alu(5, getreg(0, sz), dst, sz);  // cmp dst with AL/AX/EAX
            if (f(ZF)) wr(r, sz, src); else setreg(0, sz, dst);
            (void)res; return; }
        case 0xC0: case 0xC1: {  // xadd r/m, r
            unsigned sz = (op & 1) ? n : 1; Rm r = modrm();
            uint32_t dst = rd(r, sz), src = getreg(modrm_reg, sz);
            wr(r, sz, alu(0, dst, src, sz)); setreg(modrm_reg, sz, dst); return; }
        case 0xC7: {  // group 9: cmpxchg8b (and rdrand/rdseed under its prefixes)
            Rm r = modrm();
            if (modrm_reg == 1) {
                if (r.isreg) unsupported("cmpxchg8b reg");
                uint64_t memv = mem.r64(r.ea + seg), cmp = uint64_t(reg[EDX]) << 32 | reg[EAX];
                if (memv == cmp) { setf(ZF, 1); mem.w64(r.ea + seg, uint64_t(reg[ECX]) << 32 | reg[EBX]); }
                else { setf(ZF, 0); reg[EDX] = uint32_t(memv >> 32); reg[EAX] = uint32_t(memv); }
                return;
            }
            unsupported("0f c7 /n"); }
        case 0xAE: {  // group 15: fences, fxsave/fxrstor, ldmxcsr/stmxcsr
            Rm r = modrm();
            switch (modrm_reg) {
            case 0: {  // fxsave: the real 512-byte layout (x87 env, state, xmm) games snapshot
                if (r.isreg) unsupported("fxsave reg");
                uint32_t ea = r.ea + seg;
                mem.w(ea, fpu.cw, 2); mem.w(ea + 2, fpu.status(), 2);
                uint8_t ftw = 0;  // abridged tag word: bit i = 1 if PHYSICAL slot i is non-empty
                for (int i = 0; i < 8; ++i) if (!fpu.empty[i]) ftw |= uint8_t(1u << i);
                mem.w8(ea + 4, ftw); mem.w8(ea + 5, 0);       // +5..7 reserved
                mem.w(ea + 6, 0, 2);                          // fop: no real 8087-style tracking
                mem.w(ea + 8, 0, 8);                          // fip: documented zeros
                mem.w(ea + 16, 0, 8);                         // fdp (with cs/ds halves): documented zeros
                mem.w(ea + 24, mxcsr, 4); mem.w(ea + 28, mxcsr_mask, 4);  // mxcsr + the host CPU's real mask (probed with a genuine fxsave; silicon-specific)
                for (int i = 0; i < 8; ++i) {  // ST(i) at 32 + 16*i: 10 bytes + 6 reserved.
                    // ST(i) is relative to TOP: physical slot (top+i)&7.
                    int ph = (fpu.top + i) & 7;
                    for (int k = 0; k < 10; ++k) mem.w8(ea + 32 + 16 * i + k, fpu.st[ph][k]);
                    mem.w(ea + 32 + 16 * i + 10, 0, 6);
                }
                for (int i = 0; i < 8; ++i) mem.w128(ea + 160 + 16 * i, xmm.r[i]);
                return; }
            case 1: {  // fxrstor
                if (r.isreg) unsupported("fxrstor reg");
                uint32_t ea = r.ea + seg;
                fpu.cw = mem.r(ea, 2);
                fpu.sw = mem.r(ea + 2, 2) & ~uint16_t(0x3800);  // bits 11-13 are TOP, restored below
                fpu.top = (mem.r(ea + 2, 2) >> 11) & 7;
                uint8_t ftw = mem.r8(ea + 4);  // abridged: bit i = physical slot i non-empty; 0 -> empty (tag 11)
                for (int i = 0; i < 8; ++i) {  // STi relative to TOP (see fxsave)
                    int ph = (fpu.top + i) & 7;
                    for (int k = 0; k < 10; ++k) fpu.st[ph][k] = mem.r8(ea + 32 + 16 * i + k);
                    fpu.empty[ph] = (ftw >> ph) & 1 ? false : true;
                }
                mxcsr = mem.r(ea + 24, 4);
                for (int i = 0; i < 8; ++i) mem.r128(ea + 160 + 16 * i, xmm.r[i]);
                return; }
            case 5: case 6: return;  // xsave/xrstor: same treatment as before (not implemented)
            case 2: mxcsr = rd(r, 4); return;        // ldmxcsr
            case 3: wr(r, 4, mxcsr); return;         // stmxcsr: a save/restore pair must round-trip
            case 7: return;                          // clflush
            default: unsupported("0f ae /n"); }
        }
        case 0x10: case 0x11: sse_move(op); return;
        case 0x28: case 0x29: sse_move(op); return;
        case 0x12: case 0x13: case 0x16: case 0x17: sse_move(op); return;
        case 0x14: case 0x15: {  // unpcklps/unpckhps: dest and src interleaved (dest first)
            Rm r = modrm(); uint32_t a[4], b[4]; sse_read(r, a); sse_reg(modrm_reg, b);
            bool hi = op == 0x15; uint32_t out[4];
            for (int i = 0; i < 2; ++i) { out[i * 2] = b[i + (hi ? 2 : 0)]; out[i * 2 + 1] = a[i + (hi ? 2 : 0)]; }
            sse_write_reg(modrm_reg, out); return; }
        case 0x2B: { Rm r = modrm(); if (r.isreg) unsupported("movntps reg"); uint32_t v[4]; sse_reg(modrm_reg, v); mem.w128(r.ea + seg, v); return; }
        case 0x2E: case 0x2F: {  // ucomiss/comiss
            Rm r = modrm(); float a = xf32(modrm_reg), b = sse_f32(r);
            eflags = (eflags & ~(ZF | PF | CF | OF | SF | AF)) | 2u;
            if (std::isnan(a) || std::isnan(b)) { eflags |= ZF | PF | CF; return; }
            if (a > b) return;
            if (a < b) { eflags |= CF; return; }
            eflags |= ZF; return; }
        case 0x2A: {  // cvtsi2ss/cvtsi2sd
            Rm r = modrm();
            if (rep == 0xF3) { float v = n == 2 ? float(int16_t(rd(r, 2))) : float(int32_t(rd(r, 4))); xmm.setfs(modrm_reg, v); }
            else if (rep == 0xF2) { double v = n == 2 ? double(int16_t(rd(r, 2))) : double(int32_t(rd(r, 4))); xmm.setfd(modrm_reg, v); }
            else unsupported("cvtpi2ps (MMX)");
            return; }
        case 0x2C: case 0x2D: {  // cvttss2si/cvtss2si and the sd forms
            Rm r = modrm(); bool trunc = op == 0x2C;
            double v = rep == 0xF2 ? sse_f64(r) : double(sse_f32(r));
            double c = trunc ? std::trunc(v) : std::nearbyint(v);
            int64_t iv = int64_t(c);
            if (n == 2) setreg(modrm_reg, 2, uint32_t(uint16_t(int16_t(iv))));
            else setreg(modrm_reg, 4, uint32_t(int32_t(iv)));
            return; }
        case 0x6E: {  // movd/movq xmm, r/m (66 prefix; F3 0F 7E is the movq load)
            if (!p66) unsupported("movd (MMX)");
            // A load into an XMM register zeroes the rest of it (Intel: the
            // destination is the full 128 bits, not a partial write).
            Rm r = modrm();
            uint32_t v[4] = {rd(r, 4), 0, 0, 0};
            sse_write_reg(modrm_reg, v); return; }
        case 0x7E: {  // movd/movq r/m, xmm (66) or movq xmm, xmm/m64 (F3)
            Rm r = modrm();
            if (rep == 0xF3) { uint32_t v[4] = {0, 0, 0, 0}; uint64_t q = sse_rd64(r); v[0] = uint32_t(q); v[1] = uint32_t(q >> 32); sse_write_reg(modrm_reg, v); return; }
            // 66 0F 7E with a register operand is a GP destination (movd r32, xmm).
            if (r.isreg) { setreg(r.regno, 4, xmm.u32(modrm_reg)); return; }
            mem.w32(r.ea + seg, xmm.u32(modrm_reg)); return; }
        case 0xD6: { Rm r = modrm(); if (r.isreg) unsupported("movq2dq reg"); mem.w64(r.ea + seg, xmm.u64(modrm_reg)); return; }
        case 0x50: {  // movmskps/movmskpd: the source is the r/m XMM, the destination a GPR
            Rm r = modrm();
            if (!r.isreg) unsupported("movmskps memory operand");
            uint32_t m = 0; if (p66) for (int i = 0; i < 2; ++i) m |= (xmm.r[r.regno][i * 2 + 1] >> 31) << i;
            else for (int i = 0; i < 4; ++i) m |= (xmm.r[r.regno][i] >> 31) << i;
            setreg(modrm_reg, 4, m); return; }
        case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: sse_arith(op); return;
        case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: sse_arith(op); return;
        case 0x58: case 0x59: sse_arith(op); return;
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
        case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6F: case 0x7F:
        case 0x74: case 0x75: case 0x76: case 0xD7: case 0xE7: case 0xEB: case 0xEF:
        case 0xD1: case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xE1: case 0xE2:
        case 0xF1: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6:
        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF: case 0xE0: case 0xE8: case 0xE9: case 0xEA:
        case 0xEC: case 0xED: case 0xEE:
        case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE:
            sse_int(op); return;
        case 0x70: {  // pshufd (66) / pshuflw (F2) / pshufhw (F3), imm8 selects lanes
            Rm r = modrm(); uint8_t imm = fetch(); uint32_t a[4]; sse_read(r, a);
            uint32_t out[4];
            if (p66) { for (int i = 0; i < 4; ++i) out[i] = a[(imm >> (2 * i)) & 3]; }
            else if (rep == 0xF2 || rep == 0xF3) {  // shuffle four 16-bit lanes in one half
                uint16_t src[8], orig[8]; memcpy(orig, a, 16); memcpy(src, a, 16);
                unsigned base = rep == 0xF2 ? 0 : 4;
                for (int i = 0; i < 4; ++i) src[base + i] = orig[base + ((imm >> (2 * i)) & 3)];  // selects read the ORIGINAL lanes
                memcpy(out, src, 16);
            } else unsupported("0f 70 (MMX pshufw)");
            sse_write_reg(modrm_reg, out); return; }
        case 0x71: case 0x72: case 0x73: {  // packed shifts by imm8: /2 sr, /4 sar, /6 sl, 73 /3 psrldq /7 pslldq
            if (!p66) unsupported("0f 71/72/73 (MMX)");
            // Here the reg field is the /n shift selector; the XMM operand is r/m
            // and must be a register (mod=11): the imm8 forms have no memory
            // destination (verified #UD on real hardware; the memory-count form
            // is 0F F1/F2/F3/D1-D3 with an xmm destination instead).
            Rm r = modrm(); unsigned sop = modrm_reg; uint8_t cnt = fetch();
            if (!r.isreg) unsupported("0f 71/72/73 memory destination");
            uint32_t a[4]; sse_read(r, a);
            sse_shift(op, sop, cnt, a);
            sse_write(r, a); return; }
        case 0x18: modrm(); return;  // prefetchnta/t0/t1/t2 + reserved /4../7: all no-op hints (verified on real hardware, /0-/7 execute as NOPs in both mem and mod11 forms)
        case 0x31: {  // rdtsc: a synthetic counter that only ever advances (games poll it for timing)
            uint64_t t = tsc += 1000;
            reg[EAX] = uint32_t(t); reg[EDX] = uint32_t(t >> 32); return; }
        case 0xA2: {  // cpuid: deterministic leaves; feature bits only for what we implement
            switch (reg[EAX]) {
            case 0: reg[EAX] = 1; reg[EBX] = 0x756e6547u; reg[EDX] = 0x49656e69u; reg[ECX] = 0x6c65746eu; return;  // "GenuineIntel"
            case 1: reg[EAX] = 0x000006b4u; reg[EBX] = 0; reg[ECX] = 0;
                    reg[EDX] = (1u << 0) | (1u << 4) | (1u << 15) | (1u << 19) | (1u << 23) | (1u << 24) | (1u << 25) | (1u << 26); return;
                    // fpu tsc cmov clflush mmx fxsr sse sse2
            default: reg[EAX] = reg[EBX] = reg[ECX] = reg[EDX] = 0; return; } }
        case 0xC3: {  // movnti: 0F C3 r/m32 — a dword store in 32-bit mode; the
                      // m64 form needs REX.W (unreachable here). The 66 prefix has
                      // no MOVNTI form: #UD (verified on real hardware).
            if (p66) unsupported("66 0f c3 (no 66 movnti form)");
            Rm r = modrm(); if (r.isreg) unsupported("movnti reg");
            mem.w32(r.ea + seg, getreg(modrm_reg, 4)); return; }
        case 0xC6: {  // shufps (no prefix) / shufpd (66)
            Rm r = modrm(); uint8_t imm = fetch(); uint32_t a[4], b[4]; sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4];
            if (p66) { out[0] = a[(imm & 1) * 2]; out[1] = a[(imm & 1) * 2 + 1];
                       out[2] = b[((imm >> 1) & 1) * 2]; out[3] = b[((imm >> 1) & 1) * 2 + 1]; }
            else { out[0] = a[imm & 3]; out[1] = a[(imm >> 2) & 3]; out[2] = b[(imm >> 4) & 3]; out[3] = b[(imm >> 6) & 3]; }
            sse_write_reg(modrm_reg, out); return; }
        default: unsupported("0f opcode");
        }
    }
    // bt/bts/btr/btc r/m, r|imm. `action`: 0 test, 1 set, 2 reset, 3 complement.
    void bt(const Rm& r, unsigned regno, unsigned n, unsigned action) {
        uint32_t bit = getreg(regno, n) & (8 * n - 1);
        bt_bit(r, bit, n, action);
    }
    // bt/bts/btr/btc r/m, imm8: the bit index is the immediate itself.
    void bt_imm(const Rm& r, uint32_t bit, unsigned n, unsigned action) {
        bt_bit(r, bit & (8 * n - 1), n, action);
    }
    void bt_bit(const Rm& r, uint32_t bit, unsigned n, unsigned action) {
        uint32_t v = rd(r, n);
        setf(CF, (v >> bit) & 1);
        if (!action) return;
        if (action == 1) v |= 1u << bit; else if (action == 2) v &= ~(1u << bit); else v ^= 1u << bit;
        wr(r, n, v);
    }
    void shld(const Rm& r, unsigned regno, unsigned n, unsigned cnt, bool right) {
        if (!cnt) return;
        unsigned bits = 8 * n; uint32_t m = mask(n);
        uint64_t dst = rd(r, n) & m, src = getreg(regno, n) & m;
        uint32_t out;
        if (right) {
            if (cnt > bits) { out = 0; setf(CF, 0); }
            else { uint64_t v = (dst >> cnt) | (src << (bits - cnt)); out = uint32_t(v) & m; setf(CF, (dst >> (cnt - 1)) & 1); }
        } else {
            if (cnt > bits) { out = 0; setf(CF, 0); }
            else { uint64_t v = (dst << cnt) | (src >> (bits - cnt)); out = uint32_t(v) & m; setf(CF, (dst >> (bits - cnt)) & 1); }
        }
        szp(out, n); setf(AF, 0);
        setf(OF, ((out >> (bits - 1)) & 1) != ((out >> (bits - 2)) & 1));
        wr(r, n, out);
    }
    // ---- SSE/SSE2 ----
    // Encoding conventions (what MSVC and mingw emit): no prefix = packed
    // single, 66 = packed double / integer, F3 = scalar single, F2 = scalar
    // double. SSE is exact on every host because the register bytes are the
    // value; MXCSR is kept but no floating-point exception is ever raised.
    void sse_reg(int i, uint32_t out[4]) const { for (int k = 0; k < 4; ++k) out[k] = xmm.r[i][k]; }
    void sse_write_reg(int i, const uint32_t v[4]) { for (int k = 0; k < 4; ++k) xmm.r[i][k] = v[k]; }
    void sse_read(const Rm& r, uint32_t out[4]) const { if (r.isreg) { for (int k = 0; k < 4; ++k) out[k] = xmm.r[r.regno][k]; } else mem.r128(r.ea + seg, out); }
    void sse_write(const Rm& r, const uint32_t v[4]) { if (r.isreg) { for (int k = 0; k < 4; ++k) xmm.r[r.regno][k] = v[k]; } else mem.w128(r.ea + seg, v); }
    uint32_t sse_rd32(const Rm& r, int off = 0) const { return r.isreg ? xmm.r[r.regno][off / 4] : mem.r32(r.ea + seg + off); }
    uint64_t sse_rd64(const Rm& r, int off = 0) const {
        if (r.isreg) { uint64_t q; memcpy(&q, reinterpret_cast<const uint8_t*>(xmm.r[r.regno]) + off, 8); return q; }
        return mem.r64(r.ea + seg + off);
    }
    float sse_f32(const Rm& r, int off = 0) const { uint32_t v = sse_rd32(r, off); float f; memcpy(&f, &v, 4); return f; }
    double sse_f64(const Rm& r, int off = 0) const { uint64_t v = sse_rd64(r, off); double d; memcpy(&d, &v, 8); return d; }
    float xf32(int i) const { return xmm.fs(i); }
    static uint32_t f32b(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
    static float bf32(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }
    static uint64_t f64b(double d) { uint64_t v; memcpy(&v, &d, 8); return v; }
    static double bf64(uint64_t v) { double d; memcpy(&d, &v, 8); return d; }
    void sse_move(uint8_t op) {
        Rm r = modrm();
        bool ss = rep == 0xF3, sd = rep == 0xF2;
        if (op == 0x10 || op == 0x11) {  // movss/movsd/movups/movupd
            if (op == 0x11) {
                uint32_t v[4]; sse_reg(modrm_reg, v);
                if (ss) mem.w32(r.ea + seg, v[0]);
                else if (sd) mem.w64(r.ea + seg, uint64_t(v[1]) << 32 | v[0]);
                else sse_write(r, v);
            } else if (ss || sd) {
                uint32_t keep[4]; sse_reg(modrm_reg, keep);
                if (ss) keep[0] = sse_rd32(r);
                else { uint64_t q = sse_rd64(r); keep[0] = uint32_t(q); keep[1] = uint32_t(q >> 32); }
                sse_write_reg(modrm_reg, keep);
            } else { uint32_t v[4]; sse_read(r, v); sse_write_reg(modrm_reg, v); }
            return;
        }
        if (op == 0x28 || op == 0x29) {  // movaps/movapd (the guest arena is page aligned)
            uint32_t v[4];
            if (op == 0x29) { sse_reg(modrm_reg, v); sse_write(r, v); } else { sse_read(r, v); sse_write_reg(modrm_reg, v); }
            return;
        }
        if (op == 0x12 || op == 0x16) {  // movlps/movhps/movlpd/movhpd load, movhlps/movlhps
            uint32_t keep[4]; sse_reg(modrm_reg, keep);
            if (r.isreg) {
                uint32_t src[4]; sse_reg(r.regno, src);
                if (op == 0x12) { keep[0] = src[2]; keep[1] = src[3]; } else { keep[2] = src[0]; keep[3] = src[1]; }
            } else {
                uint64_t q = mem.r64(r.ea + seg);
                if (op == 0x12) { keep[0] = uint32_t(q); keep[1] = uint32_t(q >> 32); }
                else { keep[2] = uint32_t(q); keep[3] = uint32_t(q >> 32); }
            }
            sse_write_reg(modrm_reg, keep); return;
        }
        if (op == 0x13 || op == 0x17) {  // movlps/movhps store
            if (r.isreg) unsupported("movhps reg");
            uint32_t v[4]; sse_reg(modrm_reg, v);
            uint32_t lo = op == 0x13 ? v[0] : v[2], hi = op == 0x13 ? v[1] : v[3];
            mem.w64(r.ea + seg, uint64_t(hi) << 32 | lo); return;
        }
    }
    // Integer SIMD group (all 66-prefixed in MSVC code).
    // Packed shift shared by the imm8 (0F 71/72/73 /n) and register (D1-D3,
    // E1-E3, F1-F3) forms. `sop` is the /n from the ModRM byte; for the
    // register forms it comes from the opcode's low three bits.
    //   71: word lanes   72: dword lanes   73: qword lanes (and byte shifts)
    //   /2 = right logical, /4 = right arithmetic (words/dwords), /6 = left,
    //   73 /3 = pslldq, 73 /7 = psrldq (whole-register byte shifts).
    void sse_shift(uint8_t op, unsigned sop, unsigned cnt, uint32_t a[4]) {
        uint8_t* ab = reinterpret_cast<uint8_t*>(a);
        // Selector check first, before the count clamp: the imm8 group defines
        // only /2 (right), /4 (arithmetic) and /6 (left) for 71/72, and /2
        // psrlq, /3 psrldq, /6 psllq, /7 pslldq for 73 — psra has no 73 form.
        // Any other /n is #UD even when the count saturates.
        bool sel = op == 0x71 || op == 0x72 ? (sop == 2 || sop == 4 || sop == 6)
                                             : (sop == 2 || sop == 3 || sop == 6 || sop == 7);
        if (!sel) unsupported("invalid opcode form (sse shift /n)");
        if (op == 0x73 && (sop == 3 || sop == 7)) {  // pslldq/psrldq: shift the whole 128 bits by bytes
            unsigned k = cnt >= 16 ? 16 : cnt;
            uint8_t tmp[16];
            memcpy(tmp, ab, 16);
            std::memset(ab, 0, 16);
            if (sop == 7) memcpy(ab + k, tmp, 16 - k); else memcpy(ab, tmp + k, 16 - k);  // /7 pslldq (left), /3 psrldq (right)
            return;
        }
        unsigned gran = op == 0x71 ? 2 : op == 0x72 ? 4 : 8;
        unsigned maxcnt = gran == 2 ? 16 : gran == 4 ? 32 : 64;
        if (cnt >= maxcnt) {  // counts >= lane width: zero, or per-lane sign fill for the arithmetic shifts
            if (sop == 4 && gran == 2) { for (unsigned i = 0; i < 8; ++i) { uint16_t fill = (ab[2 * i + 1] & 0x80) ? 0xffffu : 0; memcpy(ab + 2 * i, &fill, 2); } }
            else if (sop == 4 && gran == 4) { for (int i = 0; i < 4; ++i) a[i] = (ab[4 * i + 3] & 0x80) ? 0xffffffffu : 0; }
            else std::memset(ab, 0, 16);
            return;
        }
        unsigned lanes = 16 / gran;
        for (unsigned i = 0; i < lanes; ++i) {
            if (sop == 6) {  // left
                if (gran == 8) { uint64_t v; memcpy(&v, ab + 8 * i, 8); v <<= cnt; memcpy(ab + 8 * i, &v, 8); }
                else if (gran == 4) a[i] <<= cnt;
                else { uint16_t v; memcpy(&v, ab + 2 * i, 2); v = uint16_t(v << cnt); memcpy(ab + 2 * i, &v, 2); }
            } else if (sop == 4 && gran == 2) {  // psraw
                int16_t v; memcpy(&v, ab + 2 * i, 2); v = int16_t(v >> cnt); memcpy(ab + 2 * i, &v, 2);
            } else if (sop == 4 && gran == 4) {  // psrad
                int32_t v; memcpy(&v, ab + 4 * i, 4); v >>= cnt; memcpy(ab + 4 * i, &v, 4);
            } else if (sop == 2) {  // right logical
                if (gran == 8) { uint64_t v; memcpy(&v, ab + 8 * i, 8); v >>= cnt; memcpy(ab + 8 * i, &v, 8); }
                else if (gran == 4) a[i] >>= cnt;
                else { uint16_t v; memcpy(&v, ab + 2 * i, 2); v = uint16_t(v >> cnt); memcpy(ab + 2 * i, &v, 2); }
            } else unsupported("sse shift /n unreachable");  // selectors validated at the top
        }
    }
    // Arithmetic and compare group (0F 51-5F). `op` is the second opcode byte.
    void sse_arith(uint8_t op) {
        Rm r = modrm();
        bool ss = rep == 0xF3, sd = rep == 0xF2, pd = p66;
        uint32_t a[4], b[4];
        switch (op) {
        case 0x51: {  // sqrt: the r/m operand is the source; the destination keeps its upper lanes
            sse_reg(modrm_reg, a);
            if (ss) a[0] = f32b(std::sqrt(sse_f32(r)));
            else if (sd) { double x = std::sqrt(sse_f64(r)); uint64_t q = f64b(x); a[0] = uint32_t(q); a[1] = uint32_t(q >> 32); }
            else if (pd) { for (int i = 0; i < 2; ++i) { double x = std::sqrt(sse_f64(r, i * 8)); uint64_t q = f64b(x); a[i * 2] = uint32_t(q); a[i * 2 + 1] = uint32_t(q >> 32); } }
            else { for (int i = 0; i < 4; ++i) a[i] = f32b(std::sqrt(sse_f32(r, i * 4))); }
            sse_write_reg(modrm_reg, a); return; }
        case 0x52: case 0x53: {  // rsqrtps/rcpps, and the F3 scalar rsqrtss/rcpss
            sse_reg(modrm_reg, a);
            // The scalar forms read one float and leave lanes 1-3 alone; a
            // memory source is only 4 bytes, so reading four would fault.
            int lanes = ss ? 1 : 4;
            for (int i = 0; i < lanes; ++i) { float x = sse_f32(r, i * 4); a[i] = f32b(op == 0x52 ? 1.0f / std::sqrt(x) : 1.0f / x); }
            sse_write_reg(modrm_reg, a); return; }
        case 0x54: case 0x55: case 0x56: case 0x57: {  // and/andn/or/xor
            sse_reg(modrm_reg, a); sse_read(r, b);
            for (int i = 0; i < 4; ++i) a[i] = op == 0x54 ? (a[i] & b[i]) : op == 0x55 ? (~a[i] & b[i]) : op == 0x56 ? (a[i] | b[i]) : (a[i] ^ b[i]);
            sse_write_reg(modrm_reg, a); return; }
        case 0x5A: {  // cvtps2pd / cvtpd2ps / cvtss2sd / cvtsd2ss
            sse_reg(modrm_reg, a);
            if (sd) { float f = float(sse_f64(r)); a[0] = f32b(f); }
            else if (ss) { double d = double(sse_f32(r)); uint64_t q = f64b(d); a[0] = uint32_t(q); a[1] = uint32_t(q >> 32); }
            else if (pd) { a[0] = f32b(float(sse_f64(r, 0))); a[1] = f32b(float(sse_f64(r, 8))); }
            else { double d0 = double(sse_f32(r, 0)), d1 = double(sse_f32(r, 4)); uint64_t q0 = f64b(d0), q1 = f64b(d1);
                   a[0] = uint32_t(q0); a[1] = uint32_t(q0 >> 32); a[2] = uint32_t(q1); a[3] = uint32_t(q1 >> 32); }
            sse_write_reg(modrm_reg, a); return; }
        case 0x5B: {  // cvtdq2ps / cvtps2dq / cvttps2dq
            sse_read(r, b);
            if (ss) { for (int i = 0; i < 4; ++i) a[i] = uint32_t(int32_t(std::trunc(bf32(b[i])))); }
            else if (pd) { for (int i = 0; i < 4; ++i) a[i] = uint32_t(int32_t(std::nearbyint(bf32(b[i])))); }
            else { for (int i = 0; i < 4; ++i) a[i] = f32b(float(int32_t(b[i]))); }
            sse_write_reg(modrm_reg, a); return; }
        case 0x58: case 0x59: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {  // add/mul/sub/min/div/max
            sse_reg(modrm_reg, a); sse_read(r, b);
            if (ss) a[0] = f32b(fold(op, bf32(a[0]), bf32(b[0])));
            else if (sd) { double x = fold(op, bf64(uint64_t(a[1]) << 32 | a[0]), bf64(uint64_t(b[1]) << 32 | b[0])); uint64_t q = f64b(x); a[0] = uint32_t(q); a[1] = uint32_t(q >> 32); }
            else if (pd) { for (int i = 0; i < 2; ++i) { double x = fold(op, bf64(uint64_t(a[i * 2 + 1]) << 32 | a[i * 2]), bf64(uint64_t(b[i * 2 + 1]) << 32 | b[i * 2])); uint64_t q = f64b(x); a[i * 2] = uint32_t(q); a[i * 2 + 1] = uint32_t(q >> 32); } }
            else { for (int i = 0; i < 4; ++i) a[i] = f32b(fold(op, bf32(a[i]), bf32(b[i]))); }
            sse_write_reg(modrm_reg, a); return; }
        default: unsupported("sse arith");
        }
    }
    static double fold(uint8_t op, double x, double y) {
        switch (op) {
        case 0x58: return x + y;
        case 0x59: return x * y;
        case 0x5C: return x - y;
        case 0x5E: return x / y;
        case 0x5D: return std::isnan(x) || std::isnan(y) ? std::nan("") : x < y ? x : y;
        default: return std::isnan(x) || std::isnan(y) ? std::nan("") : x > y ? x : y;
        }
    }
    // Integer SIMD group (all 66-prefixed in MSVC code).
    void sse_int(uint8_t op) {
        Rm r = modrm();
        uint32_t a[4], b[4];
        switch (op) {
        case 0x60: case 0x61: case 0x62: case 0x6C: {  // punpcklbw/wd/dq/qdq
            unsigned gran = op == 0x60 ? 1 : op == 0x61 ? 2 : op == 0x62 ? 4 : 8;
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a); const uint8_t* bb = reinterpret_cast<const uint8_t*>(b);
            for (unsigned i = 0; i < 8 / gran; ++i) { memcpy(o + i * 2 * gran, ab + i * gran, gran); memcpy(o + i * 2 * gran + gran, bb + i * gran, gran); }
            sse_write_reg(modrm_reg, out); return; }
        case 0x68: case 0x69: case 0x6A: case 0x6D: {  // punpckhbw/wd/dq/qdq
            unsigned gran = op == 0x68 ? 1 : op == 0x69 ? 2 : op == 0x6A ? 4 : 8;
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a) + 8; const uint8_t* bb = reinterpret_cast<const uint8_t*>(b) + 8;
            for (unsigned i = 0; i < 8 / gran; ++i) { memcpy(o + i * 2 * gran, ab + i * gran, gran); memcpy(o + i * 2 * gran + gran, bb + i * gran, gran); }
            sse_write_reg(modrm_reg, out); return; }
        case 0x63: case 0x67: case 0x6B: {  // packsswb / packuswb / packssdw
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            if (op == 0x63) {
                for (int i = 0; i < 8; ++i) { int v = int16_t(uint16_t(a[i / 2] >> (16 * (i % 2)))); o[i] = uint8_t(v < -128 ? -128 : v > 127 ? 127 : v); }
                for (int i = 0; i < 8; ++i) { int v = int16_t(uint16_t(b[i / 2] >> (16 * (i % 2)))); o[8 + i] = uint8_t(v < -128 ? -128 : v > 127 ? 127 : v); }
            } else if (op == 0x67) {  // packuswb: signed 16-bit in, unsigned 8-bit saturate
                for (int i = 0; i < 8; ++i) { int v = int16_t(uint16_t(a[i / 2] >> (16 * (i % 2)))); o[i] = uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); }
                for (int i = 0; i < 8; ++i) { int v = int16_t(uint16_t(b[i / 2] >> (16 * (i % 2)))); o[8 + i] = uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); }
            } else {
                for (int i = 0; i < 4; ++i) { int v = int32_t(a[i]); int16_t s = int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v); memcpy(o + 2 * i, &s, 2); }
                for (int i = 0; i < 4; ++i) { int v = int32_t(b[i]); int16_t s = int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v); memcpy(o + 8 + 2 * i, &s, 2); }
            }
            sse_write_reg(modrm_reg, out); return; }
        case 0x64: case 0x65: case 0x66: {  // pcmpgtb/w/d
            unsigned gran = op == 0x64 ? 1 : op == 0x65 ? 2 : 4;
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a); const uint8_t* bb = reinterpret_cast<const uint8_t*>(b);
            for (unsigned i = 0; i < 16 / gran; ++i) {
                bool gt;
                if (gran == 1) gt = int8_t(ab[i]) > int8_t(bb[i]);
                else if (gran == 2) { int16_t x, y; memcpy(&x, ab + 2 * i, 2); memcpy(&y, bb + 2 * i, 2); gt = x > y; }
                else { int32_t x, y; memcpy(&x, ab + 4 * i, 4); memcpy(&y, bb + 4 * i, 4); gt = x > y; }
                memset(o + i * gran, gt ? 0xff : 0, gran);
            }
            sse_write_reg(modrm_reg, out); return; }
        case 0x6F: {  // movdqa/movdqu load
            uint32_t v[4]; sse_read(r, v); sse_write_reg(modrm_reg, v); return; }
        case 0x7F: {  // movdqa/movdqu store
            uint32_t v[4]; sse_reg(modrm_reg, v); sse_write(r, v); return; }
        case 0x74: case 0x75: case 0x76: {  // pcmpeqb/w/d
            unsigned gran = op == 0x74 ? 1 : op == 0x75 ? 2 : 4;
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a); const uint8_t* bb = reinterpret_cast<const uint8_t*>(b);
            for (unsigned i = 0; i < 16 / gran; ++i) memset(o + i * gran, memcmp(ab + i * gran, bb + i * gran, gran) == 0 ? 0xff : 0, gran);
            sse_write_reg(modrm_reg, out); return; }
        case 0xD7: {  // pmovmskb: the source is the r/m XMM, the destination a GPR
            if (!r.isreg) unsupported("pmovmskb memory operand");
            sse_reg(r.regno, a);
            uint32_t m = 0; const uint8_t* ab = reinterpret_cast<const uint8_t*>(a);
            for (int i = 0; i < 16; ++i) m |= ((ab[i] >> 7) & 1) << i;
            setreg(modrm_reg, 4, m); return; }
        case 0xE7: {  // movntdq
            if (r.isreg) unsupported("movntdq reg");
            uint32_t v[4]; sse_reg(modrm_reg, v); mem.w128(r.ea + seg, v); return; }
        case 0xEB: case 0xEF: {  // por / pxor
            sse_reg(modrm_reg, a); sse_read(r, b);
            for (int i = 0; i < 4; ++i) a[i] = op == 0xEB ? (a[i] | b[i]) : (a[i] ^ b[i]);
            sse_write_reg(modrm_reg, a); return; }
        case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE: {  // psubb/w/d/q, paddb/w/d/q
            unsigned gran = (op == 0xF8 || op == 0xFC) ? 1 : (op == 0xF9 || op == 0xFD) ? 2 : (op == 0xFA || op == 0xFE) ? 4 : 8;
            bool add = op >= 0xFC;
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a); const uint8_t* bb = reinterpret_cast<const uint8_t*>(b);
            for (unsigned i = 0; i < 16 / gran; ++i) {
                uint64_t x = 0, y = 0;
                memcpy(&x, ab + i * gran, gran); memcpy(&y, bb + i * gran, gran);
                uint64_t v = add ? x + y : x - y;
                memcpy(o + i * gran, &v, gran);
            }
            sse_write_reg(modrm_reg, out); return; }
        case 0xD1: case 0xD2: case 0xD3:   // psrlw/d/q  r/m, xmm
        case 0xE1: case 0xE2: case 0xE3:   // psraw/d/q  r/m, xmm
        case 0xF1: case 0xF2: case 0xF3: { // psllw/d/q  r/m, xmm
            if (!p66) unsupported("0f d1..f3 (MMX)");
            uint32_t a[4], b[4]; sse_reg(modrm_reg, a); sse_read(r, b);
            uint64_t cnt; memcpy(&cnt, b, 8);  // the count is the low 64 bits of the source
            unsigned sop = op == 0xD1 ? 2 : op == 0xD2 ? 2 : op == 0xD3 ? 2
                         : op == 0xE1 ? 4 : op == 0xE2 ? 4
                         : op == 0xF1 ? 6 : op == 0xF2 ? 6 : 6;
            unsigned opb = op == 0xD1 || op == 0xE1 || op == 0xF1 ? 0x71
                         : op == 0xD2 || op == 0xE2 || op == 0xF2 ? 0x72 : 0x73;
            sse_shift(opb, sop, unsigned(cnt), a);
            sse_write_reg(modrm_reg, a); return; }
        case 0xD4: {  // paddq (66 0F D4): both 64-bit lanes
            sse_reg(modrm_reg, a); sse_read(r, b);
            const uint64_t* ax = reinterpret_cast<const uint64_t*>(a); const uint64_t* bx = reinterpret_cast<const uint64_t*>(b);
            uint64_t lo = ax[0] + bx[0], hi = ax[1] + bx[1];
            uint32_t out[4] = {uint32_t(lo), uint32_t(lo >> 32), uint32_t(hi), uint32_t(hi >> 32)};
            sse_write_reg(modrm_reg, out); return; }
        case 0xD5: {  // pmullw: 16x16 -> low 16 bits
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a); const uint8_t* bb = reinterpret_cast<const uint8_t*>(b);
            for (int i = 0; i < 8; ++i) { int16_t x, y, m; memcpy(&x, ab + 2 * i, 2); memcpy(&y, bb + 2 * i, 2); m = int16_t(x * y); memcpy(o + 2 * i, &m, 2); }
            sse_write_reg(modrm_reg, out); return; }
        case 0xF4: {  // pmuludq: unsigned dword 0 -> qword lanes
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint64_t lo = uint64_t(a[0]) * b[0], hi = uint64_t(a[2]) * b[2];
            uint32_t out[4] = {uint32_t(lo), uint32_t(lo >> 32), uint32_t(hi), uint32_t(hi >> 32)};
            sse_write_reg(modrm_reg, out); return; }
        case 0xF5: {  // pmaddwd: signed dwords from adjacent word pairs
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a); const uint8_t* bb = reinterpret_cast<const uint8_t*>(b);
            for (int i = 0; i < 4; ++i) { int32_t x0, x1, y0, y1;
                int16_t t; memcpy(&t, ab + 4 * i, 2); x0 = t; memcpy(&t, ab + 4 * i + 2, 2); x1 = t;
                memcpy(&t, bb + 4 * i, 2); y0 = t; memcpy(&t, bb + 4 * i + 2, 2); y1 = t;
                int32_t s = x0 * y0 + x1 * y1; memcpy(o + 4 * i, &s, 4); }
            sse_write_reg(modrm_reg, out); return; }
        case 0xF6: {  // psadbw: |a-b| sums of 8-byte halves
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a); const uint8_t* bb = reinterpret_cast<const uint8_t*>(b);
            for (int h = 0; h < 2; ++h) { unsigned s = 0;
                for (int i = 0; i < 8; ++i) s += unsigned(std::abs(int(ab[8 * h + i]) - int(bb[8 * h + i])));
                uint16_t s16 = uint16_t(s); memcpy(o + 8 * h, &s16, 2); }
            sse_write_reg(modrm_reg, out); return; }
        case 0xE0: {  // pavgb (66 0F E0)
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a); const uint8_t* bb = reinterpret_cast<const uint8_t*>(b);
            for (int i = 0; i < 16; ++i) o[i] = uint8_t((unsigned(ab[i]) + unsigned(bb[i]) + 1) >> 1);
            sse_write_reg(modrm_reg, out); return; }
        case 0xD8: case 0xD9: case 0xDC: case 0xDD:  // psubusb/psubusw, paddusb/paddusw
        case 0xE8: case 0xE9: case 0xEC: case 0xED: {  // psubsb/psubsw, paddsb/paddsw
            // Intel map: D8 psubusb, D9 psubusw, DC paddusb, DD paddusw,
            //            E8 psubsb,  E9 psubsw,  EC paddsb,  ED paddsw.
            unsigned gran = (op == 0xD8 || op == 0xDC || op == 0xE8 || op == 0xEC) ? 1 : 2;
            bool add = (op == 0xDC || op == 0xDD || op == 0xEC || op == 0xED);
            bool sgn = op >= 0xE0;
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a); const uint8_t* bb = reinterpret_cast<const uint8_t*>(b);
            if (sgn) {  // signed saturation (paddsw/psubsw)
                for (unsigned i = 0; i < 16 / gran; ++i) {
                    int32_t x, y;
                    if (gran == 1) { x = int8_t(ab[i]); y = int8_t(bb[i]); }
                    else { int16_t t, u; memcpy(&t, ab + 2 * i, 2); memcpy(&u, bb + 2 * i, 2); x = t; y = u; }
                    int32_t v = add ? x + y : x - y;
                    int32_t lo = gran == 1 ? -128 : -32768, hi = gran == 1 ? 127 : 32767;
                    if (v < lo) v = lo;
                    if (v > hi) v = hi;
                    if (gran == 1) o[i] = uint8_t(v); else memcpy(o + 2 * i, &v, 2);
                }
            } else {  // unsigned saturation (paddusb/psubusb)
                for (unsigned i = 0; i < 16 / gran; ++i) {
                    int32_t x, y;
                    if (gran == 1) { x = ab[i]; y = bb[i]; }
                    else { uint16_t t, u; memcpy(&t, ab + 2 * i, 2); memcpy(&u, bb + 2 * i, 2); x = t; y = u; }
                    int32_t v = add ? x + y : x - y;
                    int32_t hi = gran == 1 ? 255 : 65535;
                    if (v < 0) v = 0;
                    if (v > hi) v = hi;
                    if (gran == 1) o[i] = uint8_t(v); else memcpy(o + 2 * i, &v, 2);
                }
            }
            sse_write_reg(modrm_reg, out); return; }
        case 0xDA: case 0xDE:  // pminub/pmaxub
        case 0xEA: case 0xEE: {  // pminsw/pmaxsw
            bool ismin = op == 0xDA || op == 0xEA;
            bool sgn = op >= 0xEA;
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a); const uint8_t* bb = reinterpret_cast<const uint8_t*>(b);
            unsigned gran = sgn ? 2 : 1;
            for (unsigned i = 0; i < 16 / gran; ++i) {
                bool take_a;
                if (sgn) { int16_t x, y; memcpy(&x, ab + 2 * i, 2); memcpy(&y, bb + 2 * i, 2); take_a = ismin ? x < y : x > y; }
                else take_a = ismin ? ab[i] < bb[i] : ab[i] > bb[i];
                if (take_a) memcpy(o + i * gran, ab + i * gran, gran); else memcpy(o + i * gran, bb + i * gran, gran);
            }
            sse_write_reg(modrm_reg, out); return; }
        case 0xDB: case 0xDF: {  // pand / pandn (66 forms)
            sse_reg(modrm_reg, a); sse_read(r, b);
            for (int i = 0; i < 4; ++i) a[i] = op == 0xDB ? (a[i] & b[i]) : (~a[i] & b[i]);
            sse_write_reg(modrm_reg, a); return; }
        default: unsupported("sse integer");
        }
    }
    // ---- x87 ----
    // The encodings below are the ones an i686 MSVC/mingw CRT and game emit;
    // each was checked against the assembler's own encoding (see the x87 test
    // suite, which compares every one of them with this machine's real FPU).
    // Anything else faults by encoding rather than guessing.
    void x87(uint8_t op) {
        uint8_t m = fetch(); unsigned mod = m >> 6, regno = (m >> 3) & 7, rm = m & 7;
        unsigned rno = regno;
        Rm r = mod == 3 ? Rm{true, 0, rm, 0} : modrm_ea(m);
        auto st = [this](int i) { return fpu.get(i); };
        auto setst = [this](int i, Wide v) { fpu.set(i, v); };
        auto m32 = [&]() { float f; uint32_t v = mem.r32(r.ea + seg); memcpy(&f, &v, 4); return double(f); };
        auto m64 = [&]() { return bf64(mem.r64(r.ea + seg)); };
        auto m80 = [&]() { uint8_t b[10]; for (int i = 0; i < 10; ++i) b[i] = mem.r8(r.ea + seg + i); return Fpu::to_wide(b); };
        auto st32 = [&](double v) { float f = float(v); uint32_t b; memcpy(&b, &f, 4); mem.w32(r.ea + seg, b); };
        auto st64 = [&](double v) { mem.w64(r.ea + seg, f64b(v)); };
        auto st80 = [&](Wide v) { uint8_t b[10]; Fpu::from_wide(b, v); for (int i = 0; i < 10; ++i) mem.w8(r.ea + seg + i, b[i]); };
        auto to_int = [&](Wide v) { return int64_t(fpu.round_int(v)); };
        if (mod != 3) {
            switch (op) {
            case 0xD8: { double v = m32(); switch (rno) {
                case 0: setst(0, st(0) + v); return; case 1: setst(0, st(0) * v); return;
                case 2: fpu.compare(st(0), v); return; case 3: fpu.compare(st(0), v); fpu.pop(); return;
                case 4: setst(0, st(0) - v); return; case 5: setst(0, v - st(0)); return;
                case 6: setst(0, st(0) / v); return; default: setst(0, v / st(0)); return; } }
            case 0xDC: { double v = m64(); switch (rno) {
                case 0: setst(0, st(0) + v); return; case 1: setst(0, st(0) * v); return;
                case 2: fpu.compare(st(0), v); return; case 3: fpu.compare(st(0), v); fpu.pop(); return;
                case 4: setst(0, st(0) - v); return; case 5: setst(0, v - st(0)); return;
                case 6: setst(0, st(0) / v); return; default: setst(0, v / st(0)); return; } }
            case 0xDA: case 0xDE: { int32_t v = op == 0xDA ? int32_t(mem.r32(r.ea + seg)) : int32_t(int16_t(mem.r(r.ea + seg, 2))); switch (rno) {
                case 0: setst(0, st(0) + v); return; case 1: setst(0, st(0) * v); return;
                case 2: fpu.compare(st(0), v); return; case 3: fpu.compare(st(0), v); fpu.pop(); return;
                case 4: setst(0, st(0) - v); return; case 5: setst(0, v - st(0)); return;
                case 6: setst(0, st(0) / v); return; default: setst(0, v / st(0)); return; } }
            case 0xD9: switch (rno) {
                case 0: fpu.push(Wide(m32())); return;
                case 2: st32(double(st(0))); return;
                case 3: st32(double(st(0))); fpu.pop(); return;
                case 4: unsupported("fldenv");   // 28-byte environment: nothing in the CRT path reads it
                case 5: fpu.cw = uint16_t(mem.r(r.ea + seg, 2)); return;
                case 6: unsupported("fnstenv");
                default: mem.w(r.ea + seg, fpu.cw, 2); return; }  // fnstcw
            case 0xDD: switch (rno) {
                case 0: fpu.push(Wide(m64())); return;
                case 2: st64(double(st(0))); return;
                case 3: st64(double(st(0))); fpu.pop(); return;
                case 7: mem.w(r.ea + seg, fpu.status() & ~uint16_t(0x7f), 2); return;  // fnstsw m16 (DD /7 — verified on real hardware; fstp m80 is DB /7)
                default: unsupported("dd /n"); }
            case 0xDB: switch (rno) {
                case 0: fpu.push(Wide(int32_t(mem.r32(r.ea + seg)))); return;   // fild m32int
                case 2: mem.w32(r.ea + seg, uint32_t(int32_t(to_int(st(0))))); return;   // fist m32int
                case 3: mem.w32(r.ea + seg, uint32_t(int32_t(to_int(st(0))))); fpu.pop(); return;  // fistp
                case 5: fpu.push(m80()); return;                                // fld m80real
                case 7: st80(st(0)); fpu.pop(); return;                         // fstp m80real
                default: unsupported("db /n"); }
            case 0xDF: switch (rno) {
                case 0: fpu.push(Wide(int16_t(mem.r(r.ea + seg, 2)))); return;  // fild m16int
                case 2: mem.w(r.ea + seg, uint32_t(int16_t(to_int(st(0)))), 2); return;  // fist m16int
                case 3: mem.w(r.ea + seg, uint32_t(int16_t(to_int(st(0)))), 2); fpu.pop(); return;  // fistp m16int
                case 4: unsupported("fbld");                                    // packed BCD: not in the CRT path
                case 5: fpu.push(Wide(int64_t(mem.r64(r.ea + seg)))); return;   // fild m64int
                case 6: unsupported("fbstp");
                default: mem.w64(r.ea + seg, uint64_t(to_int(st(0)))); fpu.pop(); return;  // fistp m64int
            }
            default: unsupported("x87 memory form");
            }
        }
        switch (op) {
        case 0xD8: switch (rno) {
            case 0: setst(0, st(0) + st(rm)); return; case 1: setst(0, st(0) * st(rm)); return;
            case 2: fpu.compare(st(0), st(rm)); return; case 3: fpu.compare(st(0), st(rm)); fpu.pop(); return;
            case 4: setst(0, st(0) - st(rm)); return; case 5: setst(0, st(rm) - st(0)); return;
            case 6: setst(0, st(0) / st(rm)); return; default: setst(0, st(rm) / st(0)); return; }
        case 0xDC: switch (rno) {  // DC E0+i is the reversed form: st(i) = st(0) op st(i)
            case 0: setst(rm, st(rm) + st(0)); return; case 1: setst(rm, st(rm) * st(0)); return;
            case 2: fpu.compare(st(rm), st(0)); return; case 3: fpu.compare(st(rm), st(0)); fpu.pop(); return;
            case 4: setst(rm, st(0) - st(rm)); return; case 5: setst(rm, st(rm) - st(0)); return;
            case 6: setst(rm, st(0) / st(rm)); return; default: setst(rm, st(rm) / st(0)); return; }
        case 0xDE: switch (rno) {  // st(i) op= st(0), then pop
            case 0: setst(rm, st(rm) + st(0)); fpu.pop(); return;
            case 1: setst(rm, st(rm) * st(0)); fpu.pop(); return;
            case 3: if (rm != 1) unsupported("de /3"); fpu.pop(); fpu.pop(); return;  // fcompp
            case 4: setst(rm, st(0) - st(rm)); fpu.pop(); return;  // fsubrp
            case 5: setst(rm, st(rm) - st(0)); fpu.pop(); return;  // fsubp
            case 6: setst(rm, st(0) / st(rm)); fpu.pop(); return;  // fdivrp
            case 7: setst(rm, st(rm) / st(0)); fpu.pop(); return;  // fdivp
            default: unsupported("de /n"); }
        case 0xDA: switch (rno) {
            case 0: if (f(CF)) setst(0, st(rm)); return;  // fcmovb
            case 1: if (f(ZF)) setst(0, st(rm)); return;  // fcmove
            case 2: if (f(CF) || f(ZF)) setst(0, st(rm)); return;  // fcmovbe
            case 3: if (f(PF)) setst(0, st(rm)); return;  // fcmovu
            case 5: if (rm == 1) { fpu.compare(st(0), st(1)); fpu.pop(); fpu.pop(); return; }  // fucompp
                unsupported("da /5");
            default: unsupported("da /n"); }
        case 0xDB: switch (rno) {
            case 0: if (!f(CF)) setst(0, st(rm)); return;  // fcmovnb
            case 1: if (!f(ZF)) setst(0, st(rm)); return;  // fcmovne
            case 2: if (!f(CF) && !f(ZF)) setst(0, st(rm)); return;  // fcmovnbe
            case 3: if (!f(PF)) setst(0, st(rm)); return;  // fcmovnu
            // DB E2 fnclex, DB E3 fninit; DB E0/E1/E4-E7 are the obsolete
            // FENI/FDISI/FSETPM, which do nothing on anything since a 387.
            case 4: if (rm == 2) fpu.sw &= ~uint16_t(0x7f); else if (rm == 3) fpu.init(); return;
            case 5: fpu.compare(st(0), st(rm)); x87_flags(); return;  // fucomi st, st(i)
            case 6: fpu.compare(st(0), st(rm)); x87_flags(); return;  // fcomi st, st(i)
            default: unsupported("db /n"); }
        case 0xDF: switch (rno) {
            case 4: if (rm == 0) { reg[EAX] = (reg[EAX] & 0xffff0000u) | (fpu.status() & 0xffff); return; }  // fnstsw ax
                unsupported("df /4");
            case 5: fpu.compare(st(0), st(rm)); x87_flags(); fpu.pop(); return;  // fucomip st, st(i)
            case 6: fpu.compare(st(0), st(rm)); x87_flags(); fpu.pop(); return;  // fcomip st, st(i)
            default: unsupported("df /n"); }
        case 0xDD: switch (rno) {
            case 0: fpu.empty[fpu.idx(rm)] = true; return;  // ffree
            case 1: fpu.swap(0, rm); return;                // fxch
            case 2: setst(rm, st(0)); return;               // fst st(i)
            case 3: setst(rm, st(0)); fpu.pop(); return;    // fstp st(i)
            case 4: fpu.compare(st(0), st(rm)); return;     // fucom
            case 5: fpu.compare(st(0), st(rm)); fpu.pop(); return;  // fucomp
            default: unsupported("dd /n"); }
        case 0xD9: switch (m & 0x3f) {
            case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
                fpu.push(st(m & 7)); return;  // fld st(i)
            case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f:
                fpu.swap(0, m & 7); return;   // fxch st(i)
            case 0x10: return;                // fnop
            case 0x20: setst(0, -st(0)); return;             // fchs
            case 0x21: setst(0, std::fabs(st(0))); return;   // fabs
            case 0x24: fpu.compare(st(0), 0.0L); return;     // ftst
            case 0x25: {  // fxam: C1 = sign, C0/C2/C3 = class (Intel's table)
                Wide v = st(0);
                fpu.sw &= ~uint16_t(0x4700);
                if (std::signbit(v)) fpu.sw |= 0x0200;          // C1
                if (fpu.is_empty(0)) fpu.sw |= 0x4100;          // empty:    C3 C0
                else if (std::isnan(v)) fpu.sw |= 0x0100;       // NaN:      C0
                else if (std::isinf(v)) fpu.sw |= 0x0500;       // infinity: C2 C0
                else if (v == 0) fpu.sw |= 0x4000;              // zero:     C3
                else if (std::fpclassify(v) == FP_SUBNORMAL) fpu.sw |= 0x4400;  // denormal: C3 C2
                else fpu.sw |= 0x0400;                          // normal:   C2
                return; }
            case 0x28: fpu.push(1.0L); return;               // fld1
            case 0x29: fpu.push(3.32192809488736234787L); return;  // fldl2t
            case 0x2a: fpu.push(1.44269504088896340736L); return;  // fldl2e
            case 0x2b: fpu.push(3.14159265358979323846L); return;  // fldpi
            case 0x2c: fpu.push(0.30102999566398119521L); return;  // fldlg2
            case 0x2d: fpu.push(0.69314718055994530942L); return;  // fldln2
            case 0x2e: fpu.push(0.0L); return;               // fldz
            case 0x30: setst(0, std::exp2(st(0)) - 1.0L); return;  // f2xm1
            case 0x31: setst(1, st(1) * std::log2(st(0))); fpu.pop(); return;  // fyl2x
            case 0x32: setst(0, std::tan(st(0))); fpu.push(1.0L); return;  // fptan: st0 = tan(x), push 1.0
            case 0x33: setst(1, std::atan2(st(1), st(0))); fpu.pop(); return;  // fpatan
            case 0x34: {  // fxtract: st0 = significand in [1,2), st1 = unbiased exponent
                Wide v = st(0); int e; Wide m = std::frexp(v, &e); fpu.push(Wide(m * 2)); setst(1, Wide(e - 1)); return; }
            case 0x35: setst(0, std::fmod(st(0), st(1))); return;  // fprem1 (approximation: one iteration)
            case 0x36: fpu.top = (fpu.top + 1) & 7; return;  // fdecstp
            case 0x37: fpu.top = (fpu.top + 7) & 7; return;  // fincstp
            case 0x38: setst(0, std::fmod(st(0), st(1))); return;  // fprem (one iteration)
            case 0x39: setst(1, st(1) * std::log2(st(0) + 1.0L)); fpu.pop(); return;  // fyl2xp1
            case 0x3a: setst(0, std::sqrt(st(0))); return;   // fsqrt
            case 0x3b: { Wide s = std::sin(st(0)); fpu.push(std::cos(st(0))); setst(1, s); return; }  // fsincos
            case 0x3c: setst(0, fpu.round_int(st(0))); return;  // frndint
            case 0x3d: setst(0, std::ldexp(st(0), int(st(1)))); return;  // fscale
            case 0x3e: setst(0, std::sin(st(0))); return;    // fsin
            case 0x3f: setst(0, std::cos(st(0))); return;    // fcos
            default: unsupported("d9 mod3");
            }
        default: unsupported("x87");
        }
    }
    // fcomi/fucomi set the integer flags from the x87 condition bits:
    // ZF=C3 (equal), PF=C2 (unordered), CF=C0 (less).
    void x87_flags() {
        eflags = (eflags & ~uint32_t(ZF | PF | CF)) | 2u;
        if (fpu.sw & 0x4000) eflags |= ZF;
        if (fpu.sw & 0x0100) eflags |= CF;
        if (fpu.sw & 0x0400) eflags |= PF;  // C2 only: C0/C3 are the ordered outcomes
    }
    // ModRM/SIB/displacement decode for a ModRM byte already fetched (x87).
    Rm modrm_ea(uint8_t m) {
        unsigned mod = m >> 6, rm = m & 7;
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
