// SPDX-License-Identifier: GPL-3.0-or-later
// Linux-only proof of concept: translate a bounded PE32 integer block to x86-64
// long-mode code, compare it with the existing interpreter, and benchmark it.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include "../host/x86/x86_dbt.h"
#include "../host/x86/x86_win32.h"

#if !defined(__linux__) || !defined(__x86_64__)
#error "x86-dbt is an off-device Linux x86-64 proof of concept"
#endif

using namespace x86;
namespace {
constexpr uint32_t CODE = 0x10000;
constexpr uint32_t DATA = 0x20000;
constexpr uint32_t PAGE = 4096;
constexpr uint32_t EIP_OFFSET = 32;
constexpr uint32_t FLAGS_OFFSET = 36;
constexpr uint32_t MEMORY_OFFSET = 40;
constexpr uint32_t FLAG_MASK = CF | PF | AF | ZF | SF | DF | OF;
constexpr uint8_t host_reg[8] = {0, 1, 2, 6, 7, 8, 9, 10};

struct State {
    uint32_t reg[8] = {};
    uint32_t eip = 0;
    uint32_t eflags = 2;
    uint8_t* memory = nullptr;
};
static_assert(offsetof(State, eip) == EIP_OFFSET, "generated-code state layout");
static_assert(offsetof(State, eflags) == FLAGS_OFFSET, "generated-code state layout");
static_assert(offsetof(State, memory) == MEMORY_OFFSET, "generated-code state layout");

using dbt::Emitter;
using dbt::Executable;

static void mov_reg_mem(Emitter& out, uint8_t dst, uint8_t base, uint8_t disp) {
    out.rex(dst, 0, base); out.b(0x8B); out.modrm(1, dst, base);
    if ((base & 7) == 4) out.b(0x24);
    out.b(disp);
}
static void store_reg_mem(Emitter& out, uint8_t base, uint32_t disp, uint8_t src) {
    out.rex(src, 0, base); out.b(0x89); out.modrm(2, src, base);
    if ((base & 7) == 4) out.b(0x24);
    out.d(disp);
}
static void mov_imm(Emitter& out, uint8_t dst, uint32_t value) {
    out.rex(0, 0, dst); out.b(uint8_t(0xB8 + (dst & 7))); out.d(value);
}
static void register_op(Emitter& out, uint8_t opcode, uint8_t dst, uint8_t src) {
    out.rex(src, 0, dst); out.b(opcode); out.modrm(3, src, dst);
}
static uint32_t read32(const std::vector<uint8_t>& code, size_t& pc) {
    if (pc + 4 > code.size()) throw std::runtime_error("truncated disp32/immediate");
    uint32_t v = uint32_t(code[pc]) | (uint32_t(code[pc + 1]) << 8) |
                 (uint32_t(code[pc + 2]) << 16) | (uint32_t(code[pc + 3]) << 24);
    pc += 4; return v;
}
static uint8_t read8(const std::vector<uint8_t>& code, size_t& pc) {
    if (pc >= code.size()) throw std::runtime_error("truncated instruction");
    return code[pc++];
}
static void emit_binary(Emitter& out, uint8_t opcode, uint8_t dst, uint8_t src) {
    register_op(out, opcode, host_reg[dst], host_reg[src]);
}
static void emit_absolute_memory(Emitter& out, uint8_t opcode, uint8_t reg, uint32_t address,
                                 uint32_t data_base) {
    if (address < data_base || uint64_t(address) + 4 > uint64_t(data_base) + PAGE)
        throw std::runtime_error("memory operand outside the PoC data page");
    uint8_t host = host_reg[reg];
    out.rex(host, 0, 11); out.b(opcode); out.modrm(2, host, 3); out.d(address - data_base);
}

// Decode only the explicitly listed guest encodings. Register numbers are
// translated to caller-saved host registers; guest stack operations and
// control transfers other than block-ending jz/jnz exits are not part of this spike.
static std::vector<uint8_t> translate(const std::vector<uint8_t>& guest, uint32_t guest_pc,
                                      uint32_t data_base) {
    if (guest.empty() || guest.size() > 128) throw std::runtime_error("invalid block size");
    Emitter out;
    out.b(0x57);                         // save state* (RDI)
    out.b(0x48); out.b(0x83); out.b(0xEC); out.b(8); // local exit-EIP slot; aligned stack
    mov_reg_mem(out, 11, 7, FLAGS_OFFSET); // R11D = guest EFLAGS
    out.b(0x41); out.b(0x53); out.b(0x9D); // push R11; popfq
    out.rex(11, 0, 7, true); out.b(0x8B); out.modrm(1, 11, 7); out.b(MEMORY_OFFSET); // R11 = memory base
    for (unsigned i : {0u, 1u, 2u, 3u, 5u, 6u, 7u, 4u}) mov_reg_mem(out, host_reg[i], 7, uint8_t(i * 4));

    size_t pc = 0; bool branched = false;
    while (pc < guest.size()) {
        const uint8_t op = read8(guest, pc);
        if (op >= 0xB8 && op <= 0xBF) {
            uint8_t reg = uint8_t(op - 0xB8); mov_imm(out, host_reg[reg], read32(guest, pc));
        } else if (op == 0x8B) { // mov r32,r/m32 (register form)
            uint8_t m = read8(guest, pc);
            if ((m >> 6) != 3) throw std::runtime_error("mov memory form not supported");
            register_op(out, 0x8B, host_reg[m & 7], host_reg[(m >> 3) & 7]);
        } else if (op == 0x01 || op == 0x29 || op == 0x21 || op == 0x09 || op == 0x31 || op == 0x39) {
            uint8_t m = read8(guest, pc);
            if ((m >> 6) != 3) throw std::runtime_error("ALU memory form not supported");
            emit_binary(out, op, uint8_t(m & 7), uint8_t((m >> 3) & 7));
        } else if (op == 0x03 || op == 0x89) {
            uint8_t m = read8(guest, pc);
            uint32_t address;
            if ((m >> 6) != 0 || (m & 7) != 5) throw std::runtime_error("only absolute dword memory is supported");
            address = read32(guest, pc);
            if (op == 0x03) emit_absolute_memory(out, 0x03, uint8_t((m >> 3) & 7), address, data_base);
            else emit_absolute_memory(out, 0x89, uint8_t((m >> 3) & 7), address, data_base);
        } else if (op == 0x8D) { // lea r32,[base + index*scale + disp8]
            uint8_t m = read8(guest, pc);
            if (((m >> 6) != 1) || ((m & 7) != 4)) throw std::runtime_error("unsupported lea addressing form");
            uint8_t sib = read8(guest, pc), scale = uint8_t(sib >> 6);
            uint8_t index = uint8_t((sib >> 3) & 7), base = uint8_t(sib & 7);
            if (index == 4 || base == 5) throw std::runtime_error("unsupported lea SIB register");
            int8_t disp = static_cast<int8_t>(read8(guest, pc));
            uint8_t dst = host_reg[(m >> 3) & 7], idx = host_reg[index], bas = host_reg[base];
            out.rex(dst, idx, bas); out.b(0x8D); out.modrm(1, dst, 4);
            out.b(uint8_t((scale << 6) | ((idx & 7) << 3) | (bas & 7))); out.b(uint8_t(disp));
        } else if (op == 0x81 || op == 0x83) { // immediate ALU on a register
            uint8_t m = read8(guest, pc);
            if ((m >> 6) != 3) throw std::runtime_error("immediate ALU memory form not supported");
            uint8_t group = uint8_t((m >> 3) & 7), reg = host_reg[m & 7];
            if (op == 0x81 && group != 0 && group != 1 && group != 4 && group != 5 && group != 6 && group != 7)
                throw std::runtime_error("unsupported immediate ALU group");
            if (op == 0x83 && group != 0 && group != 1 && group != 4 && group != 5 && group != 6 && group != 7)
                throw std::runtime_error("unsupported immediate ALU group");
            if (op == 0x83) {
                out.rex(0, 0, reg); out.b(0x83); out.modrm(3, group, reg); out.b(read8(guest, pc));
            } else {
                out.rex(0, 0, reg); out.b(0x81); out.modrm(3, group, reg); out.d(read32(guest, pc));
            }
        } else if (op == 0x6B) { // imul r32,r/m32,imm8 (register source)
            uint8_t m = read8(guest, pc);
            if ((m >> 6) != 3) throw std::runtime_error("imul memory form not supported");
            uint8_t dst = host_reg[(m >> 3) & 7], src = host_reg[m & 7];
            out.rex(dst, 0, src); out.b(0x6B); out.modrm(3, dst, src); out.b(read8(guest, pc));
        } else if (op == 0xC1) { // immediate shifts on a register
            uint8_t m = read8(guest, pc);
            if ((m >> 6) != 3) throw std::runtime_error("shift memory form not supported");
            uint8_t group = uint8_t((m >> 3) & 7), reg = host_reg[m & 7];
            if (group != 4 && group != 5 && group != 7) throw std::runtime_error("unsupported shift group");
            out.rex(0, 0, reg); out.b(0xC1); out.modrm(3, group, reg); out.b(read8(guest, pc));
        } else if (op == 0x74 || op == 0x75) { // jz/jnz ends the translated block
            int8_t rel = static_cast<int8_t>(read8(guest, pc));
            int64_t target_off = int64_t(pc) + rel;
            if (target_off < 0 || target_off >= int64_t(guest.size())) throw std::runtime_error("conditional target outside test block");
            const uint32_t fallthrough = guest_pc + uint32_t(pc);
            const uint32_t target = guest_pc + uint32_t(target_off);
            out.b(0x0F); out.b(op == 0x74 ? 0x84 : 0x85);
            size_t to_taken = out.bytes.size(); out.d(0);
            mov_imm(out, 11, fallthrough);
            out.b(0xE9); size_t to_end = out.bytes.size(); out.d(0);
            size_t taken = out.bytes.size(); mov_imm(out, 11, target);
            size_t end = out.bytes.size();
            out.patch32(to_taken, int32_t(taken - (to_taken + 4)));
            out.patch32(to_end, int32_t(end - (to_end + 4)));
            branched = true; break;
        } else {
            char message[80]; std::snprintf(message, sizeof message, "unsupported guest opcode %02x at +%zx", op, pc - 1);
            throw std::runtime_error(message);
        }
    }
    if (pc == guest.size() && !branched) mov_imm(out, 11, guest_pc + uint32_t(pc));

    // The selected EIP is in R11D. Save architectural state without allowing
    // the host ABI's extra flags or callee-saved register rules to leak in.
    out.b(0x44); out.b(0x89); out.b(0x1C); out.b(0x24); // save selected EIP in local slot
    out.b(0x4C); out.b(0x8B); out.b(0x5C); out.b(0x24); out.b(8); // R11 = saved state*
    for (unsigned i = 0; i != 8; ++i) store_reg_mem(out, 11, i * 4, host_reg[i]);
    out.b(0x9C); out.b(0x58); // pushfq; pop rax
    out.b(0x25); out.d(0x00000CD7); // keep defined x86 EFLAGS and reserved bit 1
    store_reg_mem(out, 11, FLAGS_OFFSET, 0);
    out.b(0xFC); // clear DF before returning to the host ABI
    out.b(0x8B); out.b(0x04); out.b(0x24); // eax = local selected EIP
    store_reg_mem(out, 11, EIP_OFFSET, 0);
    out.b(0x48); out.b(0x83); out.b(0xC4); out.b(8); // discard local EIP
    out.b(0x5F);                           // restore RDI state*
    out.b(0x8B); out.b(0x47); out.b(EIP_OFFSET); // return state.eip
    out.b(0xC3);
    return out.bytes;
}


struct Machine {
    Memory memory;
    ThunkTable thunks;
    Cpu cpu{memory, thunks};
    Machine(const std::vector<uint8_t>& program) {
        memory.map(CODE, PAGE, PROT_R | PROT_W);
        memory.write_bytes(CODE, program.data(), uint32_t(program.size()));
        memory.protect(CODE, PAGE, PROT_R | PROT_X);
        memory.map(DATA, PAGE, PROT_R | PROT_W);
    }
    void run_steps(unsigned count) { while (count--) cpu.step(); }
};

static std::vector<uint8_t> compare_program() {
    std::vector<uint8_t> c = {
        0xB8, 0x78, 0x56, 0x34, 0x12,       // mov eax,0x12345678
        0x01, 0xC8,                           // add eax,ecx
        0x29, 0xD0,                           // sub eax,edx
        0x21, 0xF0,                           // and eax,esi
        0x09, 0xF8,                           // or eax,edi
        0x31, 0xD8,                           // xor eax,ebx
        0x03, 0x05, 0x04, 0x00, 0x02, 0x00, // add eax,[DATA+4]
        0x8D, 0x44, 0x48, 0x08,              // lea eax,[eax+ecx*2+8]
        0x89, 0x05, 0x08, 0x00, 0x02, 0x00, // mov [DATA+8],eax
        0x39, 0xE8,                           // cmp eax,ebp
        0x74, 0x03,                           // jz hlt; otherwise add eax,3
        0x83, 0xC0, 0x03,                     // add eax,3 (interpreter tail)
        0xF4                                  // hlt (explicit test stop)
    };
    return c;
}
static std::vector<uint8_t> integer_kernel() {
    return {
        0x8B, 0xF0,                           // mov esi,eax (old x)
        0x8B, 0xD1,                           // mov edx,ecx (i)
        0x81, 0xC2, 0xB9, 0x79, 0x37, 0x9E, // add edx,0x9e3779b9
        0x31, 0xD0,                           // xor eax,edx
        0x6B, 0xC0, 0x21,                     // imul eax,eax,33
        0xC1, 0xEE, 0x07,                     // shr esi,7
        0x01, 0xF0                            // add eax,esi
    };
}
static State initial_state(Machine& machine, std::mt19937& random) {
    State state; for (uint32_t& r : state.reg) r = random();
    state.eflags = 2u | ((random() & 1) ? uint32_t(CF) : 0u); state.eip = CODE;
    state.memory = machine.memory.page[DATA >> 12];
    machine.memory.w32(DATA + 4, random()); machine.memory.w32(DATA + 8, random());
    std::memcpy(machine.cpu.reg, state.reg, sizeof state.reg);
    machine.cpu.eflags = state.eflags; machine.cpu.eip = CODE;
    return state;
}
static bool run_to_hlt(Cpu& cpu, uint32_t hlt) {
    try { while (true) cpu.step(); }
    catch (const Fault& f) { return f.kind == FaultKind::Unsupported && f.eip == hlt; }
}
static int differential_test(const Executable& executable) {
    const auto program = compare_program();
    std::mt19937 random(0x58424E31u);
    for (unsigned n = 0; n != 1000; ++n) {
        Machine interpreted(program), translated(program);
        State state = initial_state(translated, random);
        // The interpreter and translator see identical data bytes.
        std::memcpy(interpreted.memory.page[DATA >> 12], translated.memory.page[DATA >> 12], PAGE);
        std::memcpy(interpreted.cpu.reg, state.reg, sizeof state.reg);
        interpreted.cpu.eip = state.eip; interpreted.cpu.eflags = state.eflags;
        const uint32_t exit_eip = executable(state);
        if (exit_eip != state.eip) throw std::runtime_error("generated function return did not match guest EIP");
        std::memcpy(translated.cpu.reg, state.reg, sizeof state.reg);
        translated.cpu.eip = state.eip; translated.cpu.eflags = state.eflags;
        if (!run_to_hlt(translated.cpu, CODE + uint32_t(program.size() - 1))) throw std::runtime_error("translated tail did not reach hlt");
        if (!run_to_hlt(interpreted.cpu, CODE + uint32_t(program.size() - 1))) throw std::runtime_error("interpreter did not reach hlt");
        State final_state;
        std::memcpy(final_state.reg, translated.cpu.reg, sizeof final_state.reg);
        final_state.eflags = translated.cpu.eflags; final_state.eip = translated.cpu.eip;
        final_state.memory = translated.memory.page[DATA >> 12];
        if (std::memcmp(final_state.reg, interpreted.cpu.reg, sizeof final_state.reg) != 0 ||
            ((final_state.eflags ^ interpreted.cpu.eflags) & FLAG_MASK) != 0 ||
            final_state.eip != interpreted.cpu.eip ||
            std::memcmp(translated.memory.page[DATA >> 12], interpreted.memory.page[DATA >> 12], PAGE) != 0) {
            std::fprintf(stderr, "DBT differential mismatch at case %u (translated eip=%08x interp eip=%08x)\n",
                         n, final_state.eip, interpreted.cpu.eip);
            return 1;
        }
    }
    std::puts("X86-DBT PASS: translated integer block matched interpreter on 1000 randomized states (including memory, LEA and JZ exit)");
    return 0;
}

struct RuntimeMachine {
    Memory memory;
    ThunkTable thunks;
    Cpu cpu{memory, thunks};
    RuntimeMachine() {
        if (!memory.base()) throw std::runtime_error("DBT randomized tests require reserved-base memory");
        memory.map(CODE, PAGE, PROT_R | PROT_W);
        memory.map(DATA, 3 * PAGE, PROT_R | PROT_W);
    }
    void load(const std::vector<uint8_t>& code) {
        memory.protect(CODE, PAGE, PROT_R | PROT_W);
        memory.write_bytes(CODE, code.data(), uint32_t(code.size()));
        memory.protect(CODE, PAGE, PROT_R | PROT_X);
        cpu.eip = CODE; cpu.executed = 0; cpu.eflags = 2;
    }
};
static void runtime_compare(const std::vector<uint8_t>& code, unsigned instructions, std::mt19937& rng,
                            const uint32_t initial[8], uint32_t flags, const uint8_t data[3 * PAGE],
                            uint64_t& translated_cases, uint32_t compared_flags = FLAG_MASK,
                            const uint32_t initial_xmm[8][4] = nullptr,
                            const Fpu* initial_fpu = nullptr) {
    static RuntimeMachine translated, interpreted;
    translated.load(code); interpreted.load(code);
    translated.memory.write_bytes(DATA, data, 3 * PAGE); interpreted.memory.write_bytes(DATA, data, 3 * PAGE);
    std::memcpy(translated.cpu.reg, initial, sizeof translated.cpu.reg);
    std::memcpy(interpreted.cpu.reg, initial, sizeof interpreted.cpu.reg);
    if (initial_xmm) {
        std::memcpy(translated.cpu.xmm.r, initial_xmm, sizeof translated.cpu.xmm.r);
        std::memcpy(interpreted.cpu.xmm.r, initial_xmm, sizeof interpreted.cpu.xmm.r);
    }
    if (initial_fpu) {
        translated.cpu.fpu = *initial_fpu;
        interpreted.cpu.fpu = *initial_fpu;
    }
    translated.cpu.eflags = interpreted.cpu.eflags = flags;
    dbt::RuntimeState state;
    std::memcpy(state.reg, initial, sizeof state.reg);
    if (initial_xmm) std::memcpy(state.xmm, initial_xmm, sizeof state.xmm);
    if (initial_fpu) state.fpu = *initial_fpu;
    state.eip = CODE; state.eflags = flags; state.memory = translated.memory.base();
    std::vector<uint8_t> translated_code = dbt::runtime_translate(code, CODE);
    dbt::Executable executable(translated_code);
    uint32_t next = executable(state);
    if (next != state.eip) throw std::runtime_error("runtime DBT returned a different EIP");
    for (unsigned i = 0; i < instructions; ++i) interpreted.cpu.step();
    const bool fpu_mismatch = initial_fpu && (std::memcmp(state.fpu.st, interpreted.cpu.fpu.st, sizeof state.fpu.st) ||
        std::memcmp(state.fpu.empty, interpreted.cpu.fpu.empty, sizeof state.fpu.empty) ||
        state.fpu.top != interpreted.cpu.fpu.top || state.fpu.cw != interpreted.cpu.fpu.cw ||
        state.fpu.sw != interpreted.cpu.fpu.sw);
    if (std::memcmp(state.reg, interpreted.cpu.reg, sizeof state.reg) ||
        (initial_xmm && std::memcmp(state.xmm, interpreted.cpu.xmm.r, sizeof state.xmm)) ||
        fpu_mismatch || ((state.eflags ^ interpreted.cpu.eflags) & compared_flags) || state.eip != interpreted.cpu.eip ||
        std::memcmp(translated.memory.base() + DATA, interpreted.memory.base() + DATA, 3 * PAGE)) {
        std::fprintf(stderr,"mismatch case=%llu code:",static_cast<unsigned long long>(translated_cases));
        for(uint8_t b:code) std::fprintf(stderr," %02x",b);
        std::fprintf(stderr,"\nflags=%08x/%08x eip=%08x/%08x\n",state.eflags,interpreted.cpu.eflags,state.eip,interpreted.cpu.eip);
        for(unsigned i=0;i<8;++i) if(state.reg[i]!=interpreted.cpu.reg[i]) std::fprintf(stderr,"reg%u=%08x/%08x\n",i,state.reg[i],interpreted.cpu.reg[i]);
        (void)rng;
        throw std::runtime_error("randomized runtime DBT state differs from interpreter");
    }
    ++translated_cases;
}
static void direct_jump_differential() {
    std::mt19937 rng(0x4A4D5021u); uint8_t data[3 * PAGE] = {}; uint64_t cases = 0;
    const uint32_t regs[8] = {};
    runtime_compare({0xEB, 0x05, 0xB8, 1, 0, 0, 0, 0xB8, 2, 0, 0, 0}, 1, rng, regs, 2, data, cases);
    runtime_compare({0xB8, 1, 0, 0, 0, 0xEB, 0xF9, 0xB8, 2, 0, 0, 0}, 2, rng, regs, 2, data, cases);
    runtime_compare({0xE9, 0x05, 0, 0, 0, 0xB8, 1, 0, 0, 0, 0xB8, 2, 0, 0, 0}, 1, rng, regs, 2, data, cases);
    runtime_compare({0xB8, 1, 0, 0, 0, 0xE9, 0xF6, 0xff, 0xff, 0xff, 0xB8, 2, 0, 0, 0}, 2, rng, regs, 2, data, cases);

    auto run = [](const std::vector<uint8_t>& code, uint32_t stop, uint32_t expected_eax,
                  const char* failure) {
        RuntimeMachine translated, interpreted;
        translated.load(code); interpreted.load(code);
        translated.cpu.dbt_enabled = true; interpreted.cpu.dbt_enabled = false;
        translated.cpu.run(stop); interpreted.cpu.run(stop);
        if (translated.cpu.reg[EAX] != expected_eax ||
            std::memcmp(translated.cpu.reg, interpreted.cpu.reg, sizeof translated.cpu.reg) ||
            translated.cpu.eip != interpreted.cpu.eip || translated.cpu.eflags != interpreted.cpu.eflags ||
            translated.cpu.translated_blocks < 2 || translated.cpu.interpreted_blocks != 0)
            throw std::runtime_error(failure);
    };
    run({0xEB, 0x02, 0x90, 0x90, 0xB8, 0x2a, 0, 0, 0, 0xF4}, CODE + 9, 42,
        "forward jmp target was not translated by the DBT dispatcher");
    {
        RuntimeMachine m; m.load({0xEB, 0x05, 0xB8, 1, 0, 0, 0, 0xB8, 0x2a, 0, 0, 0, 0xF4});
        m.cpu.dbt_enabled = true; m.cpu.run(CODE + 12);
        auto block = m.cpu.dbt_cache.find(CODE);
        if (block == m.cpu.dbt_cache.end() || block->second.guest_length != 2)
            throw std::runtime_error("jmp did not end the collected DBT block");
    }
    run({0xB9, 3, 0, 0, 0, 0x83, 0xE9, 1, 0x75, 0x02, 0xEB, 0x02, 0xEB, 0xF7, 0xF4}, CODE + 14, 0,
        "jcc+jmp loop differed from the interpreter");
    std::printf("X86-DBT PASS: direct rel8/rel32 forward/backward block exits, block-start jmp, late target and jcc+jmp loop matched interpreter (%llu cases)\n",
                static_cast<unsigned long long>(cases));
}

static void randomized_sse_differential() {
    std::mt19937 rng(0x53534532u); uint8_t data[3 * PAGE]; uint64_t cases = 0;
    auto one = [&](const std::vector<uint8_t>& code, bool memory = false) {
        uint32_t regs[8] = {}; uint32_t xmm[8][4];
        for (uint32_t& r : regs) r = rng();
        for (auto& row : xmm) for (uint32_t& r : row) r = rng();
        for (uint8_t& b : data) b = uint8_t(rng());
        if (memory) { regs[EAX] = DATA + 0x200; regs[ECX] = 0; }
        runtime_compare(code, 1, rng, regs, 2u | (rng() & FLAG_MASK), data, cases, FLAG_MASK, xmm);
    };
    for (unsigned n = 0; n != 5000; ++n) {
        unsigned dst = rng() & 7, src = rng() & 7;
        one({0x66, 0x0F, 0x6E, uint8_t(0xC0 | (dst << 3) | src)});
    }
    for (unsigned n = 0; n != 5000; ++n) {
        unsigned dst = rng() & 7, src = rng() & 7;
        one({0x66, 0x0F, 0x70, uint8_t(0xC0 | (dst << 3) | src), uint8_t(rng())});
    }
    for (unsigned n = 0; n != 5000; ++n) {
        unsigned dst = rng() & 7, src = rng() & 7;
        one({0x0F, uint8_t(0x58 + (rng() & 1)), uint8_t(0xC0 | (dst << 3) | src)});
        one({0x0F, uint8_t(0x58 + (rng() & 1)), uint8_t((dst << 3) | 4), 0x20}, true);
    }
    for (unsigned n = 0; n != 5000; ++n) {
        unsigned group = n & 1, dst = rng() & 7;
        one({0x66, 0x0F, 0x72, uint8_t(0xC0 | (group ? 6 : 2) << 3 | dst), uint8_t(rng())});
    }
    for (unsigned n = 0; n != 5000; ++n) {
        unsigned dst = rng() & 7;
        one({0x66, 0x0F, 0x73, uint8_t(0xC0 | (2 << 3) | dst), uint8_t(rng())});
    }
    for (unsigned n = 0; n != 5000; ++n) {
        unsigned dst = rng() & 7, src = rng() & 7;
        one({0x66, 0x0F, 0x6F, uint8_t(0xC0 | (dst << 3) | src)});
        one({0x66, 0x0F, 0x7F, uint8_t(0xC0 | (dst << 3) | src)});
    }
    for (unsigned n = 0; n != 5000; ++n) {
        unsigned dst = rng() & 7, src = rng() & 7;
        one({0x66, 0x0F, 0x6F, uint8_t((dst << 3) | 4), 0x20}, true);
        one({0x66, 0x0F, 0x7F, uint8_t((src << 3) | 4), 0x20}, true);
    }
    for (uint8_t op : {uint8_t(0x62), uint8_t(0x76), uint8_t(0xDB), uint8_t(0xEB), uint8_t(0xF4), uint8_t(0xFA), uint8_t(0xFE)}) {
        for (unsigned n = 0; n != 5000; ++n) {
            unsigned dst = rng() & 7, src = rng() & 7;
            one({0x66, 0x0F, op, uint8_t(0xC0 | (dst << 3) | src)});
        }
    }
    std::printf("X86-DBT PASS: SSE2 DBT differential families matched interpreter (5000 cases per family)\n");
}

static void extended_sse_differential() {
    std::mt19937 rng(0x45585453u); uint8_t data[3 * PAGE]; uint64_t cases = 0;
    auto one = [&](std::vector<uint8_t> code, bool memory) {
        uint32_t regs[8] = {}; uint32_t xmm[8][4];
        for (uint32_t& r : regs) r = rng();
        for (auto& row : xmm) for (uint32_t& r : row) r = rng();
        for (uint8_t& b : data) b = uint8_t(rng());
        if (memory) { regs[EAX] = DATA + 0x200; regs[ECX] = 0; }
        runtime_compare(code, 1, rng, regs, 2u | (rng() & FLAG_MASK), data, cases, FLAG_MASK, xmm);
    };
    for (unsigned n = 0; n != 5000; ++n) {
        unsigned dst = rng() & 7, src = rng() & 7;
        one({0x0f, 0x10, uint8_t(0xc0 | (dst << 3) | src)}, false);
        one({0x0f, 0x10, uint8_t((dst << 3) | 4), 0x20}, true);
        one({0x0f, 0x28, uint8_t(0xc0 | (dst << 3) | src)}, false);
        one({0x0f, 0x28, uint8_t((dst << 3) | 4), 0x20}, true);
    }
    for (unsigned n = 0; n != 5000; ++n) {
        unsigned dst = rng() & 7, src = rng() & 7;
        one({0x0f, 0x11, uint8_t(0xc0 | (dst << 3) | src)}, false);
        one({0x0f, 0x11, uint8_t((dst << 3) | 4), 0x20}, true);
        one({0x0f, 0x29, uint8_t(0xc0 | (dst << 3) | src)}, false);
        one({0x0f, 0x29, uint8_t((dst << 3) | 4), 0x20}, true);
    }
    for (uint8_t prefix : {uint8_t(0xf2), uint8_t(0xf3)}) {
        for (unsigned n = 0; n != 5000; ++n) {
            unsigned dst = rng() & 7, src = rng() & 7;
            one({prefix, 0x0f, 0x10, uint8_t(0xc0 | (dst << 3) | src)}, false);
            one({prefix, 0x0f, 0x10, uint8_t((dst << 3) | 4), 0x20}, true);
            one({prefix, 0x0f, 0x58, uint8_t(0xc0 | (dst << 3) | src)}, false);
            one({prefix, 0x0f, 0x59, uint8_t((dst << 3) | 4), 0x20}, true);
        }
    }
    for (unsigned n = 0; n != 5000; ++n) {
        unsigned xmm_src = rng() & 7, gp_dst = rng() & 7;
        one({0x66, 0x0f, 0x7e, uint8_t(0xc0 | (xmm_src << 3) | gp_dst)}, false);
        one({0x66, 0x0f, 0x7e, uint8_t((xmm_src << 3) | 4), 0x20}, true);
    }
    for (uint8_t prefix : {uint8_t(0), uint8_t(0x66)}) {
        for (unsigned n = 0; n != 5000; ++n) {
            unsigned dst = rng() & 7, src = rng() & 7;
            std::vector<uint8_t> reg = {0x0f, 0xc6, uint8_t(0xc0 | (dst << 3) | src), uint8_t(rng())};
            std::vector<uint8_t> mem = {0x0f, 0xc6, uint8_t((dst << 3) | 4), 0x20, uint8_t(rng())};
            if (prefix) { reg.insert(reg.begin(), prefix); mem.insert(mem.begin(), prefix); }
            one(reg, false);
            one(mem, true);
        }
    }
    std::printf("X86-DBT PASS: extended SSE/x87 opcode families matched interpreter (5000 cases per family)\n");
}

static void partial_block_differential() {
    static RuntimeMachine translated, interpreted;
    std::mt19937 rng(0x50415254u);
    for (unsigned n = 0; n != 5000; ++n) {
        std::vector<uint8_t> code = {0xB8, uint8_t(rng()), uint8_t(rng()), uint8_t(rng()), uint8_t(rng()),
                                     0x90, 0x83, 0xC0, 0x02, 0xF4};
        translated.load(code); interpreted.load(code);
        translated.cpu.clear_dbt_cache(); interpreted.cpu.clear_dbt_cache();
        translated.cpu.translated_blocks = translated.cpu.interpreted_blocks = 0;
        interpreted.cpu.translated_blocks = interpreted.cpu.interpreted_blocks = 0;
        translated.cpu.dbt_enabled = true; interpreted.cpu.dbt_enabled = false;
        translated.cpu.run(CODE + uint32_t(code.size() - 1));
        interpreted.cpu.run(CODE + uint32_t(code.size() - 1));
        if (translated.cpu.reg[EAX] != interpreted.cpu.reg[EAX] ||
            translated.cpu.eip != interpreted.cpu.eip || translated.cpu.eflags != interpreted.cpu.eflags ||
            translated.cpu.interpreted_blocks != 1 || translated.cpu.translated_blocks != 2) {
            throw std::runtime_error("partial DBT block did not resume after one interpreted instruction");
        }
    }
    std::printf("X86-DBT PASS: partial blocks resumed after one interpreter instruction (5000 cases)\n");
}

static bool same_fpu(const Fpu& a, const Fpu& b) {
    return !std::memcmp(a.st, b.st, sizeof a.st) && !std::memcmp(a.empty, b.empty, sizeof a.empty) &&
        a.top == b.top && a.cw == b.cw && a.sw == b.sw;
}
static void scalar_load_preserves_live_scratch() {
    std::mt19937 rng(0x5343414cu); uint8_t data[3 * PAGE] = {}; uint64_t cases = 0;
    for (uint8_t prefix : {uint8_t(0xf2), uint8_t(0xf3)}) {
        uint32_t regs[8] = {}, xmm[8][4] = {}; regs[EAX] = 0xa5a5c3c3u;
        std::memset(xmm[6], 0x5a, sizeof xmm[6]);
        std::vector<uint8_t> code = {0x66, 0x0f, 0x6e, 0xf0, prefix, 0x0f, 0x10, 0x0d,
                                     uint8_t(DATA + 0x200), uint8_t((DATA + 0x200) >> 8),
                                     uint8_t((DATA + 0x200) >> 16), uint8_t((DATA + 0x200) >> 24),
                                     0x66, 0x0f, 0x7e, 0xf0};
        runtime_compare(code, 3, rng, regs, 2, data, cases, FLAG_MASK, xmm);
    }
    std::puts("X86-DBT PASS: scalar movsd/movss loads preserve live scratch XMM state");
}
static void chained_state_differential() {
    RuntimeMachine translated, interpreted;
    std::vector<uint8_t> code = {0x66, 0x0f, 0x6e, 0xf0, 0xb8, 1, 0, 0, 0, 0x39, 0xc0, 0x74, 0,
                                 0xd9, 0x05, uint8_t(DATA + 0x100), uint8_t((DATA + 0x100) >> 8),
                                 uint8_t((DATA + 0x100) >> 16), uint8_t((DATA + 0x100) >> 24),
                                 0xbb, 2, 0, 0, 0, 0x39, 0xdb, 0x74, 0,
                                 0xb9, 3, 0, 0, 0, 0x39, 0xc9, 0x74, 0};
    const uint32_t block_b = CODE + 13, stop = CODE + 28, stop2 = stop + 9;
    translated.load(code); interpreted.load(code); translated.memory.w32(DATA + 0x100, 0x40400000u);
    interpreted.memory.w32(DATA + 0x100, 0x40400000u);
    translated.cpu.reg[EAX] = interpreted.cpu.reg[EAX] = 0x13579bdfu;
    translated.cpu.reg[ESP] = interpreted.cpu.reg[ESP] = DATA + 0x1800;
    translated.cpu.xmm.r[6][0] = interpreted.cpu.xmm.r[6][0] = 0xfeedfaceu;
    translated.cpu.fpu = interpreted.cpu.fpu = Fpu{};
    translated.cpu.dbt_enabled = true; interpreted.cpu.dbt_enabled = false;
    translated.cpu.eip = stop; translated.cpu.run(stop2);
    translated.cpu.eip = block_b; translated.cpu.run(stop2);
    std::memset(translated.cpu.reg, 0, sizeof translated.cpu.reg);
    translated.cpu.reg[EAX] = 0x13579bdfu; translated.cpu.reg[ESP] = DATA + 0x1800;
    std::memset(translated.cpu.xmm.r, 0, sizeof translated.cpu.xmm.r);
    translated.cpu.xmm.r[6][0] = 0xfeedfaceu; translated.cpu.fpu = Fpu{}; translated.cpu.eflags = 2;
    translated.cpu.eip = interpreted.cpu.eip = CODE;
    translated.cpu.run(stop); interpreted.cpu.run(stop);
    if (std::memcmp(translated.cpu.reg, interpreted.cpu.reg, sizeof translated.cpu.reg) ||
        std::memcmp(translated.cpu.xmm.r, interpreted.cpu.xmm.r, sizeof translated.cpu.xmm.r) ||
        !same_fpu(translated.cpu.fpu, interpreted.cpu.fpu) || translated.cpu.eip != interpreted.cpu.eip)
        throw std::runtime_error("chained DBT state was not synchronized at stop");
    std::puts("X86-DBT PASS: chained stop synchronized GPR, XMM and x87 state");
}
static void chained_fault_preserves_xmm() {
    RuntimeMachine translated, interpreted;
    const uint32_t fault_ea = DATA + 0x200;
    std::vector<uint8_t> code = {0x66, 0x0f, 0x6e, 0xf0, 0x39, 0xc0, 0x74, 0,
                                 0x8b, 0x05, uint8_t(fault_ea), uint8_t(fault_ea >> 8),
                                 uint8_t(fault_ea >> 16), uint8_t(fault_ea >> 24), 0xf4};
    const uint32_t block_b = CODE + 8, stop = block_b + 6;
    translated.load(code); interpreted.load(code);
    translated.cpu.reg[EAX] = interpreted.cpu.reg[EAX] = 0x2468ace0u;
    std::memset(translated.cpu.xmm.r, 0x33, sizeof translated.cpu.xmm.r);
    std::memcpy(interpreted.cpu.xmm.r, translated.cpu.xmm.r, sizeof translated.cpu.xmm.r);
    translated.cpu.dbt_enabled = true; interpreted.cpu.dbt_enabled = false;
    translated.cpu.eip = block_b; translated.cpu.run(stop);
    if (::mprotect(translated.memory.base() + DATA, PAGE, PROT_NONE) != 0)
        throw std::runtime_error("could not make the cached fault page inaccessible");
    interpreted.memory.protect(DATA, PAGE, 0);
    translated.cpu.reg[EAX] = 0x2468ace0u; translated.cpu.eflags = 2;
    translated.cpu.eip = interpreted.cpu.eip = CODE;
    bool translated_fault = false, interpreted_fault = false;
    try { translated.cpu.run(stop); } catch (const Fault& f) { translated_fault = f.kind == FaultKind::AccessViolation; }
    try { interpreted.cpu.run(stop); } catch (const Fault& f) { interpreted_fault = f.kind == FaultKind::AccessViolation; }
    if (!translated_fault || !interpreted_fault ||
        std::memcmp(translated.cpu.xmm.r, interpreted.cpu.xmm.r, sizeof translated.cpu.xmm.r))
        throw std::runtime_error("non-XMM host fault lost XMM state from an earlier chained block");
    std::puts("X86-DBT PASS: non-XMM host fault preserved earlier chained XMM state");
}

static void randomized_runtime_differential() {
    std::mt19937 rng(0x58424454u); uint64_t address_cases = 0, alu_cases = 0;
    uint8_t data[3 * PAGE];
    for (unsigned n = 0; n != 5000; ++n) {
        uint32_t regs[8]; for (uint32_t& r : regs) r = rng();
        for (uint8_t& b : data) b = uint8_t(rng());
        uint32_t flags = 2u | (rng() & FLAG_MASK);
        std::vector<uint8_t> code; unsigned count = 1;
        unsigned which = n % 6, address_mode = (n / 6) % 8;
        auto emit_mapped_address=[&](uint8_t opcode,uint8_t field,unsigned mode) {
            uint8_t mod=0,rm=0; uint32_t disp=0; bool has_sib=false;
            static const uint8_t bases[]={0,1,2,3,6,7};
            uint8_t base=bases[rng()%6], index=uint8_t(rng()%3);
            uint8_t scale=uint8_t(rng()&3);
            switch(mode) {
            case 0: disp=DATA+(rng()%(3*PAGE-4)); rm=5; break;
            case 1: base=bases[rng()%6]; regs[base]=DATA+0x400; rm=base; break;
            case 2: mod=1; regs[base]=DATA+0x400; disp=uint32_t(rng()%128); rm=base; break;
            case 3: mod=2; regs[base]=DATA+0x400; disp=uint32_t(rng()%512); rm=base; break;
            case 4: mod=1; has_sib=true; base=4; index=uint8_t(rng()%3); regs[base]=DATA+0x400; regs[index]=3; disp=8; break;
            case 5: has_sib=true; base=5; index=uint8_t(rng()%3); regs[index]=3; disp=DATA+0x300; break;
            case 6: mod=2; has_sib=true; base=uint8_t(3+ rng()%5); if(base==4 || base==5) base=6; index=uint8_t(rng()%3); regs[base]=DATA+0x400; regs[index]=3; disp=32; break;
            default: has_sib=true; base=4; index=4; regs[base]=DATA+0x400; break;
            }
            rm=has_sib?4:rm; code={opcode,uint8_t((mod<<6)|(field<<3)|rm)};
            if(has_sib) code.push_back(uint8_t((scale<<6)|(index<<3)|base));
            if(mod==1) code.push_back(uint8_t(disp));
            else if(mod==2 || (mod==0 && (rm==5 || (has_sib && base==5))))
                for(unsigned k=0;k<4;++k) code.push_back(uint8_t(disp>>(8*k)));
        };
        if (which == 0) { // mov r32,r/m32 across mapped base/SIB forms
            unsigned dst=rng()&7; emit_mapped_address(0x8B,uint8_t(dst),address_mode);
        } else if (which == 1) { // mov r/m32,r32 across mapped base/SIB forms
            unsigned src=rng()&7; emit_mapped_address(0x89,uint8_t(src),address_mode);
        } else if (which == 2) { // movzx/movsx register or mapped memory source
            uint8_t op2[] = {0xB6,0xB7,0xBE,0xBF}; uint8_t x = op2[rng() & 3]; unsigned dst=rng()&7;
            if ((n/6)&1) emit_mapped_address(0x0F,uint8_t(dst),address_mode), code.insert(code.begin()+1,x);
            else code={0x0F,x,uint8_t(0xC0 | (dst<<3) | (rng()&7))};
        } else if (which == 3) { // LEA exercises every ModRM/SIB displacement shape
            unsigned dst=rng()&7, mode=rng()%5, base=rng()&7, index=rng()&7, scale=rng()&3;
            int32_t disp=int32_t(rng());
            if (mode==0) { code={0x8D,uint8_t((dst<<3)|5)}; for(unsigned k=0;k<4;++k) code.push_back(uint8_t(uint32_t(disp)>>(8*k))); }
            else if (mode==1) { regs[base]=DATA+0x300; code={0x8D,uint8_t(0x40|(dst<<3)|(base==4?4:base))}; if(base==4) code.push_back(0x24); code.push_back(uint8_t(disp)); }
            else if (mode==2) { regs[base]=DATA+0x300; code={0x8D,uint8_t(0x80|(dst<<3)|(base==4?4:base))}; if(base==4) code.push_back(0x24); for(unsigned k=0;k<4;++k) code.push_back(uint8_t(uint32_t(disp)>>(8*k))); }
            else if (mode==3) { regs[base]=DATA+0x300; regs[index]=3; code={0x8D,uint8_t(0x40|(dst<<3)|4),uint8_t((scale<<6)|(index<<3)|base),uint8_t(disp)}; }
            else { regs[index]=3; code={0x8D,uint8_t((dst<<3)|4),uint8_t((scale<<6)|(index<<3)|5)}; for(unsigned k=0;k<4;++k) code.push_back(uint8_t(uint32_t(disp)>>(8*k))); }
        } else { // LEA also covers register-indirect and base+disp8/disp32
            unsigned dst=rng()&7, base=rng()&7; regs[base]=DATA+0x300; int32_t disp=int32_t(rng());
            bool wide=which==5; code={0x8D,uint8_t((wide?0x80:0x40)|(dst<<3)|(base==4?4:base))}; if(base==4) code.push_back(0x24);
            if(wide) for(unsigned k=0;k<4;++k) code.push_back(uint8_t(uint32_t(disp)>>(8*k))); else code.push_back(uint8_t(disp));
        }
        runtime_compare(code,count,rng,regs,flags,data,address_cases);
    }
    for (unsigned n = 0; n != 5000; ++n) {
        uint32_t regs[8]; for (uint32_t& r : regs) r = rng();
        for (uint8_t& b : data) b = uint8_t(rng());
        uint32_t flags = 2u | (rng() & FLAG_MASK); std::vector<uint8_t> code;
        unsigned kind=n%4, op=rng()%8, dst=rng()&7, src=rng()&7; bool memory=(rng()%4)==0;
        static const uint8_t rmreg[8]={0x01,0x11,0x19,0x21,0x09,0x29,0x31,0x39};
        auto emit_memory_operand=[&](uint8_t opcode,uint8_t field) {
            uint32_t ea=DATA+(rng()%(3*PAGE-4)); code={opcode,uint8_t((field<<3)|5)};
            for(unsigned k=0;k<4;++k) code.push_back(uint8_t(ea>>(8*k)));
        };
        if (kind==0) { // add/adc/sub/sbb/and/or/xor/cmp r/m32,r32
            uint8_t opcode=rmreg[op];
            if(memory) emit_memory_operand(opcode,src);
            else code={opcode,uint8_t(0xC0|(src<<3)|dst)};
        } else if (kind==1) { // immediate arithmetic/logical groups, including cmp
            unsigned group=rng()&7; bool short_imm=rng()&1; uint8_t opcode=uint8_t(short_imm?0x83:0x81);
            if(memory) emit_memory_operand(opcode,uint8_t(group));
            else code={opcode,uint8_t(0xC0|(group<<3)|dst)};
            if(short_imm) code.push_back(uint8_t(rng()));
            else { uint32_t imm=rng(); for(unsigned k=0;k<4;++k) code.push_back(uint8_t(imm>>(8*k))); }
        } else if (kind==2) { // test r/m32,r32 and test r/m32,imm32
            if(rng()&1) {
                if(memory) emit_memory_operand(0x85,uint8_t(src));
                else code={0x85,uint8_t(0xC0|(src<<3)|dst)};
            } else {
                uint8_t opcode=0xF7;
                if(memory) emit_memory_operand(opcode,0);
                else code={opcode,uint8_t(0xC0|dst)};
                uint32_t imm=rng(); for(unsigned k=0;k<4;++k) code.push_back(uint8_t(imm>>(8*k)));
            }
        } else { // not/neg/inc/dec r/m32
            bool arithmetic=(rng()&1)!=0; unsigned group=arithmetic?(rng()&1):2+(rng()&1);
            uint8_t opcode=arithmetic?0xFF:0xF7;
            if(memory) emit_memory_operand(opcode,uint8_t(group));
            else code={opcode,uint8_t(0xC0|(group<<3)|dst)};
        }
        runtime_compare(code,1,rng,regs,flags,data,alu_cases);
    }
    uint64_t shift_cases = 0;
    for (unsigned n = 0; n != 5000; ++n) {
        uint32_t regs[8]; for (uint32_t& r : regs) r = rng();
        for (uint8_t& b : data) b = uint8_t(rng());
        uint32_t flags = 2u | (rng() & FLAG_MASK); std::vector<uint8_t> code;
        unsigned group = n % 8, target = rng() & 7, form = n % 3;
        unsigned count = form == 0 ? 1 : unsigned(rng() & 0xff);
        if (form == 0) code = {0xD1, uint8_t(0xC0 | (group << 3) | target)};
        else if (form == 1) code = {0xC1, uint8_t(0xC0 | (group << 3) | target), uint8_t(count)};
        else { code = {0xD3, uint8_t(0xC0 | (group << 3) | target)}; regs[ECX] = count; }
        unsigned effective = (form == 2 ? regs[ECX] : count) & 31;
        uint32_t compared = FLAG_MASK;
        if (effective) {
            if (group <= 3) { if (effective != 1) compared &= ~uint32_t(OF); }
            else {
                compared = DF | CF | PF | ZF | SF;
                if (effective == 1) compared |= OF;
            }
        }
        runtime_compare(code,1,rng,regs,flags,data,shift_cases,compared);
    }
    uint64_t stack_cases = 0;
    for (unsigned n = 0; n != 5000; ++n) {
        uint32_t regs[8]; for (uint32_t& r : regs) r = rng();
        for (uint8_t& b : data) b = uint8_t(rng());
        regs[ESP] = DATA + 0x1800; regs[EBP] = DATA + 0x1c00;
        std::vector<uint8_t> code; unsigned kind = n % 9;
        switch (kind) {
        case 0: code = {uint8_t(0x50 | (rng() & 7))}; break;
        case 1: code = {uint8_t(0x58 | (rng() & 7))}; break;
        case 2: code = {0x68, uint8_t(rng()), uint8_t(rng()), uint8_t(rng()), uint8_t(rng())}; break;
        case 3: code = {0x6A, uint8_t(rng())}; break;
        case 4: { uint32_t ea = DATA + 0x200 + (rng() & 0x3fc); code = {0xFF, 0x35, uint8_t(ea), uint8_t(ea >> 8), uint8_t(ea >> 16), uint8_t(ea >> 24)}; break; }
        case 5: { uint32_t ea = DATA + 0x200 + (rng() & 0x3fc); code = {0x8F, 0x05, uint8_t(ea), uint8_t(ea >> 8), uint8_t(ea >> 16), uint8_t(ea >> 24)}; break; }
        case 6: code = {0xE8, 0x05, 0x00, 0x00, 0x00}; break;
        case 7: code = (rng() & 1) ? std::vector<uint8_t>{0xC3} : std::vector<uint8_t>{0xC2, 0x08, 0x00}; break;
        default: code = {0xC9}; break;
        }
        if (kind == 1 || kind == 5 || kind == 7) {
            uint32_t target = CODE + 0x400 + (rng() & 0x3fc); uint32_t at = regs[ESP];
            data[at - DATA] = uint8_t(target); data[at - DATA + 1] = uint8_t(target >> 8);
            data[at - DATA + 2] = uint8_t(target >> 16); data[at - DATA + 3] = uint8_t(target >> 24);
        } else if (kind == 8) {
            uint32_t target = CODE + 0x400 + (rng() & 0x3fc); uint32_t at = regs[EBP];
            data[at - DATA] = uint8_t(target); data[at - DATA + 1] = uint8_t(target >> 8);
            data[at - DATA + 2] = uint8_t(target >> 16); data[at - DATA + 3] = uint8_t(target >> 24);
        }
        runtime_compare(code, 1, rng, regs, 2u | (rng() & FLAG_MASK), data, stack_cases);
    }
    std::printf("X86-DBT PASS: randomized runtime differential blocks address=%llu alu=%llu shifts=%llu stack=%llu (5000 each)\n",
                (unsigned long long)address_cases, (unsigned long long)alu_cases,
                (unsigned long long)shift_cases, (unsigned long long)stack_cases);
}

static void x87_differential() {
    std::mt19937 rng(0x58383744u);
    uint8_t data[3 * PAGE];
    uint64_t cases = 0;
    auto append_address = [](std::vector<uint8_t>& code, uint32_t address) {
        for (unsigned i = 0; i != 4; ++i) code.push_back(uint8_t(address >> (i * 8)));
    };
    auto set32 = [](uint8_t* p, uint32_t value) {
        for (unsigned i = 0; i != 4; ++i) p[i] = uint8_t(value >> (i * 8));
    };
    auto set64 = [](uint8_t* p, uint64_t value) {
        for (unsigned i = 0; i != 8; ++i) p[i] = uint8_t(value >> (i * 8));
    };
    for (unsigned n = 0; n != 50000; ++n) {
        uint32_t regs[8] = {};
        for (uint32_t& r : regs) r = rng();
        for (uint8_t& b : data) b = uint8_t(rng());
        Fpu fpu;
        fpu.cw = uint16_t(0x037f | ((rng() & 3u) << 10));
        fpu.sw = uint16_t(rng() & 0x4700u);
        for (unsigned i = 0; i != 3; ++i) {
            int value = int(rng() % 199) - 99;
            if (!value) value = 1;
            fpu.push(Wide(value));
        }
        std::vector<uint8_t> code;
        const uint32_t address = DATA + 0x100;
        const unsigned family = n % 10;
        if (family == 0) {
            int value = int(rng() % 199) - 99; if (!value) value = 1;
            float input = float(value); uint32_t bits; std::memcpy(&bits, &input, 4); set32(data + 0x100, bits);
            unsigned group = n % 8;
            code = {0xD8, uint8_t((group << 3) | 5)}; append_address(code, address);
        } else if (family == 1) {
            int value = int(rng() % 199) - 99; if (!value) value = 1;
            double input = double(value) + 0.25; uint64_t bits; std::memcpy(&bits, &input, 8); set64(data + 0x100, bits);
            unsigned group = n % 8;
            code = {0xDC, uint8_t((group << 3) | 5)}; append_address(code, address);
        } else if (family == 2) {
            unsigned group = n % 8;
            code = {0xD8, uint8_t(0xC0 | (group << 3) | 1)};
        } else if (family == 3) {
            code = {0xD9, uint8_t(0xC0 | ((n & 1) ? 8 : 0) | 1)};
        } else if (family == 4) {
            unsigned group = 1 + (n % 5);
            code = {0xDD, uint8_t(0xC0 | (group << 3) | 1)};
        } else if (family == 5) {
            int value = int(rng() % 200000) - 100000; if (!value) value = 7;
            set32(data + 0x100, uint32_t(value));
            uint8_t op = (n & 1) ? 0xDF : 0xDB;
            unsigned group = (n & 2) ? 3 : 0;
            code = {op, uint8_t((group << 3) | 5)}; append_address(code, address);
        } else if (family == 6) {
            int value = int(rng() % 199) - 99; if (!value) value = 1;
            float input = float(value); uint32_t bits; std::memcpy(&bits, &input, 4); set32(data + 0x100, bits);
            unsigned group = n % 4 == 0 ? 0 : (n % 4 == 1 ? 2 : 3);
            code = {0xD9, uint8_t((group << 3) | 5)}; append_address(code, address);
        } else if (family == 7) {
            int value = int(rng() % 199) - 99; if (!value) value = 1;
            double input = double(value) + 0.5; uint64_t bits; std::memcpy(&bits, &input, 8); set64(data + 0x100, bits);
            unsigned group = n % 4 == 0 ? 0 : (n % 4 == 1 ? 2 : (n % 4 == 2 ? 3 : 7));
            code = {0xDD, uint8_t((group << 3) | 5)}; append_address(code, address);
        } else if (family == 8) {
            uint8_t op = (n & 1) ? 0xDF : 0xDB;
            unsigned group = (n & 2) ? 6 : 5;
            code = {op, uint8_t(0xC0 | (group << 3) | 1)};
        } else {
            code = {0xD9, (n & 1) ? uint8_t(0xEE) : uint8_t(0xE8)};
        }
        runtime_compare(code, 1, rng, regs, 2u | (rng() & FLAG_MASK), data, cases, FLAG_MASK, nullptr, &fpu);
    }
    std::printf("X86-DBT PASS: x87 differential matched interpreter on 5000 cases per family (%llu total)\n",
                (unsigned long long)cases);
}

static uint32_t native_integer() {
    uint32_t x = 0x12345678u;
    for (uint32_t i = 0; i < 200000; ++i) x = (x ^ (i + 0x9e3779b9u)) * 33u + (x >> 7);
    return x;
}
static int benchmark() {
    auto code = integer_kernel();
    auto translated_bytes = translate(code, CODE, DATA);
    Executable executable(translated_bytes);
    constexpr unsigned iterations = 200000;
    std::array<double, 5> interpreted_ms{}, dbt_ms{};
    uint32_t expected = native_integer();
    for (unsigned sample = 0; sample != interpreted_ms.size(); ++sample) {
        Machine interpreted(code), translated(code);
        interpreted.cpu.reg[EAX] = translated.cpu.reg[EAX] = 0x12345678u;
        State state; state.reg[EAX] = 0x12345678u; state.memory = translated.memory.page[DATA >> 12];
        auto start = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < iterations; ++i) {
            interpreted.cpu.reg[ECX] = i; interpreted.cpu.eip = CODE;
            interpreted.run_steps(7);
        }
        auto mid = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < iterations; ++i) {
            state.reg[ECX] = i; state.eip = CODE; executable(state);
        }
        auto end = std::chrono::steady_clock::now();
        interpreted_ms[sample] = std::chrono::duration<double, std::milli>(mid - start).count();
        dbt_ms[sample] = std::chrono::duration<double, std::milli>(end - mid).count();
        if (interpreted.cpu.reg[EAX] != expected || state.reg[EAX] != expected)
            throw std::runtime_error("integer kernel result disagrees with native reference");
    }
    std::sort(interpreted_ms.begin(), interpreted_ms.end()); std::sort(dbt_ms.begin(), dbt_ms.end());
    const double speedup = interpreted_ms[2] / dbt_ms[2];
    std::printf("X86-DBT BENCH integer: iterations=%u interpreter_ms=%.3f dbt_ms=%.3f speedup=%.2fx native_result=%08x\n",
                iterations, interpreted_ms[2], dbt_ms[2], speedup, expected);
    return 0;
}
}

// inc bl must hit guest BL (host RSI low byte, which needs a REX prefix), not DH.
static void byte_register_unary_differential() {
    RuntimeMachine translated, interpreted;
    const std::vector<uint8_t> code = {0xBB, 0x11, 0, 0, 0, 0xFE, 0xC3, 0xF4};
    translated.load(code); interpreted.load(code);
    translated.cpu.dbt_enabled = true; interpreted.cpu.dbt_enabled = false;
    translated.cpu.run(CODE + 7); interpreted.cpu.run(CODE + 7);
    if (translated.cpu.reg[EBX] != 0x12 || std::memcmp(translated.cpu.reg, interpreted.cpu.reg, sizeof translated.cpu.reg) ||
        translated.cpu.interpreted_blocks != 0)
        throw std::runtime_error("inc bl differed from the interpreter");
    std::printf("X86-DBT PASS: byte-register inc bl matched the interpreter\n");
}

// A faulting cvtss2si must not leave the guest MXCSR on the host.
static void faulted_sse_convert_restores_host_mxcsr() {
    RuntimeMachine m;
    m.load({0xF3, 0x0F, 0x2D, 0x05, 0x00, 0x00, 0x00, 0x70, 0xF4});
    m.cpu.mxcsr = 0x3f80 | 0x2000;  // guest rounds toward +infinity
    const uint32_t host = _mm_getcsr();
    bool faulted = false;
    try { m.cpu.run(CODE + 8); } catch (const Fault&) { faulted = true; }
    if (!faulted || _mm_getcsr() != host) throw std::runtime_error("faulting cvtss2si leaked guest MXCSR to the host");
    std::printf("X86-DBT PASS: faulting cvtss2si restored the host MXCSR\n");
}

// SEH access-violation records need the read/write bit: a guest store fault is a write.
static void fault_reports_write_access() {
    RuntimeMachine m;
    bool store_is_write = false, load_is_read = false;
    try { m.memory.w32(0x70000000, 1); } catch (const Fault& f) { store_is_write = f.write; }
    try { m.memory.r32(0x70000000); } catch (const Fault& f) { load_is_read = !f.write; }
    if (!store_is_write || !load_is_read) throw std::runtime_error("unmapped store fault was not reported as a write");
    std::printf("X86-DBT PASS: unmapped store fault reports write access, load reports read\n");
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::strcmp(argv[1], "--bench") == 0) return benchmark();
        if (argc != 1) { std::fprintf(stderr, "usage: x86-dbt [--bench]\n"); return 2; }
        auto program = compare_program();
        Executable executable(translate(program, CODE, DATA));
        int rc = differential_test(executable);
        if (rc) return rc;
        direct_jump_differential();
        randomized_sse_differential();
        extended_sse_differential();
        x87_differential();
        partial_block_differential();
        scalar_load_preserves_live_scratch();
        chained_state_differential();
        chained_fault_preserves_xmm();
        randomized_runtime_differential();
        byte_register_unary_differential();
        faulted_sse_convert_restores_host_mxcsr();
        fault_reports_write_access();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "X86-DBT FAIL: %s\n", e.what()); return 1;
    }
}
