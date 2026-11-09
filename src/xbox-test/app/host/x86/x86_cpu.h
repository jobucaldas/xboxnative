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
    uint64_t executed = 0, limit = 500ull * 1000 * 1000;
    bool exited = false; uint32_t exit_code = 0;
    unsigned depth = 0;
    Fpu fpu; Sse xmm; uint32_t mxcsr = 0x1f80;
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
            Rm r = modrm(); uint32_t bit = fetch(); bt(r, bit, n, (modrm_reg & 3) + 0); return; }
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
        case 0xAE: {  // group 15: fences, fxrstor/xsave, ldmxcsr/stmxcsr
            Rm r = modrm();
            switch (modrm_reg) {
            case 0: case 3: case 5: case 6: return;  // fxsave/fxrstor/xsave/xrstor: state is host-side only
            case 2: mxcsr = rd(r, 4); return;        // ldmxcsr
            case 7: return;                          // clflush
            default: unsupported("0f ae /n"); }
        }
        case 0x10: case 0x11: sse_move(op); return;
        case 0x28: case 0x29: sse_move(op); return;
        case 0x12: case 0x13: case 0x16: case 0x17: sse_move(op); return;
        case 0x14: case 0x15: {  // unpcklps/unpckhps
            Rm r = modrm(); uint32_t a[4], b[4]; sse_read(r, a); sse_reg(modrm_reg, b);
            bool hi = op == 0x15; uint32_t out[4];
            for (int i = 0; i < 2; ++i) { out[i] = hi ? b[i + 2] : a[i]; out[i + 2] = hi ? a[i + 2] : b[i]; }
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
            Rm r = modrm();
            xmm.setu32(modrm_reg, rd(r, 4)); return; }
        case 0x7E: {  // movd/movq r/m, xmm (66) or movq xmm, xmm/m64 (F3)
            Rm r = modrm();
            if (rep == 0xF3) { xmm.setu64(modrm_reg, sse_rd64(r)); return; }
            if (r.isreg) { xmm.setu64(r.regno, xmm.u64(modrm_reg)); return; }
            mem.w32(r.ea + seg, xmm.u32(modrm_reg)); return; }
        case 0xD6: { Rm r = modrm(); if (r.isreg) unsupported("movq2dq reg"); mem.w64(r.ea + seg, xmm.u64(modrm_reg)); return; }
        case 0x50: {  // movmskps/movmskpd
            Rm r = modrm(); (void)r;
            uint32_t m = 0; if (p66) for (int i = 0; i < 2; ++i) m |= (xmm.r[modrm_reg][i * 2 + 1] >> 31) << i;
            else for (int i = 0; i < 4; ++i) m |= (xmm.r[modrm_reg][i] >> 31) << i;
            setreg(modrm_reg, 4, m); return; }
        case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: sse_arith(op); return;
        case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: sse_arith(op); return;
        case 0x58: case 0x59: sse_arith(op); return;
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
        case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6F: case 0x7F:
        case 0x74: case 0x75: case 0x76: case 0xD7: case 0xE7: case 0xEB: case 0xEF:
        case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE:
            sse_int(op); return;
        default: unsupported("0f opcode");
        }
    }
    // bt/bts/btr/btc r/m, r|imm. `action`: 0 test, 1 set, 2 reset, 3 complement.
    void bt(const Rm& r, unsigned regno, unsigned n, unsigned action) {
        uint32_t bit = getreg(regno, n) & (8 * n - 1);
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
    // Arithmetic and compare group (0F 51-5F). `op` is the second opcode byte.
    void sse_arith(uint8_t op) {
        Rm r = modrm();
        bool ss = rep == 0xF3, sd = rep == 0xF2, pd = p66;
        uint32_t a[4], b[4];
        switch (op) {
        case 0x51: {  // sqrt
            sse_reg(modrm_reg, a);
            if (ss) a[0] = f32b(std::sqrt(bf32(a[0])));
            else if (sd) { double x = std::sqrt(bf64(uint64_t(a[1]) << 32 | a[0])); a[0] = uint32_t(f64b(x)); a[1] = uint32_t(f64b(x) >> 32); }
            else if (pd) { for (int i = 0; i < 2; ++i) { double x = std::sqrt(bf64(uint64_t(a[i * 2 + 1]) << 32 | a[i * 2])); uint64_t q = f64b(x); a[i * 2] = uint32_t(q); a[i * 2 + 1] = uint32_t(q >> 32); } }
            else { for (int i = 0; i < 4; ++i) a[i] = f32b(std::sqrt(bf32(a[i]))); }
            sse_write_reg(modrm_reg, a); return; }
        case 0x52: case 0x53: {  // rsqrtps / rcpps (approximations: the exact reciprocal is closer than the hardware estimate)
            sse_reg(modrm_reg, a);
            for (int i = 0; i < 4; ++i) { float x = bf32(a[i]); a[i] = f32b(op == 0x52 ? 1.0f / std::sqrt(x) : 1.0f / x); }
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
        case 0x68: case 0x69: case 0x6A: {  // punpckhbw/wd/dq
            unsigned gran = op == 0x68 ? 1 : op == 0x69 ? 2 : 4;
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a) + 8; const uint8_t* bb = reinterpret_cast<const uint8_t*>(b) + 8;
            for (unsigned i = 0; i < 8 / gran; ++i) { memcpy(o + i * 2 * gran, ab + i * gran, gran); memcpy(o + i * 2 * gran + gran, bb + i * gran, gran); }
            sse_write_reg(modrm_reg, out); return; }
        case 0x63: case 0x6B: {  // packsswb / packssdw
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            if (op == 0x63) {
                for (int i = 0; i < 8; ++i) { int v = int16_t(uint16_t(a[i / 2] >> (16 * (i % 2)))); o[i] = uint8_t(v < -128 ? -128 : v > 127 ? 127 : v); }
                for (int i = 0; i < 8; ++i) { int v = int16_t(uint16_t(b[i / 2] >> (16 * (i % 2)))); o[8 + i] = uint8_t(v < -128 ? -128 : v > 127 ? 127 : v); }
            } else {
                for (int i = 0; i < 4; ++i) { int v = int32_t(a[i]); int16_t s = int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v); memcpy(o + 2 * i, &s, 2); }
                for (int i = 0; i < 4; ++i) { int v = int32_t(b[i]); int16_t s = int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v); memcpy(o + 8 + 2 * i, &s, 2); }
            }
            sse_write_reg(modrm_reg, out); return; }
        case 0x64: case 0x65: case 0x66: case 0x67: {  // pcmpgtb/w/d/q
            unsigned gran = op == 0x64 ? 1 : op == 0x65 ? 2 : op == 0x66 ? 4 : 8;
            sse_reg(modrm_reg, a); sse_read(r, b);
            uint32_t out[4] = {}; uint8_t* o = reinterpret_cast<uint8_t*>(out);
            const uint8_t* ab = reinterpret_cast<const uint8_t*>(a); const uint8_t* bb = reinterpret_cast<const uint8_t*>(b);
            for (unsigned i = 0; i < 16 / gran; ++i) {
                bool gt;
                if (gran == 1) gt = int8_t(ab[i]) > int8_t(bb[i]);
                else if (gran == 2) { int16_t x, y; memcpy(&x, ab + 2 * i, 2); memcpy(&y, bb + 2 * i, 2); gt = x > y; }
                else if (gran == 4) { int32_t x, y; memcpy(&x, ab + 4 * i, 4); memcpy(&y, bb + 4 * i, 4); gt = x > y; }
                else { int64_t x, y; memcpy(&x, ab + 8 * i, 8); memcpy(&y, bb + 8 * i, 8); gt = x > y; }
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
        case 0xD7: {  // pmovmskb
            sse_reg(modrm_reg, a);
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
                case 6: mem.w(r.ea + seg, fpu.status() & ~uint16_t(0x7f), 2); return;  // fnstsw
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
        case 0xDA: switch (m & 0x38) {
            case 0x00: if (f(CF)) setst(0, st(rm)); return;  // fcmovb
            case 0x08: if (f(ZF)) setst(0, st(rm)); return;  // fcmove
            case 0x10: if (f(CF) || f(ZF)) setst(0, st(rm)); return;  // fcmovbe
            case 0x18: if (f(PF)) setst(0, st(rm)); return;  // fcmovu
            default: unsupported("da /n"); }
        case 0xDB: switch (m & 0x38) {
            case 0x00: if (!f(CF)) setst(0, st(rm)); return;  // fcmovnb
            case 0x08: if (!f(ZF)) setst(0, st(rm)); return;  // fcmovne
            case 0x10: if (!f(CF) && !f(ZF)) setst(0, st(rm)); return;  // fcmovnbe
            case 0x18: if (!f(PF)) setst(0, st(rm)); return;  // fcmovnu
            case 0x20: fpu.sw &= ~uint16_t(0x7f); return;  // fnclex
            case 0x28: if (rm == 3) { fpu.init(); return; } unsupported("db /5");  // fninit
            case 0x30: fpu.compare(st(0), st(rm)); x87_flags(); return;  // fucomi
            case 0x38: fpu.compare(st(0), st(rm)); x87_flags(); return;  // fcomi
            default: unsupported("db /n"); }
        case 0xDF: switch (m & 0x38) {
            case 0x20: reg[EAX] = (reg[EAX] & 0xffff0000u) | (fpu.status() & 0xffff); return;  // fnstsw ax
            case 0x30: fpu.compare(st(0), st(rm)); x87_flags(); fpu.pop(); return;  // fucomip
            case 0x38: fpu.compare(st(0), st(rm)); x87_flags(); fpu.pop(); return;  // fcomip
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
            case 0x25: {  // fxam: C3=C2=C1=0, C0=1 for NaN/unsupported; a value of 0 sets C3
                Wide v = st(0); fpu.sw &= ~uint16_t(0x4700);
                if (std::isnan(v)) fpu.sw |= 0x0100;
                else if (v == 0) fpu.sw |= 0x4000;
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
            case 0x32: fpu.push(std::tan(st(0))); return;    // fptan (the 1.0 push is implicit in the model)
            case 0x33: setst(1, std::atan2(st(1), st(0))); fpu.pop(); return;  // fpatan
            case 0x34: { Wide v = st(0); int e; Wide m = std::frexp(v, &e); fpu.push(Wide(e)); setst(1, m * 2); return; }  // fxtract
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
