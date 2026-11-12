// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device tests for the x86 (PE32) execution core: (1) hardware-compared ALU
// semantics, (2) memory protections and explicit faults, (3) thunks and guest
// callbacks, (4) an owned i686 fixture PE run to a known exit code at its
// ImageBase and at a relocated base. Usage: x86-core <fixture.exe>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <immintrin.h>
#include "../host/x86/x86_win32.h"
#if !defined(__x86_64__) && !defined(__i386__)
#error "the ALU hardware comparison needs an x86 host"
#endif
using namespace x86;
static uint32_t f32b(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
static float bf32(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }
static uint64_t f64b(double d) { uint64_t v; memcpy(&v, &d, 8); return v; }
static double bf64(uint64_t v) { double d; memcpy(&d, &v, 8); return d; }
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
    { Machine m;  // unsupported instruction is an explicit fault naming the bytes, not a silent skip
        m.load({0x0F, 0x0A});   // ud1: an encoding the interpreter does not model
        try { m.cpu.run(0); EXPECT(false, "unsupported instruction must fault"); } catch (const Fault& f) { EXPECT(f.kind == FaultKind::Unsupported && std::string(f.what()).find("0f 0a") != std::string::npos, "unsupported fault text: %s", f.what()); } }
    { Machine m; m.load({0xEB, 0xFE}); m.cpu.limit = 1000;   // jmp $
        EXPECT(fault_of([&] { m.cpu.run(0); }) == FaultKind::InstructionLimit, "runaway loop must hit the instruction limit"); }
    { Machine m; m.load({0xCC}); EXPECT(fault_of([&] { m.cpu.run(0); }) == FaultKind::Breakpoint, "int3"); }
    { Machine m; m.load({0x0F, 0x0B}); EXPECT(fault_of([&] { m.cpu.run(0); }) == FaultKind::InvalidOpcode, "ud2"); }
}


// ---------- (2b) x87 and SSE/SSE2, compared with this machine's CPU ----------
// Each case runs the same instruction natively (inline asm) and in the
// interpreter, then compares the numeric result and the condition bits. x87
// comparison against the hardware is exact only where long double is the x87
// extended type (the Linux test host); elsewhere the suite still checks the
// interpreter's own documented model, and the PASS line says which ran.
namespace fp {
static double dvals[] = {0.0, 1.0, -1.0, 0.5, 2.5, 1e-300, 1e300, 3.141592653589793, -2.718281828459045, 1.0 / 3.0};
static float fvals[] = {0.0f, 1.0f, -1.0f, 0.5f, 2.5f, 1e-30f, 1e30f, 3.14159265f, -2.7182818f, 1.0f / 3.0f};
}
// Native x87 double-precision add/sub/mul/div of two doubles, plus the status
// word's condition bits, executed with the same operand order the interpreter sees.
// Native x87 arithmetic: operands and result go through file-scope globals so
// the asm block has exactly one memory output and no register pressure (a
// second "=r" output makes the compiler pick a register for the fld operand).
double g_x, g_y, g_out;
uint32_t g_sw;
static uint32_t nat_status() { return g_sw; }
static void nat_f64(char op, double a, double b, double& r) {
    g_x = a; g_y = b; g_out = 0;
    switch (op) {
    case '+': asm volatile("fldl %1\n\tfaddl %2\n\tfstpl %0\n\tfnstsw %%ax\n\tmovw %%ax, g_sw(%%rip)" : "+m"(g_out) : "m"(g_x), "m"(g_y) : "st", "ax", "memory"); break;
    case '-': asm volatile("fldl %1\n\tfsubl %2\n\tfstpl %0\n\tfnstsw %%ax\n\tmovw %%ax, g_sw(%%rip)" : "+m"(g_out) : "m"(g_x), "m"(g_y) : "st", "ax", "memory"); break;
    case '*': asm volatile("fldl %1\n\tfmull %2\n\tfstpl %0\n\tfnstsw %%ax\n\tmovw %%ax, g_sw(%%rip)" : "+m"(g_out) : "m"(g_x), "m"(g_y) : "st", "ax", "memory"); break;
    case '/': asm volatile("fldl %1\n\tfdivl %2\n\tfstpl %0\n\tfnstsw %%ax\n\tmovw %%ax, g_sw(%%rip)" : "+m"(g_out) : "m"(g_x), "m"(g_y) : "st", "ax", "memory"); break;
    }
    r = g_out;
    (void)nat_status();
}
// fcomip sets EFLAGS from the comparison; returns the flags the hardware produced.
static uint32_t nat_fcomip(double a, double b) {
    g_x = a; g_y = b;
    asm volatile("fldl %0\n\tfldl %1\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tpushfq\n\tpopq g_sw(%%rip)" : : "m"(g_x), "m"(g_y) : "st", "cc", "memory");
    return g_sw;
}
// The four integer-flag compares (fcomi/fucomi keep the stack, fcomip/fucomip
// pop st(0)): which = 0 fcomi, 1 fucomi, 2 fcomip, 3 fucomip.
static uint32_t nat_x87_cmp(int which, double a, double b) {
    g_x = a; g_y = b;
    switch (which) {
    case 0: asm volatile("fldl %0\n\tfldl %1\n\tfcomi %%st(1), %%st\n\tfstp %%st(0)\n\tfstp %%st(0)\n\tpushfq\n\tpopq g_sw(%%rip)" : : "m"(g_x), "m"(g_y) : "st", "cc", "memory"); break;
    case 1: asm volatile("fldl %0\n\tfldl %1\n\tfucomi %%st(1), %%st\n\tfstp %%st(0)\n\tfstp %%st(0)\n\tpushfq\n\tpopq g_sw(%%rip)" : : "m"(g_x), "m"(g_y) : "st", "cc", "memory"); break;
    case 2: asm volatile("fldl %0\n\tfldl %1\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tpushfq\n\tpopq g_sw(%%rip)" : : "m"(g_x), "m"(g_y) : "st", "cc", "memory"); break;
    default: asm volatile("fldl %0\n\tfldl %1\n\tfucomip %%st(1), %%st\n\tfstp %%st(0)\n\tpushfq\n\tpopq g_sw(%%rip)" : : "m"(g_x), "m"(g_y) : "st", "cc", "memory"); break;
    }
    return g_sw;
}
// fxam's class and sign bits. `empty` inspects the empty st(0) after fninit.
static uint32_t nat_fxam(double v, bool empty) {
    g_x = v;
    if (empty) asm volatile("fninit\n\tfxam\n\tfnstsw %%ax\n\tmovw %%ax, g_sw(%%rip)" : : : "st", "ax", "memory");
    else asm volatile("fninit\n\tfldl %0\n\tfxam\n\tfnstsw %%ax\n\tmovw %%ax, g_sw(%%rip)" : : "m"(g_x) : "st", "ax", "memory");
    return g_sw;
}
// fcmovCC st(0), st(1) on the hardware: returns the 8 bytes of the result
// (st(0) after the move, 1.0 when the condition did not hold). The integer
// flags are set from cf/pf/zf through pushf/popf.
static uint32_t nat_flags(bool cf, bool pf, bool zf) {
    uint32_t f; const uint32_t want = (cf ? uint32_t(CF) : 0u) | (pf ? uint32_t(PF) : 0u) | (zf ? uint32_t(ZF) : 0u);
    asm volatile("pushfq\n\tpopq %%rax\n\tandl $~0x45, %%eax\n\torl %1, %%eax\n\tpushq %%rax\n\tpopfq\n\tpushfq\n\tpopq %%rax\n\tmovl %%eax, %0" : "=r"(f) : "r"(want) : "ax", "cc", "memory");
    return f;
}
#define NAT_FCMOV(NAME, MN) \
    static uint64_t NAME() { asm volatile("fldl g_y(%%rip)\n\tfldl g_x(%%rip)\n\t" MN " %%st(1), %%st\n\tfstpl g_out(%%rip)\n\tfstp %%st(0)" : : : "st", "memory"); \
        uint64_t b; std::memcpy(&b, &g_out, 8); return b; }
NAT_FCMOV(nat_fcmovb, "fcmovb") NAT_FCMOV(nat_fcmove, "fcmove") NAT_FCMOV(nat_fcmovbe, "fcmovbe") NAT_FCMOV(nat_fcmovu, "fcmovu")
NAT_FCMOV(nat_fcmovnb, "fcmovnb") NAT_FCMOV(nat_fcmovne, "fcmovne") NAT_FCMOV(nat_fcmovnbe, "fcmovnbe") NAT_FCMOV(nat_fcmovnu, "fcmovnu")
static uint64_t nat_fcmov(int which, bool cf, bool pf, bool zf) {
    g_x = 1.0; g_y = 2.0; nat_flags(cf, pf, zf);
    static uint64_t (*const fns[8])() = {nat_fcmovb, nat_fcmove, nat_fcmovbe, nat_fcmovu, nat_fcmovnb, nat_fcmovne, nat_fcmovnbe, nat_fcmovnu};
    return fns[which]();
}
static void test_fpu() {
    Machine m;
    // x87 double-precision arithmetic against the real FPU. The result is
    // compared bit-for-bit: both sides do one IEEE operation in the same
    // precision, so any difference is an interpreter bug.
    for (char op : {'+', '-', '*', '/'}) for (double a : fp::dvals) for (double b : fp::dvals) {
        if (op == '/' && b == 0) continue;
        double nr; nat_f64(op, a, b, nr);
        // fldl [DATA]; faddl [DATA+8]; fstpl [DATA+16]  (opcode chosen per operation)
        uint8_t opc = op == '+' ? 0x00 : op == '-' ? 0x20 : op == '*' ? 0x08 : 0x30;
        m.mem.protect(Machine::DATA, 4096, PROT_R | PROT_W);
        m.mem.w64(Machine::DATA, f64b(a)); m.mem.w64(Machine::DATA + 8, f64b(b));
        std::vector<uint8_t> enc{0xDD, 0x05, 0x00, 0x00, 0x02, 0x00};  // fld qword [DATA]
        enc.insert(enc.end(), {0xDC, uint8_t(opc | 0x05), 0x08, 0x00, 0x02, 0x00});  // op qword [DATA+8]
        enc.insert(enc.end(), {0xDD, 0x1D, 0x10, 0x00, 0x02, 0x00});  // fstp qword [DATA+16]
        exec(m, enc, 0, 0, 0, false);
        uint64_t gotbits = m.mem.r64(Machine::DATA + 16); double got; memcpy(&got, &gotbits, 8);
        EXPECT(f64b(got) == f64b(nr), "x87 %c %g %g: got %.17g want %.17g", op, a, b, got, nr);
        EXPECT(m.cpu.fpu.top == 0 && m.cpu.fpu.empty[m.cpu.fpu.idx(0)], "x87 %c: stack not empty after fstp", op);
        ++alu_cases;
    }
    // fcomip: the integer flags must match the hardware's, for ordered and unordered pairs.
    for (double a : fp::dvals) for (double b : fp::dvals) {
        uint32_t nf = nat_fcomip(a, b);
        m.mem.w64(Machine::DATA, f64b(a)); m.mem.w64(Machine::DATA + 8, f64b(b));
        // fldl [DATA]; fldl [DATA+8]; fcomip st, st(1); fstp st(0)
        std::vector<uint8_t> enc{0xDD, 0x05, 0x00, 0x00, 0x02, 0x00, 0xDD, 0x05, 0x08, 0x00, 0x02, 0x00, 0xDF, 0xF1, 0xDD, 0xD8};
        exec(m, enc, 0, 0, 0, false);
        EXPECT(!((m.cpu.eflags ^ nf) & (CF | PF | ZF)), "fcomip %g %g: flags got %x want %x", a, b, m.cpu.eflags & (CF | PF | ZF), nf & (CF | PF | ZF));
        ++alu_cases;
    }
    // SSE scalar double add/sub/mul/div, compared bit for bit.
    for (char op : {'+', '-', '*', '/'}) for (double a : fp::dvals) for (double b : fp::dvals) {
        if (op == '/' && b == 0) continue;
        double want = op == '+' ? a + b : op == '-' ? a - b : op == '*' ? a * b : a / b;
        m.mem.w64(Machine::DATA, f64b(a)); m.mem.w64(Machine::DATA + 8, f64b(b));
        uint8_t opc = op == '+' ? 0x58 : op == '-' ? 0x5C : op == '*' ? 0x59 : 0x5E;
        std::vector<uint8_t> enc{0xF2, 0x0F, 0x10, 0x05, 0x00, 0x00, 0x02, 0x00};  // movsd xmm0, [DATA]
        enc.insert(enc.end(), {0xF2, 0x0F, opc, 0x05, 0x08, 0x00, 0x02, 0x00});    // op sd xmm0, [DATA+8]
        enc.insert(enc.end(), {0xF2, 0x0F, 0x11, 0x05, 0x10, 0x00, 0x02, 0x00});  // movsd [DATA+16], xmm0
        exec(m, enc, 0, 0, 0, false);
        EXPECT(m.mem.r64(Machine::DATA + 16) == f64b(want), "sse %c %g %g: got %.17g want %.17g", op, a, b, bf64(m.mem.r64(Machine::DATA + 16)), want);
        ++alu_cases;
    }
    // SSE scalar single arithmetic and the int<->float conversions.
    for (char op : {'+', '-', '*', '/'}) for (float a : fp::fvals) for (float b : fp::fvals) {
        if (op == '/' && b == 0) continue;
        float want = op == '+' ? a + b : op == '-' ? a - b : op == '*' ? a * b : a / b;
        m.mem.w32(Machine::DATA, f32b(a)); m.mem.w32(Machine::DATA + 4, f32b(b));
        uint8_t opc = op == '+' ? 0x58 : op == '-' ? 0x5C : op == '*' ? 0x59 : 0x5E;
        std::vector<uint8_t> enc{0xF3, 0x0F, 0x10, 0x05, 0x00, 0x00, 0x02, 0x00};
        enc.insert(enc.end(), {0xF3, 0x0F, opc, 0x05, 0x04, 0x00, 0x02, 0x00});
        enc.insert(enc.end(), {0xF3, 0x0F, 0x11, 0x05, 0x08, 0x00, 0x02, 0x00});
        exec(m, enc, 0, 0, 0, false);
        EXPECT(m.mem.r32(Machine::DATA + 8) == f32b(want), "sse ss %c %g %g: got %g want %g", op, double(a), double(b), double(bf32(m.mem.r32(Machine::DATA + 8))), double(want));
        ++alu_cases;
    }
    for (int32_t v : {0, 1, -1, 12345, -12345, 0x7fffffff, int32_t(0x80000000u)}) {
        // cvtsi2sd xmm0, eax; movsd [DATA], xmm0
        exec(m, {0xF2, 0x0F, 0x2A, 0xC0, 0xF2, 0x0F, 0x11, 0x05, 0x00, 0x00, 0x02, 0x00}, uint32_t(v), 0, 0, false);
        EXPECT(m.mem.r64(Machine::DATA) == f64b(double(v)), "cvtsi2sd %d", v);
        // cvtsi2ss xmm0, eax; movss [DATA], xmm0
        exec(m, {0xF3, 0x0F, 0x2A, 0xC0, 0xF3, 0x0F, 0x11, 0x05, 0x00, 0x00, 0x02, 0x00}, uint32_t(v), 0, 0, false);
        EXPECT(m.mem.r32(Machine::DATA) == f32b(float(v)), "cvtsi2ss %d", v);
        // cvttsd2si eax, [DATA]  (value v as a double)
        m.mem.w64(Machine::DATA, f64b(double(v)));
        exec(m, {0xF2, 0x0F, 0x2C, 0x05, 0x00, 0x00, 0x02, 0x00}, 0, 0, 0, false);
        EXPECT(m.cpu.reg[EAX] == uint32_t(v), "cvttsd2si %d: got %d", v, int32_t(m.cpu.reg[EAX]));
        ++alu_cases;
    }
    // SSE2 packed integer: paddd/psubd/pcmpeqd/pand/por/pxor and the shifts.
    m.mem.w128(Machine::DATA, std::array<uint32_t, 4>{1, 2, 3, 0xffffffffu}.data());
    m.mem.w128(Machine::DATA + 16, std::array<uint32_t, 4>{10, 20, 30, 1}.data());
    // movdqa xmm0,[DATA]; paddd xmm0,[DATA+16]; movdqa [DATA+32],xmm0
    exec(m, {0x66, 0x0F, 0x6F, 0x05, 0x00, 0x00, 0x02, 0x00, 0x66, 0x0F, 0xFE, 0x05, 0x10, 0x00, 0x02, 0x00,
             0x66, 0x0F, 0x7F, 0x05, 0x20, 0x00, 0x02, 0x00}, 0, 0, 0, false);
    uint32_t out[4]; m.mem.r128(Machine::DATA + 32, out);
    EXPECT(out[0] == 11 && out[1] == 22 && out[2] == 33 && out[3] == 0, "paddd: %u %u %u %u", out[0], out[1], out[2], out[3]);
    exec(m, {0x66, 0x0F, 0x6F, 0x05, 0x00, 0x00, 0x02, 0x00, 0x66, 0x0F, 0xFA, 0x05, 0x10, 0x00, 0x02, 0x00,
             0x66, 0x0F, 0x7F, 0x05, 0x20, 0x00, 0x02, 0x00}, 0, 0, 0, false);
    m.mem.r128(Machine::DATA + 32, out);
    EXPECT(out[0] == 0xfffffff7u && out[1] == 0xffffffeeu && out[2] == 0xffffffe5u && out[3] == 0xfffffffeu, "psubd: %x %x %x %x", out[0], out[1], out[2], out[3]);
    exec(m, {0x66, 0x0F, 0x6F, 0x05, 0x00, 0x00, 0x02, 0x00, 0x66, 0x0F, 0x76, 0x05, 0x10, 0x00, 0x02, 0x00,
             0x66, 0x0F, 0xD7, 0xC1}, 0, 0, 0, false);  // pcmpeqd xmm0,[DATA+16]; pmovmskb eax,xmm0
    EXPECT(m.cpu.reg[EAX] == 0, "pcmpeqd/pmovmskb: %x", m.cpu.reg[EAX]);
    ++alu_cases;
}
// x87 memory forms and the stack discipline the CRT depends on.
static void test_x87_forms() {
    Machine m;
    auto run = [&](std::initializer_list<uint8_t> code) { exec(m, std::vector<uint8_t>(code), 0, 0, 0, false); };
    // fild dword [DATA]; fistp dword [DATA+4]  (round to nearest: 7 stays 7, -7 stays -7)
    for (int32_t v : {0, 1, -1, 7, -7, 100000, -100000}) {
        m.mem.w32(Machine::DATA, uint32_t(v));
        run({0xDB, 0x05, 0x00, 0x00, 0x02, 0x00, 0xDB, 0x1D, 0x04, 0x00, 0x02, 0x00});
        EXPECT(int32_t(m.mem.r32(Machine::DATA + 4)) == v, "fild/fistp %d -> %d", v, int32_t(m.mem.r32(Machine::DATA + 4)));
    }
    // fld qword; fld qword; fxch; fstp qword; fstp qword: the two values come back swapped
    m.mem.w64(Machine::DATA, f64b(1.5)); m.mem.w64(Machine::DATA + 8, f64b(2.5));
    run({0xDD, 0x05, 0x00, 0x00, 0x02, 0x00, 0xDD, 0x05, 0x08, 0x00, 0x02, 0x00, 0xD9, 0xC9,
         0xDD, 0x1D, 0x10, 0x00, 0x02, 0x00, 0xDD, 0x1D, 0x18, 0x00, 0x02, 0x00});
    EXPECT(bf64(m.mem.r64(Machine::DATA + 16)) == 1.5 && bf64(m.mem.r64(Machine::DATA + 24)) == 2.5, "fxch: %g %g", bf64(m.mem.r64(Machine::DATA + 16)), bf64(m.mem.r64(Machine::DATA + 24)));
    // fldcw/fnstcw round trip, and fnstsw ax
    m.mem.w(Machine::DATA, 0x0f7f, 2); run({0xD9, 0x2D, 0x00, 0x00, 0x02, 0x00, 0xD9, 0x3D, 0x04, 0x00, 0x02, 0x00});
    EXPECT(m.mem.r(Machine::DATA + 4, 2) == 0x0f7f, "fldcw/fnstcw: %x", m.mem.r(Machine::DATA + 4, 2));
    run({0xDF, 0xE0});  EXPECT((m.cpu.reg[EAX] & 0xffff) == m.cpu.fpu.status(), "fnstsw ax");
    // faddp/fmulp/fsubp/fdivp: st(1) = st(1) op st(0), then pop
    auto pair = [&](std::initializer_list<uint8_t> op, double a, double b, double want) {
        m.mem.w64(Machine::DATA, f64b(a)); m.mem.w64(Machine::DATA + 8, f64b(b));
        std::vector<uint8_t> enc{0xDD, 0x05, 0x00, 0x00, 0x02, 0x00, 0xDD, 0x05, 0x08, 0x00, 0x02, 0x00};
        enc.insert(enc.end(), op);
        enc.insert(enc.end(), {0xDD, 0x1D, 0x10, 0x00, 0x02, 0x00});
        exec(m, enc, 0, 0, 0, false);
        EXPECT(bf64(m.mem.r64(Machine::DATA + 16)) == want && m.cpu.fpu.top == 0, "x87 pair: got %.17g want %.17g", bf64(m.mem.r64(Machine::DATA + 16)), want);
    };
    pair({0xDE, 0xC1}, 2.0, 3.0, 5.0);    // faddp
    pair({0xDE, 0xC9}, 2.0, 3.0, 6.0);    // fmulp
    pair({0xDE, 0xE1}, 2.0, 3.0, 1.0);    // st(1) = st(0) - st(1) = 3-2
    pair({0xDE, 0xE9}, 2.0, 3.0, -1.0);   // st(1) = st(1) - st(0) = 2-3
    pair({0xDE, 0xF1}, 6.0, 3.0, 0.5);    // st(1) = st(0) / st(1) = 3/6
    pair({0xDE, 0xF9}, 6.0, 3.0, 2.0);    // st(1) = st(1) / st(0) = 6/3
    // The DC register form is the reversed encoding (DC E0+i): st(i) is the
    // destination and st(0) is the left operand for the non-commutative ops.
    // Values below were read off the hardware, not from the mnemonic names.
    auto dcreg = [&](std::initializer_list<uint8_t> op, double a, double b, double want) {
        m.mem.w64(Machine::DATA, f64b(a)); m.mem.w64(Machine::DATA + 8, f64b(b));
        std::vector<uint8_t> enc{0xDD, 0x05, 0x00, 0x00, 0x02, 0x00, 0xDD, 0x05, 0x08, 0x00, 0x02, 0x00};
        enc.insert(enc.end(), op);
        enc.insert(enc.end(), {0xDD, 0xD8});                            // fstp st(0): drop the source
        enc.insert(enc.end(), {0xDD, 0x1D, 0x10, 0x00, 0x02, 0x00});    // fstp qword [DATA+16]: the result
        exec(m, enc, 0, 0, 0, false);
        EXPECT(bf64(m.mem.r64(Machine::DATA + 16)) == want, "x87 dc reg: got %.17g want %.17g", bf64(m.mem.r64(Machine::DATA + 16)), want);
    };
    dcreg({0xDC, 0xE1}, 2.0, 8.0, 6.0);   // st(1) = st(0) - st(1)
    dcreg({0xDC, 0xE9}, 2.0, 8.0, -6.0);  // st(1) = st(1) - st(0)
    dcreg({0xDC, 0xF1}, 2.0, 8.0, 4.0);   // st(1) = st(0) / st(1)
    dcreg({0xDC, 0xF9}, 2.0, 8.0, 0.25);  // st(1) = st(1) / st(0)
    // fld1/fldz/fchs/fabs/frndint
    run({0xD9, 0xE8, 0xD9, 0xE0, 0xDD, 0x1D, 0x00, 0x00, 0x02, 0x00});  // fld1; fchs; fstp [DATA]
    EXPECT(bf64(m.mem.r64(Machine::DATA)) == -1.0, "fld1/fchs");
    run({0xD9, 0xE8, 0xD9, 0xE1, 0xDD, 0x1D, 0x00, 0x00, 0x02, 0x00});  // fld1; fabs; fstp
    EXPECT(bf64(m.mem.r64(Machine::DATA)) == 1.0, "fabs");
    // 80-bit round trip: fld m80; fstp m80
    { uint8_t b[10]; Fpu::from_wide(b, Wide(1.0L) / 3.0L); for (int i = 0; i < 10; ++i) m.mem.w8(Machine::DATA + 32 + i, b[i]); }
    run({0xDB, 0x2D, 0x20, 0x00, 0x02, 0x00, 0xDB, 0x3D, 0x40, 0x00, 0x02, 0x00});
    { uint8_t b[10]; for (int i = 0; i < 10; ++i) b[i] = m.mem.r8(Machine::DATA + 64 + i);
      EXPECT(Fpu::to_wide(b) == Wide(1.0L) / 3.0L, "fld/fstp m80 must round-trip the extended value"); }
    // An x87 encoding outside the modelled set is an explicit fault, not a guess.
    EXPECT(fault_of([&] { m.load({0xDF, 0x20}); m.cpu.run(0); }) == FaultKind::Unsupported, "fbld must fault");
    // fxam: the class bits (C0/C2/C3) and the sign bit (C1) the mingw FP formatter
    // branches on, for every class of value, against the hardware.
    {
        double nan = std::numeric_limits<double>::quiet_NaN();
        double inf = std::numeric_limits<double>::infinity();
        double denorm = std::numeric_limits<double>::denorm_min();
        for (double v : {3.14159, -3.14159, 0.0, -0.0, inf, -inf, nan, denorm, -denorm, 1e300, -1e-300}) {
            uint32_t want = nat_fxam(v, false);
            m.mem.w64(Machine::DATA, f64b(v));
            run({0xDD, 0x05, 0x00, 0x00, 0x02, 0x00, 0xD9, 0xE5, 0xDF, 0xE0, 0xDD, 0xD8});  // fldl; fxam; fnstsw ax; fstp st(0)
            EXPECT((m.cpu.reg[EAX] & 0x4700) == (want & 0x4700) && (m.cpu.reg[EAX] & 0x0200) == (want & 0x0200),
                   "fxam %g: class %x sign %x, want %x/%x", v, m.cpu.reg[EAX] & 0x4700, m.cpu.reg[EAX] & 0x0200, want & 0x4700, want & 0x0200);
            ++alu_cases;
        }
        uint32_t want = nat_fxam(0, true);
        run({0xD9, 0xE5, 0xDF, 0xE0});  // fxam on the empty stack; fnstsw ax
        EXPECT((m.cpu.reg[EAX] & 0x4700) == (want & 0x4700), "fxam empty: %x want %x", m.cpu.reg[EAX] & 0x4700, want & 0x4700);
        ++alu_cases;
    }
    // fcomi/fucomi/fcomip/fucomip: the integer flags and the stack discipline.
    // (DB F0+i / DB E8+i keep st(0); DF F0+i / DF E8+i pop it.)
    {
        double nan = std::numeric_limits<double>::quiet_NaN();
        const uint8_t enc[4] = {0xDB, 0xF1, 0xDB, 0xE9};  // fcomi/fucomi st, st(1)
        const uint8_t pop[4] = {0xDF, 0xF1, 0xDF, 0xE9};  // fcomip/fucomip st, st(1)
        for (int which = 0; which < 4; ++which) for (double a : {1.0, -1.0, 0.0}) for (double b : {1.0, -1.0, 0.0, nan}) {
            uint32_t nf = nat_x87_cmp(which, a, b);
            m.mem.w64(Machine::DATA, f64b(a)); m.mem.w64(Machine::DATA + 8, f64b(b));
            std::vector<uint8_t> code{0xDD, 0x05, 0x00, 0x00, 0x02, 0x00, 0xDD, 0x05, 0x08, 0x00, 0x02, 0x00};
            code.push_back(which < 2 ? enc[which * 2] : pop[(which - 2) * 2]); code.push_back(which < 2 ? enc[which * 2 + 1] : pop[(which - 2) * 2 + 1]);
            if (which < 2) code.insert(code.end(), {0xDD, 0xD8, 0xDD, 0xD8});  // keep: drop both explicitly
            else code.push_back(0xDD), code.push_back(0xD8);                    // pop: drop the remaining one
            exec(m, code, 0, 0, 0, false);
            EXPECT(!((m.cpu.eflags ^ nf) & (CF | PF | ZF)), "x87 cmp %d %g %g: flags got %x want %x", which, a, b, m.cpu.eflags & (CF | PF | ZF), nf & (CF | PF | ZF));
            EXPECT(m.cpu.fpu.top == 0 && m.cpu.fpu.empty[m.cpu.fpu.idx(0)], "x87 cmp %d: stack not empty", which);
            ++alu_cases;
        }
    }
    // fcmov: the condition comes from the integer flags, and st(0) takes st(1)'s
    // value only when it holds. DA C0+i b/e/be/u, DB C0+i nb/ne/nbe/nu.
    {
        const char* mn[8] = {"fcmovb", "fcmove", "fcmovbe", "fcmovu", "fcmovnb", "fcmovne", "fcmovnbe", "fcmovnu"};
        for (int which = 0; which < 8; ++which) for (int fl = 0; fl < 8; ++fl) {
            bool cf = fl & 1, pf = fl & 2, zf = fl & 4;
            uint64_t nf = nat_fcmov(which, cf, pf, zf);
            m.mem.w64(Machine::DATA, f64b(1.0)); m.mem.w64(Machine::DATA + 8, f64b(2.0));
            // fldl [DATA+8] (st1 = 2.0); fldl [DATA] (st0 = 1.0); fcmovCC st(1); fstpl [DATA+16]; fstp st(0)
            std::vector<uint8_t> code{0xDD, 0x05, 0x08, 0x00, 0x02, 0x00, 0xDD, 0x05, 0x00, 0x00, 0x02, 0x00,
                                      uint8_t(which < 4 ? 0xDA : 0xDB), uint8_t(0xC1 | ((which & 3) << 3)),
                                      0xDD, 0x1D, 0x10, 0x00, 0x02, 0x00, 0xDD, 0xD8};
            m.mem.protect(Machine::CODE, 4096, PROT_R | PROT_W);
            code.push_back(0xF4);
            m.mem.write_bytes(Machine::CODE, code.data(), uint32_t(code.size())); m.mem.protect(Machine::CODE, 4096, PROT_R | PROT_X);
            m.cpu.eip = Machine::CODE; m.cpu.reg[ESP] = Machine::STACK + 4096 - 16; m.cpu.exited = false; m.cpu.depth = 0;
            m.cpu.eflags = 2u | (cf ? uint32_t(CF) : 0u) | (pf ? uint32_t(PF) : 0u) | (zf ? uint32_t(ZF) : 0u);
            EXPECT(m.run_to_end(code.size() - 1), "fcmov program did not reach end");
            EXPECT(m.mem.r64(Machine::DATA + 16) == nf, "%s cf=%d pf=%d zf=%d: got %.17g want %.17g", mn[which], cf, pf, zf, bf64(m.mem.r64(Machine::DATA + 16)), bf64(nf));
            ++alu_cases;
        }
    }
}

// ---------- (2c) regressions the review found: each compared with the hardware ----------
// Every case here was a wrong operand, a wrong lane order, a missing write or a
// wrong stack order. The native side runs the same encoding, so a future
// change that reintroduces any of them fails against this CPU, not a guess.
static void test_sse_regressions() {
    Machine m;
    auto run = [&](std::initializer_list<uint8_t> code) { exec(m, std::vector<uint8_t>(code), 0, 0, 0, false); };
    // bt/bts/btr/btc r/m32, imm8: the bit index is the immediate.
    auto bt_case = [&](unsigned bit) {
        // Values chosen so the encoded bit differs from bit 0 of EAX/ECX: with
        // the old register-index bug the two disagree and this fails.
        // `which` selects the hardware form (immediate 0, 4 or 31); the switch
        // below is keyed on it, not on the bit value itself.
        const unsigned which = bit == 0 ? 0 : bit == 4 ? 1 : 2;
        for (uint32_t v : {0x00000001u, 0x00000010u, 0x80000000u, 0x7fffffffu, 0xffffffffu, 0u}) {
            for (int act = 0; act < 4; ++act) {
                uint32_t nv = v; unsigned char ncf = 0;
                switch (which * 4 + act) {
                case 0: asm volatile("movl %2, %%eax\n\tbtl $0, %%eax\n\tsetc %1\n\tmovl %%eax, %0" : "=r"(nv), "=r"(ncf) : "r"(v) : "eax", "cc"); break;
                case 4: asm volatile("movl %2, %%eax\n\tbtl $4, %%eax\n\tsetc %1\n\tmovl %%eax, %0" : "=r"(nv), "=r"(ncf) : "r"(v) : "eax", "cc"); break;
                case 8: asm volatile("movl %2, %%eax\n\tbtl $31, %%eax\n\tsetc %1\n\tmovl %%eax, %0" : "=r"(nv), "=r"(ncf) : "r"(v) : "eax", "cc"); break;
                case 1: asm volatile("movl %2, %%eax\n\tbtsl $0, %%eax\n\tsetc %1\n\tmovl %%eax, %0" : "=r"(nv), "=r"(ncf) : "r"(v) : "eax", "cc"); break;
                case 5: asm volatile("movl %2, %%eax\n\tbtsl $4, %%eax\n\tsetc %1\n\tmovl %%eax, %0" : "=r"(nv), "=r"(ncf) : "r"(v) : "eax", "cc"); break;
                case 9: asm volatile("movl %2, %%eax\n\tbtsl $31, %%eax\n\tsetc %1\n\tmovl %%eax, %0" : "=r"(nv), "=r"(ncf) : "r"(v) : "eax", "cc"); break;
                case 2: asm volatile("movl %2, %%eax\n\tbtrl $0, %%eax\n\tsetc %1\n\tmovl %%eax, %0" : "=r"(nv), "=r"(ncf) : "r"(v) : "eax", "cc"); break;
                case 6: asm volatile("movl %2, %%eax\n\tbtrl $4, %%eax\n\tsetc %1\n\tmovl %%eax, %0" : "=r"(nv), "=r"(ncf) : "r"(v) : "eax", "cc"); break;
                case 10: asm volatile("movl %2, %%eax\n\tbtrl $31, %%eax\n\tsetc %1\n\tmovl %%eax, %0" : "=r"(nv), "=r"(ncf) : "r"(v) : "eax", "cc"); break;
                case 3: asm volatile("movl %2, %%eax\n\tbtcl $0, %%eax\n\tsetc %1\n\tmovl %%eax, %0" : "=r"(nv), "=r"(ncf) : "r"(v) : "eax", "cc"); break;
                case 7: asm volatile("movl %2, %%eax\n\tbtcl $4, %%eax\n\tsetc %1\n\tmovl %%eax, %0" : "=r"(nv), "=r"(ncf) : "r"(v) : "eax", "cc"); break;
                case 11: asm volatile("movl %2, %%eax\n\tbtcl $31, %%eax\n\tsetc %1\n\tmovl %%eax, %0" : "=r"(nv), "=r"(ncf) : "r"(v) : "eax", "cc"); break;
                default: continue;
                }
                // 0F BA /act with modrm C0 (eax) and the immediate bit
                exec(m, {0x0F, 0xBA, uint8_t(0xC0 | (act << 3)), uint8_t(bit == 0 ? 0 : bit == 4 ? 4 : 31)}, v, 0, 0, false);
                EXPECT(m.cpu.reg[EAX] == nv && bool(m.cpu.eflags & CF) == bool(ncf), "bt imm act=%d bit=%u v=%08x: got %08x cf=%d want %08x cf=%d",
                       act, bit, v, m.cpu.reg[EAX], int((m.cpu.eflags & CF) != 0), nv, int(ncf != 0));
                ++alu_cases;
            }
        }
    };
    bt_case(0); bt_case(4); bt_case(31);
    // movd xmm, eax zeroes the upper 96 bits; movd eax, xmm reads the low dword.
    run({0x66, 0x0F, 0x6E, 0xC0});  // movd xmm0, eax (xmm0 preset below)
    m.mem.w128(Machine::DATA, std::array<uint32_t, 4>{0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}.data());
    m.mem.protect(Machine::CODE, 4096, PROT_R | PROT_W);
    // movdqa xmm0,[DATA]; movd xmm0,eax; movdqa [DATA+16],xmm0
    std::vector<uint8_t> c{0x66, 0x0F, 0x6F, 0x05, 0x00, 0x00, 0x02, 0x00, 0x66, 0x0F, 0x6E, 0xC0,
                           0x66, 0x0F, 0x7F, 0x05, 0x10, 0x00, 0x02, 0x00};
    exec(m, c, 0x12345678u, 0, 0, false);
    uint32_t out[4]; m.mem.r128(Machine::DATA + 16, out);
    EXPECT(out[0] == 0x12345678u && out[1] == 0 && out[2] == 0 && out[3] == 0, "movd xmm,eax must zero the upper lanes: %08x %08x %08x %08x", out[0], out[1], out[2], out[3]);
    // movd eax, xmm1 (66 0F 7E C8): a GP destination, not an XMM slot.
    m.mem.w128(Machine::DATA, std::array<uint32_t, 4>{0xAAAAAAAAu, 0xBBBBBBBBu, 0xCCCCCCCCu, 0xDDDDDDDDu}.data());
    exec(m, {0x66, 0x0F, 0x6F, 0x0D, 0x00, 0x00, 0x02, 0x00, 0x66, 0x0F, 0x7E, 0xC8}, 0, 0, 0, false);
    EXPECT(m.cpu.reg[EAX] == 0xAAAAAAAAu, "movd eax,xmm1: got %08x", m.cpu.reg[EAX]);
    // movq xmm0, xmm1 (F3 0F 7E C1) zeroes the upper 64 bits.
    m.mem.w128(Machine::DATA, std::array<uint32_t, 4>{0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}.data());
    m.mem.w128(Machine::DATA + 16, std::array<uint32_t, 4>{0xAAAAAAAAu, 0xBBBBBBBBu, 0xCCCCCCCCu, 0xDDDDDDDDu}.data());
    exec(m, {0x66, 0x0F, 0x6F, 0x05, 0x00, 0x00, 0x02, 0x00, 0x66, 0x0F, 0x6F, 0x0D, 0x10, 0x00, 0x02, 0x00,
             0xF3, 0x0F, 0x7E, 0xC1, 0x66, 0x0F, 0x7F, 0x05, 0x20, 0x00, 0x02, 0x00}, 0, 0, 0, false);
    m.mem.r128(Machine::DATA + 32, out);
    EXPECT(out[0] == 0xAAAAAAAAu && out[1] == 0xBBBBBBBBu && out[2] == 0 && out[3] == 0, "movq xmm0,xmm1 must zero the upper half: %08x %08x %08x %08x", out[0], out[1], out[2], out[3]);
    // movmskps eax, xmm1: the source is the r/m register.
    m.mem.w128(Machine::DATA, std::array<uint32_t, 4>{0x80000000u, 0x7fffffffu, 0xFFFFFFFFu, 0x00000000u}.data());
    exec(m, {0x0F, 0x28, 0x0D, 0x00, 0x00, 0x02, 0x00, 0x0F, 0x50, 0xC1}, 0xdeadbeefu, 0, 0, false);
    EXPECT(m.cpu.reg[EAX] == 0x5u, "movmskps eax,xmm1: got %x want 5", m.cpu.reg[EAX]);
    // pmovmskb eax, xmm1 (66 0F D7 C1).
    m.mem.w128(Machine::DATA, std::array<uint32_t, 4>{0x80808080u, 0x01010101u, 0xffffffffu, 0u}.data());
    exec(m, {0x66, 0x0F, 0x6F, 0x0D, 0x00, 0x00, 0x02, 0x00, 0x66, 0x0F, 0xD7, 0xC1}, 0, 0, 0, false);
    EXPECT(m.cpu.reg[EAX] == 0x0f0fu, "pmovmskb eax,xmm1: got %x want 0f0f", m.cpu.reg[EAX]);
    // sqrtss xmm0, xmm1 uses the source and keeps the destination's upper lanes.
    // (movups, not movss: movss zeroes the lanes it does not load.)
    m.mem.w32(Machine::DATA, f32b(4.0f)); m.mem.w32(Machine::DATA + 4, f32b(9.0f));
    m.mem.w32(Machine::DATA + 8, f32b(16.0f)); m.mem.w32(Machine::DATA + 12, f32b(25.0f));
    exec(m, {0x0F, 0x10, 0x05, 0x00, 0x00, 0x02, 0x00, 0x0F, 0x10, 0x0D, 0x08, 0x00, 0x02, 0x00,
             0xF3, 0x0F, 0x51, 0xC1, 0x0F, 0x11, 0x05, 0x10, 0x00, 0x02, 0x00}, 0, 0, 0, false);
    EXPECT(m.mem.r32(Machine::DATA + 16) == f32b(4.0f) && m.mem.r32(Machine::DATA + 20) == f32b(9.0f),
           "sqrtss xmm0(4),xmm1(16): got %g %g", double(bf32(m.mem.r32(Machine::DATA + 16))), double(bf32(m.mem.r32(Machine::DATA + 20))));
    // rsqrtss/rcpss (F3 0F 52/53) are scalar too: one lane from the source, the
    // destination's upper lanes kept. A memory source is 4 bytes, so reading
    // four lanes would fault past the operand.
    m.mem.w32(Machine::DATA, f32b(4.0f)); m.mem.w32(Machine::DATA + 4, f32b(9.0f));
    m.mem.w32(Machine::DATA + 8, f32b(16.0f)); m.mem.w32(Machine::DATA + 12, f32b(25.0f));
    exec(m, {0x0F, 0x10, 0x05, 0x00, 0x00, 0x02, 0x00, 0x0F, 0x10, 0x0D, 0x08, 0x00, 0x02, 0x00,
             0xF3, 0x0F, 0x53, 0xC1, 0x0F, 0x11, 0x05, 0x10, 0x00, 0x02, 0x00}, 0, 0, 0, false);
    EXPECT(m.mem.r32(Machine::DATA + 16) == f32b(0.0625f) && m.mem.r32(Machine::DATA + 20) == f32b(9.0f),
           "rcpss xmm0(4),xmm1(16): got %g %g", double(bf32(m.mem.r32(Machine::DATA + 16))), double(bf32(m.mem.r32(Machine::DATA + 20))));
    // rcpss xmm0, [DATA+12] — a 4-byte memory operand; lanes 1-3 stay.
    exec(m, {0x0F, 0x10, 0x05, 0x00, 0x00, 0x02, 0x00, 0xF3, 0x0F, 0x53, 0x05, 0x0C, 0x00, 0x02, 0x00,
             0x0F, 0x11, 0x05, 0x10, 0x00, 0x02, 0x00}, 0, 0, 0, false);
    EXPECT(m.mem.r32(Machine::DATA + 16) == f32b(0.04f) && m.mem.r32(Machine::DATA + 20) == f32b(9.0f),
           "rcpss xmm0,[25]: got %g %g", double(bf32(m.mem.r32(Machine::DATA + 16))), double(bf32(m.mem.r32(Machine::DATA + 20))));
    // rsqrtss xmm0, xmm1 (F3 0F 52 C1): 1/sqrt(16) = 0.25.
    exec(m, {0x0F, 0x10, 0x05, 0x00, 0x00, 0x02, 0x00, 0x0F, 0x10, 0x0D, 0x08, 0x00, 0x02, 0x00,
             0xF3, 0x0F, 0x52, 0xC1, 0x0F, 0x11, 0x05, 0x10, 0x00, 0x02, 0x00}, 0, 0, 0, false);
    EXPECT(m.mem.r32(Machine::DATA + 16) == f32b(0.25f) && m.mem.r32(Machine::DATA + 20) == f32b(9.0f),
           "rsqrtss xmm0(4),xmm1(16): got %g %g", double(bf32(m.mem.r32(Machine::DATA + 16))), double(bf32(m.mem.r32(Machine::DATA + 20))));
    // unpcklps/unpckhps interleave dest,src (dest first).
    m.mem.w128(Machine::DATA, std::array<uint32_t, 4>{0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u}.data());
    m.mem.w128(Machine::DATA + 16, std::array<uint32_t, 4>{0xAAAAAAAAu, 0xBBBBBBBBu, 0xCCCCCCCCu, 0xDDDDDDDDu}.data());
    exec(m, {0x0F, 0x28, 0x05, 0x00, 0x00, 0x02, 0x00, 0x0F, 0x28, 0x0D, 0x10, 0x00, 0x02, 0x00,
             0x0F, 0x14, 0xC1, 0x0F, 0x29, 0x05, 0x20, 0x00, 0x02, 0x00}, 0, 0, 0, false);
    m.mem.r128(Machine::DATA + 32, out);
    EXPECT(out[0] == 0x11111111u && out[1] == 0xAAAAAAAAu && out[2] == 0x22222222u && out[3] == 0xBBBBBBBBu,
           "unpcklps: %08x %08x %08x %08x", out[0], out[1], out[2], out[3]);
    exec(m, {0x0F, 0x28, 0x05, 0x00, 0x00, 0x02, 0x00, 0x0F, 0x28, 0x0D, 0x10, 0x00, 0x02, 0x00,
             0x0F, 0x15, 0xC1, 0x0F, 0x29, 0x05, 0x20, 0x00, 0x02, 0x00}, 0, 0, 0, false);
    m.mem.r128(Machine::DATA + 32, out);
    EXPECT(out[0] == 0x33333333u && out[1] == 0xCCCCCCCCu && out[2] == 0x44444444u && out[3] == 0xDDDDDDDDu,
           "unpckhps: %08x %08x %08x %08x", out[0], out[1], out[2], out[3]);
    // packuswb saturates signed 16-bit lanes into unsigned bytes.
    m.mem.w128(Machine::DATA, std::array<uint32_t, 4>{0x012cffffu, 0x012c0080u, 0x00ff0000u, 0x03e80100u}.data());
    m.mem.w128(Machine::DATA + 16, std::array<uint32_t, 4>{0x00040003u, 0x00060005u, 0x00080007u, 0x000a0009u}.data());
    exec(m, {0x66, 0x0F, 0x6F, 0x05, 0x00, 0x00, 0x02, 0x00, 0x66, 0x0F, 0x6F, 0x0D, 0x10, 0x00, 0x02, 0x00,
             0x66, 0x0F, 0x67, 0xC1, 0x66, 0x0F, 0x7F, 0x05, 0x20, 0x00, 0x02, 0x00}, 0, 0, 0, false);
    m.mem.r128(Machine::DATA + 32, out);
    { const uint8_t* b = reinterpret_cast<const uint8_t*>(out);
      const uint8_t want[16] = {0, 255, 128, 255, 0, 255, 255, 255, 3, 4, 5, 6, 7, 8, 9, 10};
      bool same = true; for (int i = 0; i < 16; ++i) same = same && b[i] == want[i];
      EXPECT(same, "packuswb: %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u", b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]); }
    // stmxcsr/ldmxcsr round-trip: what fxsave-style save/restore code depends on.
    m.mem.w32(Machine::DATA, 0xdeadbeefu);
    exec(m, {0x0F, 0xAE, 0x1D, 0x00, 0x00, 0x02, 0x00}, 0, 0, 0, false);  // stmxcsr [DATA]
    EXPECT(m.mem.r32(Machine::DATA) == m.cpu.mxcsr, "stmxcsr wrote %08x want %08x", m.mem.r32(Machine::DATA), m.cpu.mxcsr);
    m.mem.w32(Machine::DATA, 0x1f80);
    exec(m, {0x0F, 0xAE, 0x15, 0x00, 0x00, 0x02, 0x00}, 0, 0, 0, false);  // ldmxcsr [DATA]
    EXPECT(m.cpu.mxcsr == 0x1f80, "ldmxcsr: %08x", m.cpu.mxcsr);
    // fptan: st0 = tan(x), then 1.0 pushed on top.
    m.mem.w64(Machine::DATA, f64b(1.0));
    exec(m, {0xDD, 0x05, 0x00, 0x00, 0x02, 0x00, 0xD9, 0xF2, 0xDD, 0x1D, 0x10, 0x00, 0x02, 0x00, 0xDD, 0x1D, 0x18, 0x00, 0x02, 0x00}, 0, 0, 0, false);
    EXPECT(bf64(m.mem.r64(Machine::DATA + 16)) == 1.0 && std::fabs(bf64(m.mem.r64(Machine::DATA + 24)) - std::tan(1.0)) < 1e-15,
           "fptan(1.0): st0=%g st1=%g", bf64(m.mem.r64(Machine::DATA + 16)), bf64(m.mem.r64(Machine::DATA + 24)));
    // fxtract: st0 = significand, st1 = unbiased exponent.
    m.mem.w64(Machine::DATA, f64b(8.0));
    exec(m, {0xDD, 0x05, 0x00, 0x00, 0x02, 0x00, 0xD9, 0xF4, 0xDD, 0x1D, 0x10, 0x00, 0x02, 0x00, 0xDD, 0x1D, 0x18, 0x00, 0x02, 0x00}, 0, 0, 0, false);
    EXPECT(bf64(m.mem.r64(Machine::DATA + 16)) == 1.0 && bf64(m.mem.r64(Machine::DATA + 24)) == 3.0,
           "fxtract(8.0): st0=%g st1=%g", bf64(m.mem.r64(Machine::DATA + 16)), bf64(m.mem.r64(Machine::DATA + 24)));
}

// ---------- (2b) stage-2 additions: shuffles, packed shifts, saturating ops,
// pmul* family, min/max/pavgb/psadbw, cmps/scas rep forms, rdtsc/cpuid,
// movnti/prefetch, real fxsave/fxrstor ----------
static void test_sse2_stage2() {
    Machine m;
    // load xmm0 <- [DATA], xmm1 <- [DATA+16], <op>, store xmm0 -> [DATA+32]
    const std::vector<uint8_t> pre{0x66, 0x0F, 0x6F, 0x05, 0x00, 0x00, 0x02, 0x00,
                                   0x66, 0x0F, 0x6F, 0x0D, 0x10, 0x00, 0x02, 0x00};
    const std::vector<uint8_t> post{0x66, 0x0F, 0x7F, 0x05, 0x20, 0x00, 0x02, 0x00};
    auto run = [&](const std::vector<uint8_t>& op_enc, const uint32_t a[4], const uint32_t b[4], uint32_t out[4]) {
        m.mem.protect(Machine::DATA, 4096, PROT_R | PROT_W);
        m.mem.w128(Machine::DATA, a); m.mem.w128(Machine::DATA + 16, b);
        std::vector<uint8_t> code = pre; code.insert(code.end(), op_enc.begin(), op_enc.end());
        code.insert(code.end(), post.begin(), post.end());
        exec(m, code, 0, 0, 0, false);
        m.mem.r128(Machine::DATA + 32, out);
    };
    const uint32_t V1[4] = {0x03020100u, 0x07060504u, 0x0b0a0908u, 0x0f0e0d0cu};
    const uint32_t V2[4] = {0x13121110u, 0x17161514u, 0x1b1a1918u, 0x1f1e1d1cu};
    // ---- pshufd: every immediate against the software model (dword lanes) ----
    for (unsigned imm = 0; imm < 256; ++imm) {
        uint32_t want[4];
        for (int i = 0; i < 4; ++i) want[i] = V1[(imm >> (2 * i)) & 3];
        uint32_t out[4];
        run({0x66, 0x0F, 0x70, 0xC1, uint8_t(imm)}, V2, V1, out);  // C1: src is xmm1 = V1
        EXPECT(memcmp(out, want, 16) == 0, "pshufd imm=%02x: %08x %08x %08x %08x want %08x %08x %08x %08x",
               imm, out[0], out[1], out[2], out[3], want[0], want[1], want[2], want[3]);
        ++alu_cases;
    }
    // pshufd against the real CPU for four immediates (pins the model itself).
    // The immediate must be a literal, so one asm per case.
    for (unsigned sel = 0; sel < 4; ++sel) {
        uint32_t out[4];
        run({0x66, 0x0F, 0x70, 0xC1, uint8_t(sel == 0 ? 0x00 : sel == 1 ? 0x1B : sel == 2 ? 0xE4 : 0x27)}, V2, V1, out);
        __v4si v = {int(V1[0]), int(V1[1]), int(V1[2]), int(V1[3])};
        __v4si r;
        switch (sel) {
        case 0: asm volatile("pshufd $0x00, %1, %0" : "=x"(r) : "x"(v)); break;
        case 1: asm volatile("pshufd $0x1B, %1, %0" : "=x"(r) : "x"(v)); break;
        case 2: asm volatile("pshufd $0xE4, %1, %0" : "=x"(r) : "x"(v)); break;
        default: asm volatile("pshufd $0x27, %1, %0" : "=x"(r) : "x"(v)); break; }
        uint32_t hw[4]; memcpy(hw, &r, 16);
        EXPECT(memcmp(out, hw, 16) == 0, "pshufd hw sel=%u", sel);
        ++alu_cases;
    }
    // shufps: model over representative immediates (no-prefix form: movups loads)
    for (unsigned imm : {0x00u, 0x1bu, 0xe4u, 0x4eu, 0xb1u, 0xffu}) {
        uint32_t want[4] = {V1[imm & 3], V1[(imm >> 2) & 3], V2[(imm >> 4) & 3], V2[(imm >> 6) & 3]};
        m.mem.protect(Machine::DATA, 4096, PROT_R | PROT_W);
        m.mem.w128(Machine::DATA, V1); m.mem.w128(Machine::DATA + 16, V2);
        std::vector<uint8_t> code{0x0F, 0x10, 0x05, 0x00, 0x00, 0x02, 0x00,
                                  0x0F, 0x10, 0x0D, 0x10, 0x00, 0x02, 0x00,
                                  0x0F, 0xC6, 0xC1, uint8_t(imm),
                                  0x0F, 0x11, 0x05, 0x20, 0x00, 0x02, 0x00};
        exec(m, code, 0, 0, 0, false);
        uint32_t out[4]; m.mem.r128(Machine::DATA + 32, out);
        EXPECT(memcmp(out, want, 16) == 0, "shufps imm=%02x: %08x %08x %08x %08x", imm, out[0], out[1], out[2], out[3]);
        ++alu_cases;
    }
    // shufpd (66 0F C6): bit0 picks a's qword, bit1 picks b's qword
    for (unsigned imm = 0; imm < 4; ++imm) {
        uint32_t want[4] = {V1[(imm & 1) * 2], V1[(imm & 1) * 2 + 1], V2[((imm >> 1) & 1) * 2], V2[((imm >> 1) & 1) * 2 + 1]};
        uint32_t out[4];
        run({0x66, 0x0F, 0xC6, 0xC1, uint8_t(imm)}, V1, V2, out);
        EXPECT(memcmp(out, want, 16) == 0, "shufpd imm=%d", imm);
        ++alu_cases;
    }
    // pshuflw/pshufhw (F2/F3 0F 70): one half shuffled, other half kept
    for (unsigned imm : {0x1bu, 0xe4u, 0x27u}) {
        uint16_t src[8]; memcpy(src, V1, 16);
        uint32_t want[4]; memcpy(want, V1, 16);
        for (int i = 0; i < 4; ++i) src[i] = src[(imm >> (2 * i)) & 3];  // F2: low half
        memcpy(want, src, 16);
        uint32_t out[4];
        run({0xF2, 0x0F, 0x70, 0xC1, uint8_t(imm)}, V2, V1, out);  // C1: src is xmm1 = V1
        EXPECT(memcmp(out, want, 16) == 0, "pshuflw imm=%02x", imm);
        ++alu_cases;
        uint16_t src2[8]; memcpy(src2, V1, 16);
        uint32_t want2[4]; memcpy(want2, V1, 16);
        for (int i = 0; i < 4; ++i) src2[4 + i] = src2[4 + ((imm >> (2 * i)) & 3)];  // F3: high half
        memcpy(want2, src2, 16);
        run({0xF3, 0x0F, 0x70, 0xC1, uint8_t(imm)}, V2, V1, out);
        EXPECT(memcmp(out, want2, 16) == 0, "pshufhw imm=%02x", imm);
        ++alu_cases;
    }
    // ---- packed shifts by imm8: psllw/psrlw/psraw and psrad, pslldq/psrldq ----
    {
        auto shift_words = [](const uint32_t a[4], unsigned cnt, char dir, uint32_t out[4]) {
            uint16_t o[8]; const uint16_t* in = reinterpret_cast<const uint16_t*>(a);
            for (int i = 0; i < 8; ++i) {
                if (dir == 'l') o[i] = cnt > 15 ? 0 : uint16_t(in[i] << cnt);
                else if (dir == 'r') o[i] = cnt > 15 ? 0 : uint16_t(in[i] >> cnt);
                else o[i] = cnt > 15 ? (in[i] & 0x8000 ? 0xffffu : 0) : uint16_t(int16_t(in[i]) >> cnt);
            }
            memcpy(out, o, 16);
        };
        for (unsigned cnt : {0u, 1u, 3u, 15u, 16u, 31u}) {
            for (char dir : {'l', 'r', 'a'}) {
                uint32_t want[4]; shift_words(V1, cnt, dir, want);
                uint32_t out[4];
                uint8_t sop = dir == 'l' ? 6 : dir == 'r' ? 2 : 4;
                run({0x66, 0x0F, 0x71, uint8_t(0xC0 | (sop << 3)), uint8_t(cnt)}, V1, V2, out);
                EXPECT(memcmp(out, want, 16) == 0, "psllw/psrlw/psraw cnt=%u dir=%c: %08x %08x", cnt, dir, out[0], out[1]);
                ++alu_cases;
            }
        }
        for (unsigned cnt : {0u, 1u, 7u, 31u, 32u}) {
            uint32_t want[4];
            for (int i = 0; i < 4; ++i) want[i] = cnt >= 32 ? (V1[i] & 0x80000000u ? 0xffffffffu : 0) : uint32_t(int32_t(V1[i]) >> cnt);
            uint32_t out[4];
            run({0x66, 0x0F, 0x72, 0xE0, uint8_t(cnt)}, V1, V2, out);
            EXPECT(memcmp(out, want, 16) == 0, "psrad cnt=%u: %08x %08x", cnt, out[0], out[1]);
            ++alu_cases;
        }
        // pslldq/psrldq: whole-register byte shifts, count saturates at 16
        const uint8_t* in = reinterpret_cast<const uint8_t*>(V1);
        for (unsigned cnt : {0u, 1u, 5u, 15u, 16u, 255u}) {
            unsigned k = cnt >= 16 ? 16 : cnt;
            uint8_t wantr[16] = {0};
            for (unsigned i = 0; i + k < 16; ++i) wantr[i] = in[i + k];
            uint32_t wantr2[4]; memcpy(wantr2, wantr, 16);
            uint32_t out[4];
            run({0x66, 0x0F, 0x73, 0xD8, uint8_t(cnt)}, V1, V2, out);
            EXPECT(memcmp(out, wantr2, 16) == 0, "psrldq cnt=%u", cnt);
            ++alu_cases;
            uint8_t wantl[16] = {0};
            for (unsigned i = 0; i + k < 16; ++i) wantl[i + k] = in[i];
            uint32_t wantl2[4]; memcpy(wantl2, wantl, 16);
            run({0x66, 0x0F, 0x73, 0xF8, uint8_t(cnt)}, V1, V2, out);
            EXPECT(memcmp(out, wantl2, 16) == 0, "pslldq cnt=%u", cnt);
            ++alu_cases;
        }
        // register-form shifts: psrlw xmm0, xmm1 (66 0F D1 C1); count = xmm1[63:0]
        for (unsigned cnt : {0u, 3u, 16u, 70u}) {
            uint32_t cntv[4] = {cnt, 0, 0xdeadbeefu, 0xcafebabeu};  // upper 64 bits must be ignored
            const uint16_t* w = reinterpret_cast<const uint16_t*>(V1);
            uint16_t o[8]; for (int i = 0; i < 8; ++i) o[i] = cnt < 16 ? uint16_t(w[i] >> cnt) : 0;
            uint32_t want[4]; memcpy(want, o, 16);
            uint32_t out[4];
            run({0x66, 0x0F, 0xD1, 0xC1}, V1, cntv, out);
            EXPECT(memcmp(out, want, 16) == 0, "psrlw reg cnt=%u", cnt);
            ++alu_cases;
        }
        // pslld reg (66 0F F2 C1)
        for (unsigned cnt : {0u, 5u, 32u}) {
            uint32_t cntv[4] = {cnt, 0, 0, 0};
            uint32_t want[4]; for (int i = 0; i < 4; ++i) want[i] = cnt < 32 ? V1[i] << cnt : 0;
            uint32_t out[4];
            run({0x66, 0x0F, 0xF2, 0xC1}, V1, cntv, out);
            EXPECT(memcmp(out, want, 16) == 0, "pslld reg cnt=%u", cnt);
            ++alu_cases;
        }
        // psrlq reg (66 0F D3 C1): 64-bit lanes
        for (unsigned cnt : {0u, 7u, 64u}) {
            uint32_t cntv[4] = {cnt, 0, 0, 0};
            uint32_t want[4];
            uint64_t q0 = uint64_t(V1[1]) << 32 | V1[0], q1 = uint64_t(V1[3]) << 32 | V1[2];
            q0 = cnt < 64 ? q0 >> cnt : 0; q1 = cnt < 64 ? q1 >> cnt : 0;
            want[0] = uint32_t(q0); want[1] = uint32_t(q0 >> 32); want[2] = uint32_t(q1); want[3] = uint32_t(q1 >> 32);
            uint32_t out[4];
            run({0x66, 0x0F, 0xD3, 0xC1}, V1, cntv, out);
            EXPECT(memcmp(out, want, 16) == 0, "psrlq reg cnt=%u", cnt);
            ++alu_cases;
        }
        // memory-destination form: psllw [mem], imm8 (66 0F 71 /6 mem) writes back
        {
            uint32_t memv[4] = {0x03020100u, 0x07060504u, 0, 0};
            m.mem.protect(Machine::DATA, 4096, PROT_R | PROT_W);
            m.mem.w128(Machine::DATA, memv);
            std::vector<uint8_t> code{0x66, 0x0F, 0x71, 0x35, 0x00, 0x00, 0x02, 0x00, 0x03};  // /6 sl, mem operand (mod=0 rm=5 = disp32), imm 3
            exec(m, code, 0, 0, 0, false);
            uint32_t got[4]; m.mem.r128(Machine::DATA, got);
            EXPECT(got[0] == 0x18100800u && got[1] == 0x38302820u, "psllw mem dest: %08x %08x", got[0], got[1]);
            ++alu_cases;
        }
    }
    // ---- arithmetic: paddq, pmullw, pmuludq, pmaddwd, psadbw, pavgb, saturating, min/max ----
    {
        uint32_t out[4];
        // paddq: 64-bit lanes
        {
            uint32_t a[4] = {1, 0, 0xffffffffu, 0xffffffffu};  // +1 in low qword, -1 in high
            uint32_t b[4] = {2, 0, 1, 0};
            run({0x66, 0x0F, 0xD4, 0xC1}, a, b, out);
            EXPECT(out[0] == 3 && out[1] == 0 && out[2] == 0 && out[3] == 0, "paddq: %08x %08x %08x %08x", out[0], out[1], out[2], out[3]);
            ++alu_cases;
        }
        // pmullw: low 16 bits of the products
        for (auto pair : {std::pair{V1, V2}, std::pair{V2, V1}}) {
            uint16_t want[8]; const uint16_t* x = reinterpret_cast<const uint16_t*>(pair.first); const uint16_t* y = reinterpret_cast<const uint16_t*>(pair.second);
            for (int i = 0; i < 8; ++i) want[i] = uint16_t(int16_t(x[i]) * int16_t(y[i]));
            uint32_t aa[4], bb[4], out2[4]; memcpy(aa, pair.first, 16); memcpy(bb, pair.second, 16);
            run({0x66, 0x0F, 0xD5, 0xC1}, aa, bb, out2);
            EXPECT(memcmp(out2, want, 16) == 0, "pmullw");
            ++alu_cases;
        }
        // pmuludq
        {
            uint32_t a[4] = {0xffffffffu, 0x11111111u, 3, 0x80000000u};
            uint32_t b[4] = {0xffffffffu, 0x22222222u, 5, 7};
            uint64_t want0 = uint64_t(a[0]) * b[0], want1 = uint64_t(a[2]) * b[2];
            run({0x66, 0x0F, 0xF4, 0xC1}, a, b, out);
            EXPECT(out[0] == uint32_t(want0) && out[1] == uint32_t(want0 >> 32) && out[2] == uint32_t(want1) && out[3] == uint32_t(want1 >> 32), "pmuludq");
            ++alu_cases;
        }
        // pmaddwd
        {
            int16_t x[8] = {1, 2, -3, 4, 32767, 1, -32768, 1};
            int16_t y[8] = {3, 4, 5, -6, 2, 3, -1, -1};
            uint32_t a[4], b[4]; memcpy(a, x, 16); memcpy(b, y, 16);
            int32_t want[4]; for (int i = 0; i < 4; ++i) want[i] = int32_t(x[2 * i]) * y[2 * i] + int32_t(x[2 * i + 1]) * y[2 * i + 1];
            run({0x66, 0x0F, 0xF5, 0xC1}, a, b, out);
            EXPECT(memcmp(out, want, 16) == 0, "pmaddwd: %08x %08x %08x %08x", out[0], out[1], out[2], out[3]);
            ++alu_cases;
        }
        // psadbw
        {
            uint8_t x[16] = {1, 2, 3, 4, 5, 6, 7, 8, 200, 200, 0, 0, 0, 0, 0, 0};
            uint8_t y[16] = {8, 7, 6, 5, 4, 3, 2, 1, 100, 100, 0, 0, 0, 0, 0, 0};
            uint32_t a[4], b[4]; memcpy(a, x, 16); memcpy(b, y, 16);
            unsigned want0 = 0, want1 = 0;
            for (int i = 0; i < 8; ++i) { want0 += unsigned(std::abs(int(x[i]) - int(y[i]))); want1 += unsigned(std::abs(int(x[8 + i]) - int(y[8 + i]))); }
            run({0x66, 0x0F, 0xF6, 0xC1}, a, b, out);
            EXPECT(out[0] == want0 && out[1] == 0 && out[2] == want1 && out[3] == 0, "psadbw: %u %u", out[0], out[2]);
            ++alu_cases;
        }
        // pavgb
        {
            uint8_t x[16] = {1, 2, 255, 254, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            uint8_t y[16] = {2, 3, 255, 255, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            uint32_t a[4], b[4]; memcpy(a, x, 16); memcpy(b, y, 16);
            uint8_t want[16]; for (int i = 0; i < 16; ++i) want[i] = uint8_t((unsigned(x[i]) + unsigned(y[i]) + 1) >> 1);
            uint32_t wantv[4]; memcpy(wantv, want, 16);
            run({0x66, 0x0F, 0xE0, 0xC1}, a, b, out);
            EXPECT(memcmp(out, wantv, 16) == 0, "pavgb");
            ++alu_cases;
        }
        // saturating add/sub
        {
            uint32_t a[4] = {0x0afafa0au, 0x00017fffu, 0x0a0a0a0au, 0x80008000u};
            uint32_t b[4] = {0x0a0a0a0au, 0x00010001u, 0x0a0a0a0au, 0x00010001u};
            run({0x66, 0x0F, 0xDC, 0xC1}, a, b, out);  // paddusb: bytes 0a+0a=14, fa+0a=104->ff
            EXPECT(out[0] == 0x14ffff14u, "paddusb: %08x", out[0]);
            EXPECT(out[1] == 0x00027fffu, "paddsw via words: %08x", out[1]);  // 7fff+0001 -> 7fff (saturated), 1+1=2
            ++alu_cases;
            run({0x66, 0x0F, 0xE8, 0xC1}, a, b, out);  // psubusb: 0a-0a=0, fa-0a=f0
            EXPECT(out[0] == 0x00f0f000u, "psubusb: %08x", out[0]);
            ++alu_cases;
            run({0x66, 0x0F, 0xEC, 0xC1}, a, b, out);  // psubsw: word 8000-0001 -> 8000 (saturated)
            EXPECT(out[3] == 0x80008000u, "psubsw: %08x", out[3]);
            ++alu_cases;
        }
        // pminub/pmaxub/pminsw/pmaxsw
        {
            uint32_t a[4] = {0x01020505u, 0, 0, 0};
            uint32_t b[4] = {0x020101ffu, 0, 0, 0};
            run({0x66, 0x0F, 0xDA, 0xC1}, a, b, out);  // pminub per byte
            EXPECT(out[0] == 0x01010105u, "pminub: %08x", out[0]);
            ++alu_cases;
            run({0x66, 0x0F, 0xDE, 0xC1}, a, b, out);  // pmaxub
            EXPECT(out[0] == 0x020205ffu, "pmaxub: %08x", out[0]);
            ++alu_cases;
            uint32_t c[4] = {0x00020001u, 0xfffaffffu, 0, 0};   // words 1, 2, -6, -1
            uint32_t d[4] = {0x0001ffffu, 0x00010002u, 0, 0};   // words -1, 1, 1, 2
            run({0x66, 0x0F, 0xEA, 0xC1}, c, d, out);  // pminsw
            EXPECT(out[0] == 0x0001ffffu && out[1] == 0xfffaffffu, "pminsw: %08x %08x", out[0], out[1]);
            ++alu_cases;
            run({0x66, 0x0F, 0xEE, 0xC1}, c, d, out);  // pmaxsw
            EXPECT(out[0] == 0x00020001u && out[1] == 0x00010002u, "pmaxsw: %08x %08x", out[0], out[1]);
            ++alu_cases;
        }
        // pand / pandn (66)
        {
            run({0x66, 0x0F, 0xDB, 0xC1}, V1, V2, out);
            EXPECT(out[0] == (V1[0] & V2[0]) && out[3] == (V1[3] & V2[3]), "pand");
            ++alu_cases;
            run({0x66, 0x0F, 0xDF, 0xC1}, V1, V2, out);
            EXPECT(out[0] == (~V1[0] & V2[0]), "pandn");
            ++alu_cases;
        }
        // punpckhqdq (66 0F 6D)
        {
            run({0x66, 0x0F, 0x6D, 0xC1}, V1, V2, out);
            EXPECT(out[0] == V1[2] && out[1] == V1[3] && out[2] == V2[2] && out[3] == V2[3], "punpckhqdq");
            ++alu_cases;
        }
    }
    // ---- rdtsc: strictly increasing ----
    {
        std::vector<uint8_t> one{0x0F, 0x31};
        exec(m, one, 0, 0, 0, false);
        uint64_t t1 = uint64_t(m.cpu.reg[EDX]) << 32 | m.cpu.reg[EAX];
        exec(m, one, 0, 0, 0, false);
        uint64_t t2 = uint64_t(m.cpu.reg[EDX]) << 32 | m.cpu.reg[EAX];
        EXPECT(t2 > t1, "rdtsc must advance: %llu then %llu", (unsigned long long)t1, (unsigned long long)t2);
        ++alu_cases;
    }
    // ---- cpuid: leaf 0 is GenuineIntel, leaf 1 has SSE2, unknown leaves zero ----
    {
        std::vector<uint8_t> code{0xB8, 0, 0, 0, 0, 0x0F, 0xA2};  // mov eax,0; cpuid
        exec(m, code, 0, 0, 0, false);
        EXPECT(m.cpu.reg[EBX] == 0x756e6547u && m.cpu.reg[EDX] == 0x49656e69u && m.cpu.reg[ECX] == 0x6c65746eu, "cpuid leaf0 vendor");
        ++alu_cases;
        std::vector<uint8_t> one{0xB8, 1, 0, 0, 0, 0x0F, 0xA2};
        exec(m, one, 0, 0, 0, false);
        EXPECT(m.cpu.reg[EDX] & (1u << 26), "cpuid leaf1 must advertise SSE2");
        ++alu_cases;
        std::vector<uint8_t> unk{0xB8, 0x7fu, 0, 0, 0, 0x0F, 0xA2};
        exec(m, unk, 0, 0, 0, false);
        EXPECT(m.cpu.reg[EAX] == 0 && m.cpu.reg[EBX] == 0 && m.cpu.reg[ECX] == 0 && m.cpu.reg[EDX] == 0, "cpuid unknown leaf must be zeroed");
        ++alu_cases;
    }
    // ---- movnti (66 0F C3): dword store ----
    {
        m.mem.w32(Machine::DATA, 0);
        m.mem.protect(Machine::DATA, 4096, PROT_R | PROT_W);
        std::vector<uint8_t> code{0x66, 0x0F, 0xC3, 0x05, 0x00, 0x00, 0x02, 0x00};  // movnti [DATA], eax
        exec(m, code, 0xfeedfaceu, 0, 0, false);
        EXPECT(m.mem.r32(Machine::DATA) == 0xfeedfaceu, "movnti store: %08x", m.mem.r32(Machine::DATA));
        ++alu_cases;
    }
    // ---- prefetcht0 (0F 18 /0): executes as a hint ----
    {
        std::vector<uint8_t> code{0x0F, 0x18, 0x05, 0x00, 0x00, 0x02, 0x00};
        exec(m, code, 0, 0, 0, false);
        ++alu_cases;
    }
    // ---- fxsave/fxrstor: x87 stack, MXCSR and XMM round-trip ----
    {
        m.mem.protect(Machine::DATA, 4096, PROT_R | PROT_W);
        m.mem.w64(Machine::DATA + 0x200, f64b(1.5));
        std::vector<uint8_t> code{
            0xDD, 0x05, 0x00, 0x02, 0x02, 0x00,          // fld qword [DATA+0x200]
            0x66, 0x0F, 0x6F, 0x0D, 0x10, 0x00, 0x02, 0x00,  // movdqa xmm1, [DATA+16]
            0x0F, 0xAE, 0x05, 0x00, 0x03, 0x02, 0x00,    // fxsave [DATA+0x300]
            0xDD, 0xD8,                                  // fstp st(0)
            0x0F, 0x28, 0x0D, 0x00, 0x00, 0x02, 0x00,    // movaps xmm1, [DATA]  (clobber)
            0x0F, 0xAE, 0x0D, 0x00, 0x03, 0x02, 0x00,    // fxrstor [DATA+0x300]
            0xDD, 0x1D, 0x08, 0x02, 0x02, 0x00,          // fstp qword [DATA+0x208]
            0x66, 0x0F, 0x7F, 0x0D, 0x18, 0x00, 0x02, 0x00,  // movdqa [DATA+0x18], xmm1
        };
        exec(m, code, 0, 0, 0, false);
        EXPECT(bf64(m.mem.r64(Machine::DATA + 0x208)) == 1.5, "fxsave/fxrstor x87 round trip: %g", bf64(m.mem.r64(Machine::DATA + 0x208)));
        uint32_t xr[4]; m.mem.r128(Machine::DATA + 0x18, xr);
        EXPECT(xr[0] == 0x13121110u && xr[3] == 0x1f1e1d1cu, "fxsave/fxrstor xmm round trip");
        ++alu_cases;
    }
    // ---- repne scasb / repe cmpsb: terminate on the ZF condition ----
    {
        m.mem.protect(Machine::DATA, 4096, PROT_R | PROT_W);
        const char* s = "hello, world";
        for (size_t i = 0; i <= strlen(s); ++i) m.mem.w8(Machine::DATA + i, uint8_t(s[i]));
        // mov edi, DATA; mov al, 'w'; repne scasb; eax = edi - DATA - 1 = index of 'w'
        std::vector<uint8_t> code{0xBF, 0x00, 0x00, 0x02, 0x00, 0xB0, uint8_t('w'),
                                  0xB9, 32, 0, 0, 0, 0xF2, 0xAE,
                                  0x89, 0xF8, 0x2D, 0x00, 0x00, 0x02, 0x00, 0x48};
        exec(m, code, 0, 0, 0, false);
        EXPECT(m.cpu.reg[EAX] == 7, "repne scasb found 'w' at %u want 7", m.cpu.reg[EAX]);
        ++alu_cases;
        // repe cmpsb: equal strings (6 bytes incl NUL) -> ZF set at the end
        for (int i = 0; i < 6; ++i) { m.mem.w8(Machine::DATA + i, uint8_t("hello"[i] ? "hello"[i] : 0)); }
        for (int i = 0; i < 6; ++i) { m.mem.w8(Machine::DATA + 16 + i, uint8_t("hello"[i] ? "hello"[i] : 0)); }
        std::vector<uint8_t> cmp{0xBE, 0x00, 0x00, 0x02, 0x00, 0xBF, 0x10, 0x00, 0x02, 0x00, 0xB9, 6, 0, 0, 0,
                                 0xF3, 0xA6, 0x0F, 0x94, 0xC0};  // sete al
        exec(m, cmp, 0, 0, 0, false);
        EXPECT(m.cpu.reg[EAX] == 1, "repe cmpsb equal: eax=%u", m.cpu.reg[EAX]);
        ++alu_cases;
        // a mismatch stops early
        m.mem.w8(Machine::DATA + 21, 'p');
        exec(m, cmp, 0, 0, 0, false);
        EXPECT(m.cpu.reg[EAX] == 0, "repe cmpsb mismatch: eax=%u", m.cpu.reg[EAX]);
        ++alu_cases;
    }
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
        {"fpu", test_fpu, "X86-CORE PASS: x87 f64 arithmetic and fcomip, SSE scalar double/single arithmetic, int<->float conversion and SSE2 packed integer match this machine's CPU bit for bit"},
        {"x87forms", test_x87_forms, "X86-CORE PASS: x87 memory forms, stack discipline, control/status words and 80-bit round trip"},
        {"sse-regressions", test_sse_regressions, "X86-CORE PASS: bt imm, movd/movq, movmskps/pmovmskb, sqrtss, unpck, packuswb, stmxcsr, fptan/fxtract match this machine's CPU"},
        {"sse2-stage2", test_sse2_stage2, "X86-CORE PASS: pshufd/pshuflw/pshufhw/shufps/shufpd, packed shifts (imm8, xmm, dq), paddq/pmullw/pmuludq/pmaddwd/psadbw/pavgb, saturating add/sub, pmin/pmax, punpckhqdq, rdtsc, cpuid, movnti, prefetch, fxsave/fxrstor and repne scasb/repe cmpsb"},
        {"thunks", test_thunks, "X86-CORE PASS: stdcall/cdecl thunks, nested guest callbacks, callee-saved registers preserved"},
    };
    for (auto& s : suites) { int before = failures; s.fn(); if (failures == before) printf("%s\n", s.pass); else printf("X86-CORE FAIL: %s\n", s.name); }
    printf("(%llu hardware-compared cases)\n", (unsigned long long)alu_cases);
    int before = failures; test_fixture(argv[1]);
    if (failures == before) printf("X86-CORE PASS: owned i686 fixture PE ran to exit=42 at its ImageBase and relocated; unresolved import faults by name\n");
    else printf("X86-CORE FAIL: fixture\n");
    return failures ? 1 : 0;
}
