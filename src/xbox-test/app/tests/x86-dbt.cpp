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
#include <sys/mman.h>
#include <vector>
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

struct Emitter {
    std::vector<uint8_t> bytes;
    void b(uint8_t v) { bytes.push_back(v); }
    void d(uint32_t v) { for (unsigned i = 0; i != 4; ++i) b(uint8_t(v >> (i * 8))); }
    void patch32(size_t at, int32_t v) {
        uint32_t bits; std::memcpy(&bits, &v, sizeof bits);
        for (unsigned i = 0; i != 4; ++i) bytes.at(at + i) = uint8_t(bits >> (i * 8));
    }
    void rex(uint8_t r = 0, uint8_t x = 0, uint8_t base = 0, bool w = false) {
        uint8_t value = uint8_t(0x40 | (w ? 8 : 0) | ((r >> 3) << 2) | ((x >> 3) << 1) | (base >> 3));
        if (value != 0x40) b(value);
    }
    void modrm(uint8_t mod, uint8_t reg, uint8_t rm) { b(uint8_t((mod << 6) | ((reg & 7) << 3) | (rm & 7))); }
};

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

    size_t pc = 0;
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
            break;
        } else {
            char message[80]; std::snprintf(message, sizeof message, "unsupported guest opcode %02x at +%zx", op, pc - 1);
            throw std::runtime_error(message);
        }
    }
    if (pc == guest.size()) mov_imm(out, 11, guest_pc + uint32_t(pc));

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

struct Executable {
    void* address = MAP_FAILED;
    size_t size = PAGE;
    explicit Executable(const std::vector<uint8_t>& code) {
        address = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (address == MAP_FAILED) throw std::runtime_error("mmap RW failed");
        if (code.size() > size) throw std::runtime_error("translated block exceeds one page");
        std::memcpy(address, code.data(), code.size());
        if (mprotect(address, size, PROT_READ | PROT_EXEC) != 0) {
            munmap(address, size); address = MAP_FAILED; throw std::runtime_error("mprotect RX failed");
        }
        __builtin___clear_cache(static_cast<char*>(address), static_cast<char*>(address) + code.size());
    }
    ~Executable() { if (address != MAP_FAILED) munmap(address, size); }
    Executable(const Executable&) = delete;
    Executable& operator=(const Executable&) = delete;
    uint32_t operator()(State& state) const { return reinterpret_cast<uint32_t (*)(State*)>(address)(&state); }
};

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

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::strcmp(argv[1], "--bench") == 0) return benchmark();
        if (argc != 1) { std::fprintf(stderr, "usage: x86-dbt [--bench]\n"); return 2; }
        auto program = compare_program();
        Executable executable(translate(program, CODE, DATA));
        return differential_test(executable);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "X86-DBT FAIL: %s\n", e.what()); return 1;
    }
}
