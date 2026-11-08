// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device tests for the x86 (PE32) execution core: (1) hardware-compared ALU
// semantics, (2) memory protections and explicit faults, (3) thunks and guest
// callbacks, (4) an owned i686 fixture PE run to a known exit code at its
// ImageBase and at a relocated base. Usage: x86-core <fixture.exe>
#include <cstdio>
#include <fstream>
#include <iterator>
#include "../host/x86/x86_win32.h"
#if !defined(__x86_64__) && !defined(__i386__)
#error "the ALU hardware comparison needs an x86 host"
#endif
using namespace x86;
static int failures = 0;
#define EXPECT(c, ...) do { if (!(c)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---------- harness: tiny flat machine for byte programs ----------
struct Machine {
    Memory mem; ThunkTable thunks; Cpu cpu{mem, thunks};
    static constexpr uint32_t CODE = 0x10000, DATA = 0x20000, STACK = 0x30000;
    Machine() { thunks.base = 0x7FFE0000; map_thunk_page(mem, thunks); mem.map(CODE, 4096, PROT_R | PROT_X); mem.map(DATA, 4096, PROT_R | PROT_W); mem.map(STACK, 4096, PROT_R | PROT_W); }
    void load(std::initializer_list<uint8_t> code) {
        mem.protect(CODE, 4096, PROT_R | PROT_W); std::vector<uint8_t> c(code); c.push_back(0xF4);  // hlt = explicit stop
        mem.write_bytes(CODE, c.data(), uint32_t(c.size())); mem.protect(CODE, 4096, PROT_R | PROT_X);
        cpu.eip = CODE; cpu.reg[ESP] = STACK + 4096 - 16; cpu.exited = false; cpu.depth = 0;
    }
    // Runs until the trailing hlt (reported as an Unsupported fault at the hlt address).
    bool run_to_end(size_t len) {
        try { cpu.run(0); } catch (const Fault& f) { return f.kind == FaultKind::Unsupported && f.eip == CODE + len; }
        return false;
    }
};
template <class F> static FaultKind fault_of(F f) { try { f(); } catch (const Fault& e) { return e.kind; } return FaultKind(-1); }

// ---------- (1) hardware comparison ----------
// Each native function executes one real instruction on this CPU with CL as the second operand/count and a given
// input CF, returning EAX and EFLAGS. The interpreter runs the same encoding; defined results/flags must be equal.
// (Build with -mno-red-zone: the asm uses pushf/pop.)
typedef void (*NatFn)(bool, uint32_t, uint32_t, uint32_t&, uint32_t&);
#define NATIVE_FN(NAME, MN, OPS) \
    static void NAME(bool cf, uint32_t a, uint32_t b, uint32_t& r, uint32_t& fl) { uint32_t na = a, nf; \
        if (cf) asm volatile("stc\n\t" MN " " OPS "\n\tpushf\n\tpop %%rdx" : "+a"(na), "=d"(nf) : "c"(b) : "cc"); \
        else asm volatile("clc\n\t" MN " " OPS "\n\tpushf\n\tpop %%rdx" : "+a"(na), "=d"(nf) : "c"(b) : "cc"); \
        r = na; fl = nf; }
#define NATIVE_SIZES(ID, MN) NATIVE_FN(ID##_8, MN, "%%cl, %%al") NATIVE_FN(ID##_16, MN, "%%cx, %%ax") NATIVE_FN(ID##_32, MN, "%%ecx, %%eax")
#define NATIVE_SHIFTS(ID, MN) NATIVE_FN(ID##_8, MN, "%%cl, %%al") NATIVE_FN(ID##_16, MN, "%%cl, %%ax") NATIVE_FN(ID##_32, MN, "%%cl, %%eax")
NATIVE_SIZES(n_add, "add") NATIVE_SIZES(n_or, "or") NATIVE_SIZES(n_adc, "adc") NATIVE_SIZES(n_sbb, "sbb")
NATIVE_SIZES(n_and, "and") NATIVE_SIZES(n_sub, "sub") NATIVE_SIZES(n_xor, "xor") NATIVE_SIZES(n_cmp, "cmp")
NATIVE_SHIFTS(n_rol, "rol") NATIVE_SHIFTS(n_ror, "ror") NATIVE_SHIFTS(n_shl, "shl") NATIVE_SHIFTS(n_shr, "shr") NATIVE_SHIFTS(n_sar, "sar")
NATIVE_FN(n_inc32, "incl", "%%eax") NATIVE_FN(n_dec32, "decl", "%%eax") NATIVE_FN(n_neg32, "negl", "%%eax") NATIVE_FN(n_not32, "notl", "%%eax")
NATIVE_FN(n_inc8, "incb", "%%al") NATIVE_FN(n_dec16, "decw", "%%ax")
NATIVE_FN(n_imul32, "imul", "%%ecx, %%eax") NATIVE_FN(n_imul16, "imul", "%%cx, %%ax")
static const uint32_t vals[] = {0, 1, 2, 3, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff, 0x80000000u, 0xffffffffu, 0x12345678u, 0x9abcdef0u, 0xdeadbeefu};
static uint64_t alu_cases = 0;
static const uint32_t ALL_FLAGS = CF | PF | AF | ZF | SF | OF;
// Runs one encoded instruction (plus a trailing hlt stop) with EAX=a, ECX=b, EDX=d and the given input CF.
static size_t setup(Machine& m, std::vector<uint8_t> enc, uint32_t a, uint32_t b, uint32_t d, bool cf) {
    size_t len = enc.size(); enc.push_back(0xF4);
    m.mem.protect(Machine::CODE, 4096, PROT_R | PROT_W);
    m.mem.write_bytes(Machine::CODE, enc.data(), uint32_t(enc.size())); m.mem.protect(Machine::CODE, 4096, PROT_R | PROT_X);
    m.cpu.eip = Machine::CODE; m.cpu.reg[EAX] = a; m.cpu.reg[ECX] = b; m.cpu.reg[EDX] = d; m.cpu.eflags = 2u | (cf ? uint32_t(CF) : 0u);
    return len;
}
static void exec(Machine& m, std::vector<uint8_t> enc, uint32_t a, uint32_t b, uint32_t d, bool cf) {
    size_t len = setup(m, enc, a, b, d, cf); EXPECT(m.run_to_end(len), "program did not reach end");
}
static FaultKind exec_fault(Machine& m, std::vector<uint8_t> enc, uint32_t a, uint32_t b, uint32_t d, bool cf) {
    setup(m, enc, a, b, d, cf); return fault_of([&] { m.cpu.run(0); });
}
static void test_alu() {
    Machine m;
    NatFn nat[8][3] = {{n_add_8, n_add_16, n_add_32}, {n_or_8, n_or_16, n_or_32}, {n_adc_8, n_adc_16, n_adc_32}, {n_sbb_8, n_sbb_16, n_sbb_32},
                       {n_and_8, n_and_16, n_and_32}, {n_sub_8, n_sub_16, n_sub_32}, {n_xor_8, n_xor_16, n_xor_32}, {n_cmp_8, n_cmp_16, n_cmp_32}};
    for (bool cf : {false, true}) for (uint32_t a : vals) for (uint32_t b : vals) for (int aop = 0; aop < 8; ++aop) {
        uint32_t def = (aop == 1 || aop == 4 || aop == 6) ? (CF | PF | ZF | SF | OF) : ALL_FLAGS;  // AF undefined for logic ops
        for (int si = 0; si < 3; ++si) {
            unsigned bits = si == 0 ? 8 : si == 1 ? 16 : 32; uint32_t msk = bits == 32 ? 0xffffffffu : (1u << bits) - 1;
            std::vector<uint8_t> enc; if (si == 1) enc.push_back(0x66);
            enc.push_back(uint8_t(aop * 8 + (si ? 1 : 0))); enc.push_back(0xC8);  // op r/m, r with al/ax/eax,cl/cx/ecx
            uint32_t nr, nf; nat[aop][si](cf, a, b, nr, nf); exec(m, enc, a, b, 0, cf);
            EXPECT(((m.cpu.reg[EAX] ^ nr) & msk) == 0 && (m.cpu.reg[EAX] & ~msk) == (a & ~msk) && !((m.cpu.eflags ^ nf) & def),
                   "alu%u op%d a=%x b=%x cf=%d: got %x/%x want %x/%x", bits, aop, a, b, cf, m.cpu.reg[EAX], m.cpu.eflags & def, nr, nf & def);
            ++alu_cases;
        }
    }
}
// Shifts and rotates (D2/D3 /n by CL): results always; CF/ZF/SF/PF for count != 0 (CF/OF undefined past the operand
// width); OF only for count 1; all flags unchanged for count 0.
static void test_shift() {
    Machine m;
    NatFn nat[5][3] = {{n_rol_8, n_rol_16, n_rol_32}, {n_ror_8, n_ror_16, n_ror_32}, {n_shl_8, n_shl_16, n_shl_32}, {n_shr_8, n_shr_16, n_shr_32}, {n_sar_8, n_sar_16, n_sar_32}};
    const int sops[5] = {0, 1, 4, 5, 7};
    for (unsigned cnt = 0; cnt <= 33; ++cnt) for (uint32_t a : vals) for (int cf = 0; cf < 2; ++cf) for (int k = 0; k < 5; ++k) for (int si = 0; si < 3; ++si) {
        unsigned size = si == 0 ? 8 : si == 1 ? 16 : 32; uint32_t msk = size == 32 ? 0xffffffffu : (1u << size) - 1;
        std::vector<uint8_t> enc; if (si == 1) enc.push_back(0x66);
        enc.push_back(si == 0 ? 0xD2 : 0xD3); enc.push_back(uint8_t(0xC0 | sops[k] << 3));
        uint32_t nr, nf; nat[k][si](cf, a, cnt, nr, nf); exec(m, enc, a, cnt, 0, cf);
        EXPECT(((m.cpu.reg[EAX] ^ nr) & msk) == 0 && (m.cpu.reg[EAX] & ~msk) == (a & ~msk), "shift%u sop%d a=%x cnt=%u: got %x want %x", size, sops[k], a, cnt, m.cpu.reg[EAX], nr);
        unsigned eff = cnt & 31; bool rotate = sops[k] < 2; uint32_t def;
        if (eff == 0) { def = CF; EXPECT(m.cpu.eflags == (2u | (cf ? uint32_t(CF) : 0u)), "count 0 must leave flags untouched"); }
        else if (rotate) def = CF | (eff == 1 ? uint32_t(OF) : 0u);
        else if (eff <= size) def = CF | PF | ZF | SF | (eff == 1 ? uint32_t(OF) : 0u);
        else def = ZF | SF | PF;
        EXPECT(!((m.cpu.eflags ^ nf) & def), "shift%u sop%d a=%x cnt=%u cf=%d: flags got %x want %x (mask %x)", size, sops[k], a, cnt, cf, m.cpu.eflags & def, nf & def, def);
        alu_cases += 2;
    }
}
// inc/dec/neg/not, imul r,r/m, one-operand mul/imul and div/idiv (including #DE).
static void test_unary_muldiv() {
    Machine m;
    for (uint32_t a : vals) for (uint32_t b : vals) for (int cf = 0; cf < 2; ++cf) {
        uint32_t nr, nf, got;
        struct U { NatFn fn; std::vector<uint8_t> enc; uint32_t msk, def; const char* name; };
        U cases[] = {
            {n_inc32, {0xFF, 0xC0}, 0xffffffffu, ALL_FLAGS, "inc32"}, {n_dec32, {0xFF, 0xC8}, 0xffffffffu, ALL_FLAGS, "dec32"},
            {n_neg32, {0xF7, 0xD8}, 0xffffffffu, ALL_FLAGS, "neg32"}, {n_not32, {0xF7, 0xD0}, 0xffffffffu, 0, "not32"},
            {n_inc8, {0xFE, 0xC0}, 0xffu, ALL_FLAGS, "inc8"}, {n_dec16, {0x66, 0xFF, 0xC8}, 0xffffu, ALL_FLAGS, "dec16"},
            {n_imul32, {0x0F, 0xAF, 0xC1}, 0xffffffffu, CF | OF, "imul r32,r/m32"}, {n_imul16, {0x66, 0x0F, 0xAF, 0xC1}, 0xffffu, CF | OF, "imul r16,r/m16"}};
        for (auto& c : cases) {
            c.fn(cf, a, b, nr, nf); exec(m, c.enc, a, b, 0, cf); got = m.cpu.reg[EAX];
            EXPECT(((got ^ nr) & c.msk) == 0 && (got & ~c.msk) == (a & ~c.msk) && !((m.cpu.eflags ^ nf) & c.def), "%s a=%x b=%x cf=%d: got %x/%x want %x/%x", c.name, a, b, cf, got, m.cpu.eflags & c.def, nr, nf & c.def);
        }
        uint64_t um = uint64_t(a) * b; int64_t sm = int64_t(int32_t(a)) * int32_t(b);
        exec(m, {0xF7, 0xE1}, a, b, 0, cf); EXPECT(m.cpu.reg[EAX] == uint32_t(um) && m.cpu.reg[EDX] == uint32_t(um >> 32) && bool(m.cpu.eflags & CF) == ((um >> 32) != 0) && bool(m.cpu.eflags & OF) == ((um >> 32) != 0), "mul32 a=%x b=%x", a, b);
        exec(m, {0xF7, 0xE9}, a, b, 0, cf); EXPECT(m.cpu.reg[EAX] == uint32_t(sm) && m.cpu.reg[EDX] == uint32_t(sm >> 32) && bool(m.cpu.eflags & CF) == (sm != int64_t(int32_t(sm))), "imul1-32 a=%x b=%x", a, b);
        exec(m, {0xF6, 0xE1}, a, b, 0, cf); { uint32_t p = (a & 0xff) * (b & 0xff); EXPECT((m.cpu.reg[EAX] & 0xffff) == p && bool(m.cpu.eflags & CF) == ((p >> 8) != 0), "mul8 a=%x b=%x", a, b); }
        for (uint32_t hi : {0u, 1u, 0xffffffffu, 0x7fffffffu}) {
            if (b == 0) break;
            uint64_t num = uint64_t(hi) << 32 | a;
            if (num / b <= 0xffffffffull) { exec(m, {0xF7, 0xF1}, a, b, hi, cf); EXPECT(m.cpu.reg[EAX] == uint32_t(num / b) && m.cpu.reg[EDX] == uint32_t(num % b), "div32 %x:%x/%x", hi, a, b); }
            else EXPECT(exec_fault(m, {0xF7, 0xF1}, a, b, hi, cf) == FaultKind::DivideError, "div32 overflow must fault");
            int64_t sn = int64_t(num), sd = int32_t(b);
            if (sn / sd >= INT32_MIN && sn / sd <= INT32_MAX) { exec(m, {0xF7, 0xF9}, a, b, hi, cf); EXPECT(m.cpu.reg[EAX] == uint32_t(sn / sd) && m.cpu.reg[EDX] == uint32_t(sn % sd), "idiv32 %x:%x/%x", hi, a, b); }
            else EXPECT(exec_fault(m, {0xF7, 0xF9}, a, b, hi, cf) == FaultKind::DivideError, "idiv32 overflow must fault");
        }
        alu_cases += 8 + 3;
    }
    EXPECT(exec_fault(m, {0xF7, 0xF1}, 1, 0, 0, false) == FaultKind::DivideError, "divide by zero must be an explicit DivideError");
    EXPECT(exec_fault(m, {0xF6, 0xF1}, 0x1234, 0, 0, false) == FaultKind::DivideError, "8-bit divide by zero must fault");
}

// ---------- (2) byte programs: control flow, addressing, strings, faults ----------
static void test_programs() {
    { Machine m;  // call/ret, push/pop, lea with SIB, mov via memory, loop with jcc, movzx/movsx, setcc, cmov
        // eax = 0; ecx = 5; loop: add eax, ecx; dec ecx; jnz loop      => 15
        m.load({0x31, 0xC0, 0xB9, 5, 0, 0, 0, 0x01, 0xC8, 0x49, 0x75, 0xFB});
        EXPECT(m.run_to_end(12) && m.cpu.reg[EAX] == 15 && m.cpu.reg[ECX] == 0, "loop gave %u", m.cpu.reg[EAX]); }
    { Machine m;  // call +N; ret; stack discipline: push 7; call f (f: mov eax,[esp+4]; ret 4)
        m.load({0x6A, 7, 0xE8, 1, 0, 0, 0, 0xF4 /*return lands here*/, 0x8B, 0x44, 0x24, 0x04, 0xC2, 4, 0});
        uint32_t esp0 = m.cpu.reg[ESP]; bool ok = false;
        try { m.cpu.run(0); } catch (const Fault& f) { ok = f.eip == Machine::CODE + 7; }
        EXPECT(ok && m.cpu.reg[EAX] == 7 && m.cpu.reg[ESP] == esp0, "stdcall ret 4: eax=%u esp delta=%d", m.cpu.reg[EAX], int(m.cpu.reg[ESP] - esp0)); }
    { Machine m;  // mov [DATA+8], 0x11223344; movzx/movsx/lea scaled index; bswap
        m.load({0xC7, 0x05, 0x08, 0x00, 0x02, 0x00, 0x44, 0x33, 0x22, 0x11,   /* mov dword [0x20008], imm */
                0x0F, 0xB6, 0x05, 0x0B, 0x00, 0x02, 0x00,                       /* movzx eax, byte [0x2000b] = 0x11 */
                0x0F, 0xBE, 0x0D, 0x08, 0x00, 0x02, 0x00,                       /* movsx ecx, byte [0x20008] = 0x44 */
                0xB8, 0x80, 0, 0, 0, 0x0F, 0xBE, 0xD0,                          /* mov eax,0x80; movsx edx, al = 0xffffff80 */
                0x8D, 0x1C, 0x95, 0x10, 0, 0, 0,                                 /* lea ebx,[edx*4+0x10] */
                0x0F, 0xC8});                                                    /* bswap eax */
        EXPECT(m.run_to_end(10 + 7 + 7 + 8 + 7 + 2) && m.mem.r32(0x20008) == 0x11223344 && m.cpu.reg[ECX] == 0x44 && m.cpu.reg[EDX] == 0xffffff80u &&
               m.cpu.reg[EBX] == 0xffffff80u * 4 + 0x10 && m.cpu.reg[EAX] == 0x80000000u, "mem/ext: ecx=%x edx=%x ebx=%x eax=%x", m.cpu.reg[ECX], m.cpu.reg[EDX], m.cpu.reg[EBX], m.cpu.reg[EAX]); }
    { Machine m;  // rep stosd (4 dwords), rep movsb, DF
        m.load({0xBF, 0x00, 0x00, 0x02, 0x00, 0xB8, 0xEF, 0xBE, 0xAD, 0xDE, 0xB9, 4, 0, 0, 0, 0xF3, 0xAB,
                0xBE, 0x00, 0x00, 0x02, 0x00, 0xBF, 0x40, 0x00, 0x02, 0x00, 0xB9, 16, 0, 0, 0, 0xF3, 0xA4});
        EXPECT(m.run_to_end(17 + 17) && m.mem.r32(0x20000) == 0xdeadbeefu && m.mem.r32(0x2000c) == 0xdeadbeefu && m.mem.r32(0x20040) == 0xdeadbeefu && m.mem.r32(0x2004c) == 0xdeadbeefu && m.cpu.reg[ECX] == 0, "rep string ops"); }
    { Machine m;  // fs: override reads through fs_base
        m.mem.w32(0x20100, 0xCAFEF00Du); m.cpu.fs_base = 0x20100;
        m.load({0x64, 0xA1, 0, 0, 0, 0}); EXPECT(m.run_to_end(6) && m.cpu.reg[EAX] == 0xCAFEF00Du, "fs:[0] must read fs_base"); }
    { Machine m;  // faults are explicit and carry the address/kind
        m.load({0xC7, 0x05, 0, 0, 0, 0, 1, 0, 0, 0});   // mov [0], 1
        try { m.cpu.run(0); EXPECT(false, "null write must fault"); } catch (const Fault& f) { EXPECT(f.kind == FaultKind::AccessViolation && f.addr == 0 && f.eip == Machine::CODE, "null write fault: %s", f.what()); } }
    { Machine m;  // W^X: code page not writable from the guest, data page not executable
        m.load({0xC6, 0x05, 0x00, 0x00, 0x01, 0x00, 0x90});   // mov byte [CODE], 0x90
        EXPECT(fault_of([&] { m.cpu.run(0); }) == FaultKind::AccessViolation, "write to RX page must fault");
        m.load({0xE9, 0xFB, 0xFF, 0x00, 0x00}); /* jmp CODE+0x10000-5+5 -> DATA region 0x20000 */
        m.cpu.eip = Machine::DATA; EXPECT(fault_of([&] { m.cpu.run(0); }) == FaultKind::ExecuteViolation, "execute from RW page must fault"); }
    { Machine m;  // unsupported instruction (x87) is an explicit fault naming the bytes, not a silent skip
        m.load({0xD9, 0xEE});
        try { m.cpu.run(0); EXPECT(false, "x87 must fault"); } catch (const Fault& f) { EXPECT(f.kind == FaultKind::Unsupported && std::string(f.what()).find("d9 ee") != std::string::npos, "x87 fault text: %s", f.what()); } }
    { Machine m; m.load({0xEB, 0xFE}); m.cpu.limit = 1000;   // jmp $
        EXPECT(fault_of([&] { m.cpu.run(0); }) == FaultKind::InstructionLimit, "runaway loop must hit the instruction limit"); }
    { Machine m; m.load({0xCC}); EXPECT(fault_of([&] { m.cpu.run(0); }) == FaultKind::Breakpoint, "int3"); }
    { Machine m; m.load({0x0F, 0x0B}); EXPECT(fault_of([&] { m.cpu.run(0); }) == FaultKind::InvalidOpcode, "ud2"); }
}

// ---------- (3) thunks, callbacks, nesting ----------
static void test_thunks() {
    Machine m; Host host; Registry reg; register_win32(reg, host);
    // Hand-built "image": two thunks installed directly. stdcall one-arg thunk that calls a guest stdcall callback twice.
    uint32_t cb_thunk = m.thunks.add(Thunk{"test.dll", "CallTwice", Conv::Stdcall, 2, [](Cpu& c) -> uint32_t {
        uint32_t fn = c.arg(0), v = c.arg(1); uint32_t a = c.call(fn, {v}), b = c.call(fn, {a}); return b; }});
    uint32_t cdecl_thunk = m.thunks.add(Thunk{"test.dll", "Sum3", Conv::Cdecl, 3, [](Cpu& c) -> uint32_t { return c.arg(0) + c.arg(1) + c.arg(2); }});
    uint32_t nest_thunk = m.thunks.add(Thunk{"test.dll", "Nest", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t { return c.call(c.arg(0), {5}) + 1; }});
    // thunk page must cover the added stubs
    Memory fresh; (void)fresh;
    m.mem.protect(m.thunks.base, 4096, PROT_R | PROT_W); std::vector<uint8_t> int3(4096, 0xCC); m.mem.write_bytes(m.thunks.base, int3.data(), 4096); m.mem.protect(m.thunks.base, 4096, PROT_R | PROT_X);
    // guest: callback f(x) = x*3+1 (stdcall, ret 4) at CODE+0x100
    std::vector<uint8_t> f{0x8B, 0x44, 0x24, 0x04, 0x6B, 0xC0, 0x03, 0x40, 0xC2, 0x04, 0x00};
    m.mem.protect(Machine::CODE, 4096, PROT_R | PROT_W); m.mem.write_bytes(Machine::CODE + 0x100, f.data(), uint32_t(f.size()));
    // main: push 2; push f; call [CallTwice]; (stdcall pops 8); push 3; push 2; push 1; call Sum3; add esp,12; hlt
    auto imm = [](uint32_t v) { return std::vector<uint8_t>{uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; };
    std::vector<uint8_t> code{0x6A, 2, 0x68}; auto fa = imm(Machine::CODE + 0x100); code.insert(code.end(), fa.begin(), fa.end());
    code.push_back(0xB9); auto t1 = imm(cb_thunk); code.insert(code.end(), t1.begin(), t1.end()); code.insert(code.end(), {0xFF, 0xD1});  // mov ecx,thunk; call ecx
    code.insert(code.end(), {0x89, 0xC3});  // mov ebx, eax (callee-saved survives)
    code.insert(code.end(), {0x6A, 3, 0x6A, 2, 0x6A, 1, 0xB9}); auto t2 = imm(cdecl_thunk); code.insert(code.end(), t2.begin(), t2.end());
    code.insert(code.end(), {0xFF, 0xD1, 0x83, 0xC4, 0x0C});  // call ecx; add esp,12
    code.insert(code.end(), {0x68}); auto fb = imm(Machine::CODE + 0x100); code.insert(code.end(), fb.begin(), fb.end());
    code.push_back(0xB9); auto t3 = imm(nest_thunk); code.insert(code.end(), t3.begin(), t3.end()); code.insert(code.end(), {0xFF, 0xD1});
    code.push_back(0xF4);
    m.mem.write_bytes(Machine::CODE, code.data(), uint32_t(code.size()));
    m.mem.protect(Machine::CODE, 4096, PROT_R | PROT_X);
    m.cpu.eip = Machine::CODE; m.cpu.reg[ESP] = Machine::STACK + 4096 - 16; uint32_t esp0 = m.cpu.reg[ESP]; m.cpu.reg[ESI] = 0x5151; 
    bool ok = false; try { m.cpu.run(0); } catch (const Fault& e) { ok = e.eip == Machine::CODE + code.size() - 1; }
    // f(2)=7, f(7)=22; Sum3=6; Nest -> f(5)=16, +1 = 17
    EXPECT(ok && m.cpu.reg[EBX] == 22 && m.cpu.reg[EAX] == 17 && m.cpu.reg[ESP] == esp0 && m.cpu.reg[ESI] == 0x5151, "thunks: ok=%d ebx=%u eax=%u esp delta=%d", ok, m.cpu.reg[EBX], m.cpu.reg[EAX], int(m.cpu.reg[ESP] - esp0));
    // execution inside the thunk page off a stub boundary is an explicit fault
    Machine m2; Thunk t{"x.dll", "X", Conv::Cdecl, 0, [](Cpu&) -> uint32_t { return 0; }}; m2.thunks.add(t); m2.load({0xB8, 0, 0, 0, 0}); m2.cpu.eip = m2.thunks.base + 3;
    EXPECT(fault_of([&] { m2.cpu.run(0); }) == FaultKind::BadThunk, "mid-stub execution must fault");
}

// ---------- (4) owned fixture PE ----------
static std::vector<uint8_t> slurp(const char* path) {
    std::ifstream f(path, std::ios::binary); if (!f) { printf("FAIL cannot open fixture %s\n", path); exit(2); }
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
}
struct RunResult { bool completed = false; uint32_t exit_code = 0xdead; std::string out; std::map<std::string, unsigned> calls; Image img; std::string fault; uint64_t executed = 0; };
static RunResult run_fixture(const std::vector<uint8_t>& file, uint32_t base, bool omit_strlen = false) {
    RunResult r; Memory mem; ThunkTable table; Host host; Registry reg; register_win32(reg, host);
    if (omit_strlen) reg.map.erase({"msvcrt.dll", "strlen"});
    Image img = load_pe32(mem, table, reg, file, base); map_thunk_page(mem, table);
    Cpu cpu(mem, table); init_process(mem, cpu, img);
    try { r.exit_code = run_entry(cpu, img); r.completed = true; } catch (const Fault& f) { r.fault = f.what(); }
    r.out = host.out; r.calls = host.calls; r.img = img; r.executed = cpu.executed; return r;
}
static void test_fixture(const char* path) {
    auto file = slurp(path);
    for (uint32_t base : {0u, 0x10000000u}) {
        RunResult r = run_fixture(file, base);
        printf("fixture base=0x%08x: exit=%u instructions=%llu relocs=%u imports=%u bound=%u\n", r.img.base, r.exit_code, (unsigned long long)r.executed, r.img.relocations, r.img.imports, r.img.bound);
        EXPECT(r.completed, "fixture faulted: %s", r.fault.c_str());
        EXPECT(r.exit_code == 42, "fixture exit code %u != 42; output:\n%s", r.exit_code, r.out.c_str());
        const char* want = "x86-fixture: start\nx86-fixture: teb/peb ok\ncrc=1632946162\nhello from 32-bit\nchecks=15\nfailed=0\n";
        EXPECT(r.out == want, "fixture stdout mismatch:\n%s", r.out.c_str());
        EXPECT(r.img.unresolved.empty() && r.img.bound == r.img.imports && r.img.imports == 7, "imports bound=%u of %u", r.img.bound, r.img.imports);
        EXPECT(r.calls["qsort"] == 2 && r.calls["ExitProcess"] == 1 && r.calls["WriteFile"] >= 6, "qsort=%u WriteFile=%u", r.calls["qsort"], r.calls["WriteFile"]);
        if (base) EXPECT(r.img.relocations > 0 && r.img.base == base, "relocated run must apply relocations");
    }
    RunResult miss = run_fixture(file, 0, true);   // an unresolved import is a named, explicit fault on first call
    EXPECT(!miss.completed && miss.fault.find("unresolved import msvcrt.dll!strlen") != std::string::npos, "unresolved import fault: '%s'", miss.fault.c_str());
}

int main(int argc, char** argv) {
    if (argc != 2) { printf("usage: x86-core <fixture.exe>\n"); return 2; }
    struct { const char* name; void (*fn)(); const char* pass; } suites[] = {
        {"alu", test_alu, "X86-CORE PASS: add/or/adc/sbb/and/sub/xor/cmp 8/16/32-bit results and flags match this machine's CPU"},
        {"shift", test_shift, "X86-CORE PASS: rol/ror/shl/shr/sar 8/16/32-bit count 0..33 results and defined flags match this machine's CPU"},
        {"unary", test_unary_muldiv, "X86-CORE PASS: inc/dec/neg/not, imul, mul/imul/div/idiv (incl. #DE) match this machine's CPU"},
        {"programs", test_programs, "X86-CORE PASS: control flow, addressing, string ops, FS base, W^X and explicit faults"},
        {"thunks", test_thunks, "X86-CORE PASS: stdcall/cdecl thunks, nested guest callbacks, callee-saved registers preserved"},
    };
    for (auto& s : suites) { int before = failures; s.fn(); if (failures == before) printf("%s\n", s.pass); else printf("X86-CORE FAIL: %s\n", s.name); }
    printf("(%llu hardware-compared cases)\n", (unsigned long long)alu_cases);
    int before = failures; test_fixture(argv[1]);
    if (failures == before) printf("X86-CORE PASS: owned i686 fixture PE ran to exit=42 at its ImageBase and relocated; unresolved import faults by name\n");
    else printf("X86-CORE FAIL: fixture\n");
    return failures ? 1 : 0;
}
