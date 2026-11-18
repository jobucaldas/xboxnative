// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#if !defined(_WIN32)
#include <csignal>
#include <csetjmp>
#include <mutex>
#include <ucontext.h>
#endif
#include "x86_fpu.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace x86::dbt {
constexpr uint32_t runtime_page = 4096;
constexpr uint8_t runtime_prot_r = 1, runtime_prot_w = 2, runtime_prot_x = 4, runtime_prot_cow = 8;
struct Emitter {
    std::vector<uint8_t> bytes;
    void b(uint8_t v) { bytes.push_back(v); }
    void d(uint32_t v) { for (unsigned i = 0; i != 4; ++i) b(uint8_t(v >> (i * 8))); }
    void q(uint64_t v) { for (unsigned i = 0; i != 8; ++i) b(uint8_t(v >> (i * 8))); }
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

struct Executable {
#if defined(_WIN32)
    void* address = nullptr;
#else
    void* address = MAP_FAILED;
#endif
    size_t size = 4096;
    explicit Executable(const std::vector<uint8_t>& code) {
        if (code.size() > size) throw std::runtime_error("translated block exceeds one page");
#if defined(_WIN32)
        using AllocFromApp = LPVOID(WINAPI*)(LPVOID, SIZE_T, DWORD, DWORD);
        using ProtectFromApp = BOOL(WINAPI*)(LPVOID, SIZE_T, DWORD, PDWORD);
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        auto alloc = kernel ? reinterpret_cast<AllocFromApp>(GetProcAddress(kernel, "VirtualAllocFromApp")) : nullptr;
        auto protect = kernel ? reinterpret_cast<ProtectFromApp>(GetProcAddress(kernel, "VirtualProtectFromApp")) : nullptr;
        if (!alloc || !protect) throw std::runtime_error("UWP executable-memory APIs unavailable");
        address = alloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!address) throw std::runtime_error("VirtualAllocFromApp RW failed");
        std::memcpy(address, code.data(), code.size());
        DWORD old_protection = 0;
        if (!protect(address, size, PAGE_EXECUTE_READ, &old_protection)) {
            VirtualFree(address, 0, MEM_RELEASE); address = nullptr;
            throw std::runtime_error("VirtualProtectFromApp RX failed");
        }
        FlushInstructionCache(GetCurrentProcess(), address, code.size());
#else
        address = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (address == MAP_FAILED) throw std::runtime_error("mmap RW failed");
        std::memcpy(address, code.data(), code.size());
        if (mprotect(address, size, PROT_READ | PROT_EXEC) != 0) {
            munmap(address, size); address = MAP_FAILED;
            throw std::runtime_error("mprotect RX failed");
        }
        __builtin___clear_cache(static_cast<char*>(address), static_cast<char*>(address) + code.size());
#endif
    }
    ~Executable() {
#if defined(_WIN32)
        if (address) VirtualFree(address, 0, MEM_RELEASE);
#else
        if (address != MAP_FAILED) munmap(address, size);
#endif
    }
    Executable(const Executable&) = delete;
    Executable& operator=(const Executable&) = delete;
    template<class State> uint32_t operator()(State& state) const {
        return reinterpret_cast<uint32_t (*)(State*)>(address)(&state);
    }
};
// Runtime blocks share this fixed ABI with the generated-code dispatcher.
struct RuntimeState {
    uint32_t reg[8] = {};
    uint32_t eip = 0;
    uint32_t eflags = 2;
    uint8_t* memory = nullptr;
    uint32_t xmm[8][4] = {};
    Fpu fpu;
    uint32_t fallback_eip = 0xffffffffu;
    uint32_t exit_eip = 0xffffffffu;
};
static_assert(offsetof(RuntimeState, eip) == 32, "DBT state layout");
static_assert(offsetof(RuntimeState, eflags) == 36, "DBT state layout");
static_assert(offsetof(RuntimeState, memory) == 40, "DBT state layout");
static_assert(offsetof(RuntimeState, xmm) == 48, "DBT state layout");

inline uint16_t runtime_x87_u16(const uint8_t* p) {
    return uint16_t(p[0]) | uint16_t(p[1]) << 8;
}
inline uint32_t runtime_x87_u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint64_t runtime_x87_u64(const uint8_t* p) {
    uint64_t v = 0; for (unsigned i = 0; i != 8; ++i) v |= uint64_t(p[i]) << (i * 8); return v;
}
inline uint8_t runtime_x87_byte(uint64_t encoded, unsigned i) { return uint8_t(encoded >> (i * 8)); }

constexpr uint32_t runtime_CF = 1u, runtime_PF = 4u, runtime_ZF = 0x40u;
// x87 operations stay in the interpreter's architectural representation. The
// generated block calls this leaf for each x87 instruction, so TOP, physical
// tags, control/status words and the console's double-precision model all match
// the Cpu interpreter rather than leaking the host x87 register stack.
inline void runtime_x87(RuntimeState* state, uint64_t encoded, uint32_t length) {
    uint8_t bytes[8] = {};
    for (unsigned i = 0; i < length && i != sizeof bytes; ++i) bytes[i] = runtime_x87_byte(encoded, i);
    const uint8_t op = bytes[0], modrm = bytes[1];
    const unsigned mod = modrm >> 6, regno = (modrm >> 3) & 7, rm = modrm & 7;
    size_t p = 2;
    uint32_t ea = 0;
    if (mod != 3) {
        if (rm == 4) {
            const uint8_t sib = bytes[p++];
            const unsigned scale = sib >> 6, index = (sib >> 3) & 7, base = sib & 7;
            if (index != 4) ea += state->reg[index] << scale;
            if (base == 5 && mod == 0) ea += runtime_x87_u32(bytes + p), p += 4;
            else ea += state->reg[base];
        } else if (rm == 5 && mod == 0) ea = runtime_x87_u32(bytes + p), p += 4;
        else ea = state->reg[rm];
        if (mod == 1) ea += uint32_t(int32_t(int8_t(bytes[p++])));
        else if (mod == 2) ea += runtime_x87_u32(bytes + p);
    }
    Fpu& fpu = state->fpu;
    auto st = [&](int i) { return fpu.get(i); };
    auto setst = [&](int i, Wide v) { fpu.set(i, v); };
    auto mem = state->memory + ea;
    auto m16 = [&]() { return runtime_x87_u16(mem); };
    auto m32 = [&]() { uint32_t bits = runtime_x87_u32(mem); float v; std::memcpy(&v, &bits, 4); return Wide(v); };
    auto m64 = [&]() { uint64_t bits = runtime_x87_u64(mem); double v; std::memcpy(&v, &bits, 8); return Wide(v); };
    auto st32 = [&](Wide v) { float f = float(v); uint32_t bits; std::memcpy(&bits, &f, 4); for (unsigned i = 0; i != 4; ++i) mem[i] = uint8_t(bits >> (i * 8)); };
    auto st64 = [&](Wide v) { double d = double(v); uint64_t bits; std::memcpy(&bits, &d, 8); for (unsigned i = 0; i != 8; ++i) mem[i] = uint8_t(bits >> (i * 8)); };
    auto to_int = [&](Wide v) { return int64_t(fpu.round_int(v)); };
    auto compare = [&](Wide a, Wide b) { fpu.compare(a, b); };
    auto x87_flags = [&]() {
        state->eflags = (state->eflags & ~uint32_t(runtime_CF | runtime_PF | runtime_ZF)) | 2u;
        if (fpu.sw & 0x4000) state->eflags |= runtime_ZF;
        if (fpu.sw & 0x0100) state->eflags |= runtime_CF;
        if (fpu.sw & 0x0400) state->eflags |= runtime_PF;
    };
    if (mod != 3) {
        switch (op) {
        case 0xD8: { Wide v = m32(); switch (regno) {
            case 0: setst(0, st(0) + v); return; case 1: setst(0, st(0) * v); return;
            case 2: compare(st(0), v); return; case 3: compare(st(0), v); fpu.pop(); return;
            case 4: setst(0, st(0) - v); return; case 5: setst(0, v - st(0)); return;
            case 6: setst(0, st(0) / v); return; default: setst(0, v / st(0)); return; } }
        case 0xDC: { Wide v = m64(); switch (regno) {
            case 0: setst(0, st(0) + v); return; case 1: setst(0, st(0) * v); return;
            case 2: compare(st(0), v); return; case 3: compare(st(0), v); fpu.pop(); return;
            case 4: setst(0, st(0) - v); return; case 5: setst(0, v - st(0)); return;
            case 6: setst(0, st(0) / v); return; default: setst(0, v / st(0)); return; } }
        case 0xDA: case 0xDE: { Wide v = op == 0xDA ? Wide(int32_t(runtime_x87_u32(mem))) : Wide(int16_t(m16())); switch (regno) {
            case 0: setst(0, st(0) + v); return; case 1: setst(0, st(0) * v); return;
            case 2: compare(st(0), v); return; case 3: compare(st(0), v); fpu.pop(); return;
            case 4: setst(0, st(0) - v); return; case 5: setst(0, v - st(0)); return;
            case 6: setst(0, st(0) / v); return; default: setst(0, v / st(0)); return; } }
        case 0xD9: switch (regno) {
            case 0: fpu.push(m32()); return; case 2: st32(st(0)); return; case 3: st32(st(0)); fpu.pop(); return;
            case 5: fpu.cw = m16(); return; case 7: for (unsigned i = 0; i != 2; ++i) mem[i] = uint8_t(fpu.cw >> (i * 8)); return;
            default: return; }
        case 0xDD: switch (regno) {
            case 0: fpu.push(m64()); return; case 2: st64(st(0)); return; case 3: st64(st(0)); fpu.pop(); return;
            case 7: for (unsigned i = 0; i != 2; ++i) mem[i] = uint8_t((fpu.status() & ~uint16_t(0x7f)) >> (i * 8)); return;
            default: return; }
        case 0xDB: switch (regno) {
            case 0: fpu.push(Wide(int32_t(runtime_x87_u32(mem)))); return;
            case 3: { int32_t v = int32_t(to_int(st(0))); for (unsigned i = 0; i != 4; ++i) mem[i] = uint8_t(uint32_t(v) >> (i * 8)); fpu.pop(); return; }
            default: return; }
        case 0xDF: switch (regno) {
            case 0: fpu.push(Wide(int16_t(m16()))); return;
            case 3: { int16_t v = int16_t(to_int(st(0))); mem[0] = uint8_t(v); mem[1] = uint8_t(uint16_t(v) >> 8); fpu.pop(); return; }
            default: return; }
        default: return;
        }
    }
    switch (op) {
    case 0xD8: switch (regno) {
        case 0: setst(0, st(0) + st(rm)); return; case 1: setst(0, st(0) * st(rm)); return;
        case 2: compare(st(0), st(rm)); return; case 3: compare(st(0), st(rm)); fpu.pop(); return;
        case 4: setst(0, st(0) - st(rm)); return; case 5: setst(0, st(rm) - st(0)); return;
        case 6: setst(0, st(0) / st(rm)); return; default: setst(0, st(rm) / st(0)); return; }
    case 0xDC: switch (regno) {
        case 0: setst(rm, st(rm) + st(0)); return; case 1: setst(rm, st(rm) * st(0)); return;
        case 2: compare(st(rm), st(0)); return; case 3: compare(st(rm), st(0)); fpu.pop(); return;
        case 4: setst(rm, st(0) - st(rm)); return; case 5: setst(rm, st(rm) - st(0)); return;
        case 6: setst(rm, st(0) / st(rm)); return; default: setst(rm, st(rm) / st(0)); return; }
    case 0xDE: switch (regno) {
        case 0: setst(rm, st(rm) + st(0)); fpu.pop(); return; case 1: setst(rm, st(rm) * st(0)); fpu.pop(); return;
        case 3: if (rm == 1) { compare(st(0), st(1)); fpu.pop(); fpu.pop(); } return;
        case 4: setst(rm, st(0) - st(rm)); fpu.pop(); return; case 5: setst(rm, st(rm) - st(0)); fpu.pop(); return;
        case 6: setst(rm, st(0) / st(rm)); fpu.pop(); return; case 7: setst(rm, st(rm) / st(0)); fpu.pop(); return;
        default: return; }
    case 0xDB: switch (regno) {
        case 5: compare(st(0), st(rm)); x87_flags(); return; case 6: compare(st(0), st(rm)); x87_flags(); return;
        case 4: if (rm == 2) fpu.sw &= ~uint16_t(0x7f); else if (rm == 3) fpu.init(); return;
        default: return; }
    case 0xDF: switch (regno) {
        case 4: if (rm == 0) { state->reg[0] = (state->reg[0] & 0xffff0000u) | fpu.status(); } return;
        case 5: compare(st(0), st(rm)); x87_flags(); fpu.pop(); return;
        case 6: compare(st(0), st(rm)); x87_flags(); fpu.pop(); return;
        default: return; }
    case 0xDD: switch (regno) {
        case 1: fpu.swap(0, rm); return; case 2: setst(rm, st(0)); return; case 3: setst(rm, st(0)); fpu.pop(); return;
        case 4: compare(st(0), st(rm)); return; case 5: compare(st(0), st(rm)); fpu.pop(); return;
        default: return; }
    case 0xD9:
        if ((modrm & 0x38) == 0) { fpu.push(st(rm)); return; }
        if ((modrm & 0x38) == 8) { fpu.swap(0, rm); return; }
        switch (modrm & 0x3f) {
        case 0x20: setst(0, -st(0)); return; case 0x21: setst(0, std::fabs(st(0))); return;
        case 0x28: fpu.push(Wide(1)); return; case 0x2e: fpu.push(Wide(0)); return;
        case 0x3a: setst(0, std::sqrt(st(0))); return; default: return;
        }
    default: return;
    }
}

struct InstructionMap {
    size_t host_offset = 0;
    uint32_t guest_eip = 0;
    bool memory_write = false;
    uint8_t memory_size = 0;
};

struct RuntimeBlock {
    std::unique_ptr<Executable> code;
    std::vector<uint8_t> guest_bytes;
    std::vector<std::pair<uint32_t, uint32_t>> page_generations;
    std::vector<std::pair<uint32_t, uint32_t>> memory_generations;
    std::vector<uint32_t> store_pages;
    std::vector<InstructionMap> instruction_map;
    uint32_t guest_length = 0;
    uint32_t guest_pc = 0;
    uint8_t exit_opcode = 0;
    bool may_fault = false;
    bool interpreter_boundary = false;
    bool uses_xmm = false;
    RuntimeBlock* linked_target = nullptr;
};

#if !defined(_WIN32)
struct HostFaultContext {
    sigjmp_buf jump;
    RuntimeState* state = nullptr;
    const RuntimeBlock* block = nullptr;
    uintptr_t code_address = 0;
    uintptr_t host_pc = 0;
    uintptr_t fault_address = 0;
    uintptr_t host_registers[8] = {};
    uintptr_t host_flags = 0;
};
inline thread_local HostFaultContext* active_host_fault = nullptr;
inline std::once_flag host_fault_signal_once;
inline void host_fault_signal(int signal_number, siginfo_t* info, void* raw_context) {
    auto* context = active_host_fault;
    auto* ucontext = static_cast<ucontext_t*>(raw_context);
    uintptr_t pc = uintptr_t(ucontext->uc_mcontext.gregs[REG_RIP]);
    if (context && pc >= context->code_address && pc < context->code_address + 4096) {
        context->host_pc = pc;
        context->fault_address = uintptr_t(info->si_addr);
        context->host_registers[0] = uintptr_t(ucontext->uc_mcontext.gregs[REG_RAX]);
        context->host_registers[1] = uintptr_t(ucontext->uc_mcontext.gregs[REG_RCX]);
        context->host_registers[2] = uintptr_t(ucontext->uc_mcontext.gregs[REG_RDX]);
        context->host_registers[3] = uintptr_t(ucontext->uc_mcontext.gregs[REG_RSI]);
        context->host_registers[4] = uintptr_t(ucontext->uc_mcontext.gregs[REG_RDI]);
        context->host_registers[5] = uintptr_t(ucontext->uc_mcontext.gregs[REG_R8]);
        context->host_registers[6] = uintptr_t(ucontext->uc_mcontext.gregs[REG_R9]);
        context->host_registers[7] = uintptr_t(ucontext->uc_mcontext.gregs[REG_R10]);
        context->host_flags = uintptr_t(ucontext->uc_mcontext.gregs[REG_EFL]);
        siglongjmp(context->jump, 1);
    }
    std::signal(signal_number, SIG_DFL);
    std::raise(signal_number);
}
inline void install_host_fault_signal() {
    std::call_once(host_fault_signal_once, [] {
        struct sigaction action = {};
        action.sa_sigaction = host_fault_signal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &action, nullptr);
    });
}
inline uint32_t recover_host_fault(HostFaultContext& context) {
    context.state->reg[0] = uint32_t(context.host_registers[0]);
    context.state->reg[1] = uint32_t(context.host_registers[1]);
    context.state->reg[2] = uint32_t(context.host_registers[2]);
    context.state->reg[3] = uint32_t(context.host_registers[3]);
    context.state->reg[4] = uint32_t(context.host_registers[4]);
    context.state->reg[5] = uint32_t(context.host_registers[5]);
    context.state->reg[6] = uint32_t(context.host_registers[6]);
    context.state->reg[7] = uint32_t(context.host_registers[7]);
    context.state->eflags = uint32_t(context.host_flags);
    size_t offset = context.host_pc - context.code_address;
    const InstructionMap* mapping = nullptr;
    for (const auto& candidate : context.block->instruction_map) {
        if (candidate.host_offset > offset) break;
        mapping = &candidate;
    }
    context.state->eip = mapping ? mapping->guest_eip : context.block->guest_pc;
    return context.state->eip;
}
#endif

#if defined(_WIN32)
struct HostFaultContext {
    RuntimeState* state = nullptr;
    const RuntimeBlock* block = nullptr;
    uintptr_t code_address = 0;
    uintptr_t host_pc = 0;
    uintptr_t fault_address = 0;
    uintptr_t host_registers[8] = {};
    uintptr_t host_flags = 0;
};
inline int host_fault_filter(EXCEPTION_POINTERS* exception, HostFaultContext& context) {
    if (!exception || exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        !context.state || !context.block) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t pc = uintptr_t(exception->ContextRecord->Rip);
    if (pc < context.code_address || pc >= context.code_address + 4096 ||
        exception->ExceptionRecord->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    context.host_pc = pc;
    context.fault_address = uintptr_t(exception->ExceptionRecord->ExceptionInformation[1]);
    context.host_registers[0] = uintptr_t(exception->ContextRecord->Rax);
    context.host_registers[1] = uintptr_t(exception->ContextRecord->Rcx);
    context.host_registers[2] = uintptr_t(exception->ContextRecord->Rdx);
    context.host_registers[3] = uintptr_t(exception->ContextRecord->Rsi);
    context.host_registers[4] = uintptr_t(exception->ContextRecord->Rdi);
    context.host_registers[5] = uintptr_t(exception->ContextRecord->R8);
    context.host_registers[6] = uintptr_t(exception->ContextRecord->R9);
    context.host_registers[7] = uintptr_t(exception->ContextRecord->R10);
    context.host_flags = uintptr_t(exception->ContextRecord->EFlags);
    return EXCEPTION_EXECUTE_HANDLER;
}
inline uint32_t recover_host_fault(HostFaultContext& context) {
    context.state->reg[0] = uint32_t(context.host_registers[0]);
    context.state->reg[1] = uint32_t(context.host_registers[1]);
    context.state->reg[2] = uint32_t(context.host_registers[2]);
    context.state->reg[3] = uint32_t(context.host_registers[3]);
    context.state->reg[4] = uint32_t(context.host_registers[4]);
    context.state->reg[5] = uint32_t(context.host_registers[5]);
    context.state->reg[6] = uint32_t(context.host_registers[6]);
    context.state->reg[7] = uint32_t(context.host_registers[7]);
    context.state->eflags = uint32_t(context.host_flags);
    size_t offset = context.host_pc - context.code_address;
    const InstructionMap* mapping = nullptr;
    for (const auto& candidate : context.block->instruction_map) {
        if (candidate.host_offset > offset) break;
        mapping = &candidate;
    }
    context.state->eip = mapping ? mapping->guest_eip : context.block->guest_pc;
    return context.state->eip;
}
#endif

inline uint8_t runtime_reg(unsigned guest) {
    static constexpr uint8_t map[8] = {0, 1, 2, 6, 7, 8, 9, 10};
    return map[guest];
}
inline void runtime_mov_reg_mem(Emitter& out, uint8_t dst, uint8_t base, uint8_t disp) {
    out.rex(dst, 0, base); out.b(0x8B); out.modrm(1, dst, base);
    if ((base & 7) == 4) out.b(0x24);
    out.b(disp);
}
inline void runtime_mov_reg_mem64(Emitter& out, uint8_t dst, uint8_t base, uint8_t disp) { out.rex(dst, 0, base, true); out.b(0x8B); out.modrm(1, dst, base); if ((base & 7) == 4) out.b(0x24); out.b(disp); }
inline void runtime_store_reg_mem(Emitter& out, uint8_t base, uint32_t disp, uint8_t src) {
    out.rex(src, 0, base); out.b(0x89); out.modrm(2, src, base);
    if ((base & 7) == 4) out.b(0x24);
    out.d(disp);
}
inline void runtime_load_xmm_mem(Emitter& out, uint8_t dst, uint8_t base, uint32_t disp) {
    out.b(0xF3); out.rex(0, 0, base); out.b(0x0F); out.b(0x6F); out.modrm(2, dst, base);
    if ((base & 7) == 4) out.b(0x24);
    out.d(disp);
}
inline void runtime_store_xmm_mem(Emitter& out, uint8_t base, uint32_t disp, uint8_t src) {
    out.b(0xF3); out.rex(0, 0, base); out.b(0x0F); out.b(0x7F); out.modrm(2, src, base);
    if ((base & 7) == 4) out.b(0x24);
    out.d(disp);
}
inline void runtime_sse(Emitter& out, uint8_t prefix, uint8_t op, uint8_t dst, uint8_t src, bool memory) {
    if (prefix) out.b(prefix);
    out.rex(0, 0, memory ? 11 : src);
    out.b(0x0F); out.b(op); out.modrm(memory ? 0 : 3, dst, memory ? 3 : src);
}
inline void runtime_mov_imm(Emitter& out, uint8_t dst, uint32_t value) {
    out.rex(0, 0, dst); out.b(uint8_t(0xB8 + (dst & 7))); out.d(value);
}
inline void runtime_mov_imm64(Emitter& out, uint8_t dst, uint64_t value) {
    out.rex(0, 0, dst, true); out.b(uint8_t(0xB8 + (dst & 7))); out.q(value);
}
inline void runtime_regop(Emitter& out, uint8_t op, uint8_t dst, uint8_t src) {
    out.rex(src, 0, dst); out.b(op); out.modrm(3, src, dst);
}
inline uint32_t runtime_u32(const std::vector<uint8_t>& b, size_t& p) {
    if (p + 4 > b.size()) throw std::runtime_error("truncated DBT instruction");
    uint32_t v = uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24;
    p += 4; return v;
}
inline uint8_t runtime_u8(const std::vector<uint8_t>& b, size_t& p) {
    if (p >= b.size()) throw std::runtime_error("truncated DBT instruction");
    return b[p++];
}
inline void runtime_emit_memory(Emitter& out, uint8_t op, uint8_t guest_reg, uint32_t ea) {
    uint8_t r = runtime_reg(guest_reg);
    runtime_mov_imm(out, 11, ea); // zero-extend the guest effective address
    out.rex(11, 11, 12, true); out.b(0x8D); out.modrm(0, 11, 4); out.b(0x1C); // r11 = r12 + r11, without changing flags
    out.rex(r, 0, 11); out.b(op); out.modrm(0, r, 3);
}
struct RuntimeAddress {
    uint8_t modrm = 0;
    uint8_t sib = 0;
    bool has_sib = false;
    bool absolute = false;
    uint32_t displacement = 0;
};
inline RuntimeAddress runtime_address(const std::vector<uint8_t>& b, size_t& p, uint8_t modrm) {
    RuntimeAddress a; a.modrm = modrm;
    unsigned mod = modrm >> 6, rm = modrm & 7;
    if (mod == 3) throw std::runtime_error("register operand has no address");
    if (rm == 4) {
        a.has_sib = true; a.sib = runtime_u8(b, p);
        if (mod == 0 && (a.sib & 7) == 5) a.absolute = true;
    } else if (mod == 0 && rm == 5) a.absolute = true;
    if (a.absolute) a.displacement = runtime_u32(b, p);
    else if (mod == 1) a.displacement = uint32_t(int32_t(int8_t(runtime_u8(b, p))));
    else if (mod == 2) a.displacement = runtime_u32(b, p);
    return a;
}
inline bool runtime_fixed_absolute(const RuntimeAddress& a) {
    return a.absolute && (!a.has_sib || ((a.sib >> 3) & 7) == 4);
}
inline void runtime_emit_ea(Emitter& out, const RuntimeAddress& a, bool host_address = true) {
    unsigned mod = a.modrm >> 6, rm = a.modrm & 7;
    uint8_t base = a.has_sib ? uint8_t(a.sib & 7) : uint8_t(rm);
    uint8_t index = a.has_sib ? uint8_t((a.sib >> 3) & 7) : 4;
    bool indexed_absolute = a.absolute && a.has_sib && index != 4;
    if (a.absolute && !indexed_absolute) runtime_mov_imm(out, 11, a.displacement);
    else {
        uint8_t host_base = a.absolute ? 5 : runtime_reg(base);
        uint8_t host_index = index == 4 ? 4 : runtime_reg(index);
        uint8_t scale = a.has_sib ? uint8_t(a.sib >> 6) : 0;
        out.rex(11, host_index, host_base); out.b(0x8D);
        if (a.has_sib) {
            out.modrm(uint8_t(a.absolute ? 0 : mod), 11, 4);
            out.b(uint8_t((scale << 6) | ((host_index & 7) << 3) | (host_base & 7)));
        } else out.modrm(uint8_t(mod), 11, host_base);
        if (a.absolute) out.d(a.displacement);
        else if (mod == 1) out.b(uint8_t(a.displacement));
        else if (mod == 2) out.d(a.displacement);
    }
    // 32-bit LEA above gives the guest's modulo-2^32 effective address.
    if (host_address) {
        out.rex(11, 11, 12, true); out.b(0x8D); out.modrm(0, 11, 4); out.b(0x1C);
    }
}
inline void runtime_emit_memory_ea(Emitter& out, uint8_t op, uint8_t guest_reg, const RuntimeAddress& a) {
    runtime_emit_ea(out, a);
    uint8_t r = runtime_reg(guest_reg);
    out.rex(r, 0, 11); out.b(op); out.modrm(0, r, 3);
}
inline size_t runtime_jcc(Emitter& out, uint8_t condition) { out.b(0x0f); out.b(condition); size_t at = out.bytes.size(); out.d(0); return at; }
inline size_t runtime_jmp(Emitter& out) { out.b(0xe9); size_t at = out.bytes.size(); out.d(0); return at; }
inline void runtime_mov_reg_reg(Emitter& out, uint8_t dst, uint8_t src) { out.rex(src, 0, dst, true); out.b(0x89); out.modrm(3, src, dst); }
inline void runtime_and_imm32(Emitter& out, uint8_t reg, uint32_t value) { out.rex(0, 0, reg); out.b(0x81); out.modrm(3, 4, reg); out.d(value); }
inline void runtime_cmp_imm32(Emitter& out, uint8_t reg, uint32_t value) { out.rex(0, 0, reg); out.b(0x81); out.modrm(3, 7, reg); out.d(value); }
inline void runtime_movzx_indexed(Emitter& out, uint8_t dst, uint8_t base, uint8_t index, uint8_t scale) { out.rex(dst, index, base); out.b(0x0f); out.b(0xb6); out.modrm(0, dst, 4); out.b(uint8_t((scale << 6) | ((index & 7) << 3) | (base & 7))); }
inline void runtime_test_imm8(Emitter& out, uint8_t reg, uint8_t value) { out.rex(0, 0, reg); out.b(0xf6); out.modrm(3, 0, reg); out.b(value); }
inline void runtime_or_indexed_imm8(Emitter& out, uint8_t base, uint8_t index, uint8_t value) { out.rex(0, index, base); out.b(0x80); out.modrm(0, 1, 4); out.b(uint8_t(((index & 7) << 3) | (base & 7))); out.b(value); }
inline void runtime_store_reg_mem32(Emitter& out, uint8_t base, uint32_t disp, uint8_t src) { out.rex(src, 0, base); out.b(0x89); out.modrm(2, src, base); if ((base & 7) == 4) out.b(0x24); out.d(disp); }
inline void runtime_inc_generation(Emitter& out, uint32_t* generation) {
    if (!generation) return;
    out.b(0x9c); runtime_mov_imm64(out, 14, reinterpret_cast<uintptr_t>(generation));
    out.rex(0, 13, 14); out.b(0xff); out.modrm(0, 0, 4); out.b(uint8_t((2u << 6) | ((13u & 7u) << 3) | (14u & 7u)));
    size_t nonzero = runtime_jcc(out, 0x85); out.rex(0, 13, 14); out.b(0xc7); out.modrm(0, 0, 4); out.b(uint8_t((2u << 6) | ((13u & 7u) << 3) | (14u & 7u))); out.d(1);
    out.patch32(nonzero, int32_t(out.bytes.size() - (nonzero + 4))); out.b(0x9d);
}
inline void runtime_inc_generation_raw(Emitter& out, uint32_t* generation) {
    runtime_mov_imm64(out, 14, reinterpret_cast<uintptr_t>(generation)); out.rex(0, 13, 14); out.b(0xff); out.modrm(0, 0, 4); out.b(uint8_t((2u << 6) | ((13u & 7u) << 3) | (14u & 7u)));
    size_t nonzero = runtime_jcc(out, 0x85); out.rex(0, 13, 14); out.b(0xc7); out.modrm(0, 0, 4); out.b(uint8_t((2u << 6) | ((13u & 7u) << 3) | (14u & 7u))); out.d(1);
    out.patch32(nonzero, int32_t(out.bytes.size() - (nonzero + 4)));
}
inline void runtime_emit_dynamic_store_tail(Emitter& out, const uint8_t* prot, uint32_t* generation,
                                            uint32_t guest_eip, uint8_t state_slot, std::vector<size_t>& exit_jumps) {
    if (!prot) return;
    out.b(0x9c); runtime_test_imm8(out, 14, runtime_prot_x); size_t not_code = runtime_jcc(out, 0x84);
    if (generation) runtime_inc_generation_raw(out, generation);
    out.b(0x9d); runtime_mov_imm(out, 11, guest_eip);
    runtime_mov_reg_mem64(out, 13, 4, state_slot); runtime_store_reg_mem32(out, 13, offsetof(RuntimeState, exit_eip), 11); exit_jumps.push_back(runtime_jmp(out));
    size_t continue_label = out.bytes.size(); out.b(0x9d); out.patch32(not_code, int32_t(continue_label - (not_code + 4)));
}
inline void runtime_emit_generation_touch_page(Emitter& out, const uint8_t* prot, uint32_t* generation, uint32_t page) {
    if (!generation || (prot && !(prot[page] & runtime_prot_x))) return;
    runtime_mov_imm(out, 13, page); runtime_inc_generation(out, generation);
}
inline void runtime_emit_memory_check(Emitter& out, const uint8_t* prot, uint8_t need, uint8_t size,
                                      uint32_t guest_eip, uint8_t state_slot, std::vector<size_t>& fallback_jumps) {
    if (!prot) return;
    out.b(0x9c); runtime_mov_reg_reg(out, 13, 11); runtime_regop(out, 0x29, 13, 12); runtime_mov_reg_reg(out, 14, 13);
    runtime_and_imm32(out, 14, runtime_page - 1); runtime_cmp_imm32(out, 14, runtime_page - size); size_t cross_page = runtime_jcc(out, 0x87);
    out.rex(0, 0, 13); out.b(0xc1); out.modrm(3, 5, 13); out.b(12); runtime_movzx_indexed(out, 14, 15, 13, 0);
    size_t permission_fail = 0, cow_valid = 0, cow_fail = 0;
    if (need == runtime_prot_w) {
        runtime_test_imm8(out, 14, runtime_prot_w); cow_valid = runtime_jcc(out, 0x85); runtime_test_imm8(out, 14, runtime_prot_cow); cow_fail = runtime_jcc(out, 0x84); runtime_or_indexed_imm8(out, 15, 13, runtime_prot_w);
    } else { runtime_test_imm8(out, 14, need); permission_fail = runtime_jcc(out, 0x84); }
    size_t skip_fallback = runtime_jmp(out), invalid = out.bytes.size(); out.b(0x9d); runtime_mov_imm(out, 11, guest_eip);
    runtime_mov_reg_mem64(out, 13, 4, state_slot); runtime_store_reg_mem32(out, 13, offsetof(RuntimeState, fallback_eip), 11);
    size_t to_epilogue = runtime_jmp(out); fallback_jumps.push_back(to_epilogue); size_t valid = out.bytes.size(); out.b(0x9d);
    out.patch32(cross_page, int32_t(invalid - (cross_page + 4))); if (permission_fail) out.patch32(permission_fail, int32_t(invalid - (permission_fail + 4)));
    if (cow_fail) out.patch32(cow_fail, int32_t(invalid - (cow_fail + 4)));
    out.patch32(skip_fallback, int32_t(valid - (skip_fallback + 4)));
    if (cow_valid) out.patch32(cow_valid, int32_t(valid - (cow_valid + 4)));
}
inline void runtime_emit_guest_stack_address(Emitter& out) {
    out.rex(11, runtime_reg(4), 12, true); out.b(0x8D); out.modrm(0, 11, 4); out.b(uint8_t(((runtime_reg(4) & 7) << 3) | (12 & 7)));
}
inline void runtime_emit_guest_stack_adjust(Emitter& out, bool push, uint32_t amount = 4) {
    uint8_t sp = runtime_reg(4); out.rex(sp, 0, sp); out.b(0x8D); out.modrm(2, sp, sp); out.d(push ? uint32_t(0u - amount) : amount);
}
inline void runtime_emit_guest_push_reg(Emitter& out, uint8_t guest_reg) {
    uint8_t r = runtime_reg(guest_reg); out.rex(13, 0, r); out.b(0x8B); out.modrm(3, 13, r);
    runtime_emit_guest_stack_adjust(out, true); runtime_emit_guest_stack_address(out);
    out.rex(13, 0, 11); out.b(0x89); out.modrm(0, 13, 3);
}
inline void runtime_emit_guest_pop_reg(Emitter& out, uint8_t guest_reg) {
    runtime_emit_guest_stack_address(out); uint8_t r = runtime_reg(guest_reg);
    if (guest_reg == 4) {
        out.rex(13, 0, 11); out.b(0x8B); out.modrm(0, 13, 3);
        runtime_emit_guest_stack_adjust(out, false);
        out.rex(r, 0, 13); out.b(0x8B); out.modrm(3, r, 13);
    } else {
        out.rex(r, 0, 11); out.b(0x8B); out.modrm(0, r, 3);
        runtime_emit_guest_stack_adjust(out, false);
    }
}

// Decode one encoding from the emitter's explicit subset. Memory accesses are
// intentionally restricted to absolute dwords; the dispatcher validates both
// R and W before allowing them into native code.
inline size_t runtime_instruction(const std::vector<uint8_t>& b, size_t start,
                                  uint32_t& memory_ea, bool& memory_operand, bool& absolute_memory,
                                  bool& memory_write, uint8_t& memory_size) {
    size_t p = start; uint8_t prefix = 0;
    if (p < b.size() && b[p] == 0x66) prefix = runtime_u8(b, p);
    uint8_t op = runtime_u8(b, p);
    memory_operand = false; absolute_memory = false; memory_write = false; memory_size = 0;
    if (prefix) {
        if (prefix != 0x66 || op != 0x0F) throw std::runtime_error("unsupported DBT prefix");
        uint8_t op2 = runtime_u8(b, p);
        if (op2 != 0x6E && op2 != 0x6F && op2 != 0x70 && op2 != 0x72 && op2 != 0x73 &&
            op2 != 0x76 && op2 != 0x7F && op2 != 0xDB && op2 != 0xEB && op2 != 0xF4 &&
            op2 != 0xFA && op2 != 0xFE && op2 != 0x62 && op2 != 0x58 && op2 != 0x59)
            throw std::runtime_error("unsupported DBT SSE opcode");
        uint8_t m = runtime_u8(b, p);
        if ((m >> 6) != 3) {
            RuntimeAddress a = runtime_address(b, p, m); memory_operand = true;
            if (runtime_fixed_absolute(a)) { memory_ea = a.displacement; absolute_memory = true; }
            memory_write = op2 == 0x7f; memory_size = 16;
        }
        if ((op2 == 0x70 || op2 == 0x72 || op2 == 0x73) && (m >> 6) != 3)
            throw std::runtime_error("unsupported DBT SSE memory form");
        if (op2 == 0x70 || op2 == 0x72 || op2 == 0x73) runtime_u8(b, p);
        return p - start;
    }
    if (op >= 0x50 && op <= 0x5F) return p - start;
    if (op == 0x68) { runtime_u32(b, p); return p - start; }
    if (op == 0x6A) { runtime_u8(b, p); return p - start; }
    if (op == 0xC2) { runtime_u8(b, p); runtime_u8(b, p); return p - start; }
    if (op == 0xC3 || op == 0xC9) return p - start;
    if (op >= 0xD8 && op <= 0xDF) {
        uint8_t m = runtime_u8(b, p); unsigned group = (m >> 3) & 7;
        bool valid = op == 0xD8 || op == 0xDC || op == 0xDE ||
            (op == 0xD9 && (group == 0 || group == 2 || group == 3 || group == 5 || group == 7)) ||
            (op == 0xDB && (group == 0 || group == 3)) ||
            (op == 0xDD && (group == 0 || group == 2 || group == 3 || group == 7)) ||
            (op == 0xDF && (group == 0 || group == 3));
        if ((m >> 6) != 3) {
            if (!valid) throw std::runtime_error("unsupported DBT x87 memory form");
            RuntimeAddress a = runtime_address(b, p, m); memory_operand = true;
            if (runtime_fixed_absolute(a)) { memory_ea = a.displacement; absolute_memory = true; }
            memory_write = (op == 0xd9 && (group == 2 || group == 3 || group == 7)) ||
                (op == 0xdb && group == 3) || (op == 0xdd && (group == 2 || group == 3 || group == 7)) ||
                (op == 0xdf && group == 3);
            memory_size = op == 0xd8 || op == 0xd9 || op == 0xdb ? 4 : op == 0xdc || op == 0xdd ? 8 : 2;
        } else {
            unsigned form = m & 0x3f;
            valid = op == 0xD8 || op == 0xDC || op == 0xDE ||
                (op == 0xD9 && (form <= 0x0f || form == 0x20 || form == 0x21 || form == 0x28 || form == 0x2e || form == 0x3a)) ||
                (op == 0xDB && (group == 4 || group == 5 || group == 6)) ||
                (op == 0xDD && group >= 1 && group <= 5) ||
                (op == 0xDF && (group == 4 || group == 5 || group == 6));
            if (!valid) throw std::runtime_error("unsupported DBT x87 form");
        }
        return p - start;
    }
    if (op == 0xE8) { runtime_u32(b, p); return p - start; }
    if (op == 0x8F) {
        uint8_t m = runtime_u8(b, p);
        if (((m >> 3) & 7) != 0) throw std::runtime_error("unsupported pop group");
        if ((m >> 6) != 3) { runtime_address(b, p, m); memory_operand = true; memory_write = true; memory_size = 4; }
        return p - start;
    }
    if (op >= 0xB8 && op <= 0xBF) { runtime_u32(b, p); return p - start; }
    if (op == 0x0F) {
        uint8_t op2 = runtime_u8(b, p);
        if (op2 == 0x28 || op2 == 0x29 || op2 == 0x58 || op2 == 0x59) {
            uint8_t m = runtime_u8(b, p);
            if ((m >> 6) != 3) {
                RuntimeAddress a = runtime_address(b, p, m); memory_operand = true;
                if (runtime_fixed_absolute(a)) { memory_ea = a.displacement; absolute_memory = true; }
                memory_write = op2 == 0x29;
                memory_size = 16;
            }
            return p - start;
        }
        if (op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBE || op2 == 0xBF) {
            uint8_t m = runtime_u8(b, p);
            if ((m >> 6) != 3) {
                RuntimeAddress a = runtime_address(b, p, m); memory_operand = true;
                if (runtime_fixed_absolute(a)) { memory_ea = a.displacement; absolute_memory = true; }
                memory_size = (op2 == 0xb6 || op2 == 0xbe) ? 1 : 2;
            }
            return p - start;
        }
        throw std::runtime_error("unsupported DBT 0F opcode");
    }
    if (op == 0x74 || op == 0x75) { runtime_u8(b, p); return p - start; }
    bool modrm_op = op == 0x8B || op == 0x89 || op == 0x8D ||
        op == 0x00 || op == 0x01 || op == 0x08 || op == 0x09 || op == 0x10 || op == 0x11 ||
        op == 0x18 || op == 0x19 || op == 0x20 || op == 0x21 || op == 0x28 || op == 0x29 ||
        op == 0x30 || op == 0x31 || op == 0x38 || op == 0x39 || op == 0x03 || op == 0x0B ||
        op == 0x13 || op == 0x1B || op == 0x23 || op == 0x2B || op == 0x33 || op == 0x3B ||
        op == 0x84 || op == 0x85 || op == 0x81 || op == 0x83 || op == 0xF6 || op == 0xF7 ||
        op == 0xFE || op == 0xFF;
    if (modrm_op) {
        uint8_t m = runtime_u8(b, p); unsigned group = (m >> 3) & 7;
        bool unary = op == 0xF6 || op == 0xF7 || op == 0xFE || op == 0xFF;
        if ((m >> 6) != 3) {
            RuntimeAddress a = runtime_address(b, p, m); memory_operand = op != 0x8d;
            if (runtime_fixed_absolute(a)) { memory_ea = a.displacement; absolute_memory = true; }
            memory_size = ((op <= 0x3b && !(op & 1)) || op == 0x84 || op == 0xf6 || op == 0xfe) ? 1 : 4;
            memory_write = op == 0x89 ||
                ((op <= 0x3b) && ((op & 7) == 1) && op != 0x39) ||
                ((op == 0x81 || op == 0x83) && group != 7) ||
                ((op == 0xf6 || op == 0xf7) && (group == 2 || group == 3)) ||
                ((op == 0xfe || op == 0xff) && group < 2);
        }
        if (op == 0x81 || (op == 0xF7 && group == 0)) runtime_u32(b, p);
        else if (op == 0x83 || (op == 0xF6 && group == 0)) runtime_u8(b, p);
        if (unary && group > 3 && !(op == 0xFF && (group == 4 || group == 6))) throw std::runtime_error("unsupported unary DBT group");
        if (op == 0xFE && group > 1) throw std::runtime_error("unsupported byte unary DBT group");
        if ((op == 0xF6 || op == 0xFE) && (m >> 6) == 3 && (m & 7) >= 4)
            throw std::runtime_error("high-byte register needs the interpreter");
        return p - start;
    }
    if (op == 0x6B) {
        if ((runtime_u8(b, p) >> 6) != 3) throw std::runtime_error("unsupported imul form");
        runtime_u8(b, p); return p - start;
    }
    if (op == 0xC1 || op == 0xD1 || op == 0xD3) {
        uint8_t m = runtime_u8(b, p);
        if ((m >> 6) != 3) throw std::runtime_error("unsupported shift form");
        if (op == 0xC1) runtime_u8(b, p);
        return p - start;
    }
    throw std::runtime_error("unsupported DBT opcode");
}

inline bool runtime_uses_xmm(const std::vector<uint8_t>& guest) {
    size_t p = 0;
    while (p < guest.size()) {
        size_t start = p; bool prefixed = guest[p] == 0x66;
        if (prefixed) ++p;
        if (p + 1 < guest.size() && guest[p] == 0x0F) {
            uint8_t op2 = guest[p + 1];
            if (prefixed || op2 == 0x28 || op2 == 0x29 || op2 == 0x58 || op2 == 0x59)
                return true;
        }
        uint32_t memory_ea = 0; bool memory_operand = false, absolute_memory = false, memory_write = false; uint8_t memory_size = 0;
        p = start + runtime_instruction(guest, start, memory_ea, memory_operand, absolute_memory, memory_write, memory_size);
    }
    return false;
}

inline std::vector<uint8_t> runtime_translate(const std::vector<uint8_t>& guest, uint32_t guest_pc,
                                               std::vector<InstructionMap>* instruction_map = nullptr,
                                               const uint8_t* prot = nullptr, uint32_t* generation = nullptr) {
    Emitter out;
    bool uses_xmm = runtime_uses_xmm(guest);
#if defined(_WIN32)
    out.b(0x48); out.b(0x89); out.b(0xCF); // Windows x64 passes State* in RCX; use RDI internally.
#endif
    out.b(0x57); out.b(0x41); out.b(0x54); out.b(0x41); out.b(0x55); out.b(0x41); out.b(0x56);
    if (prot) out.b(0x41), out.b(0x57); // save state*, preserve R12-R15 when metadata checks are enabled
    out.b(0x48); out.b(0x83); out.b(0xEC); out.b(uses_xmm ? 40 : 8);
    if (prot) runtime_mov_imm64(out, 15, reinterpret_cast<uintptr_t>(prot));
    if (uses_xmm) {
        runtime_store_xmm_mem(out, 4, 0, 6); runtime_store_xmm_mem(out, 4, 16, 7);
    }
    runtime_mov_reg_mem(out, 11, 7, 36);
    out.b(0x41); out.b(0x53); out.b(0x9D);
    out.rex(12, 0, 7, true); out.b(0x8B); out.modrm(1, 12, 7); out.b(40);
    if (uses_xmm)
        for (unsigned i = 0; i != 8; ++i) runtime_load_xmm_mem(out, uint8_t(i), 7, 48 + i * 16);
    for (unsigned i : {0u, 1u, 2u, 3u, 5u, 6u, 7u, 4u}) runtime_mov_reg_mem(out, runtime_reg(i), 7, uint8_t(i * 4));
    size_t p = 0;
    const uint8_t state_slot = uint8_t((uses_xmm ? 64 : 32) + (prot ? 8 : 0));
    std::vector<size_t> fallback_jumps, exit_jumps;
    bool branch_terminated = false;
    while (p < guest.size()) {
        size_t begin = p; uint8_t prefix = 0;
        const size_t map_index = instruction_map ? instruction_map->size() : 0;
        if (instruction_map) instruction_map->push_back({out.bytes.size(), guest_pc + uint32_t(begin), false, 0});
        if (guest[p] == 0x66) prefix = runtime_u8(guest, p);
        uint8_t op = runtime_u8(guest, p);
        auto mark_memory = [&](bool write, uint8_t size) {
            if (instruction_map) {
                (*instruction_map)[map_index].memory_write = write;
                (*instruction_map)[map_index].memory_size = size;
            }
        };
        auto check_memory = [&](bool dynamic, bool write, uint8_t size) {
            if (dynamic) runtime_emit_memory_check(out, prot, write ? runtime_prot_w : runtime_prot_r, size,
                                                    guest_pc + uint32_t(begin), state_slot, fallback_jumps);
        };
        auto touch_static = [&](const RuntimeAddress& a, uint8_t size) {
            if (!generation || !runtime_fixed_absolute(a)) return;
            uint32_t first = a.displacement >> 12;
            uint32_t last = (a.displacement + size - 1) >> 12;
            for (uint32_t page = first;; ++page) {
                runtime_emit_generation_touch_page(out, prot, generation, page);
                if (page == last) break;
            }
        };
        auto exit_dynamic_store = [&](bool dynamic, bool write) {
            if (dynamic && write) runtime_emit_dynamic_store_tail(out, prot, generation,
                                                                   guest_pc + uint32_t(p), state_slot, exit_jumps);
        };
        if (prefix) {
            if (prefix != 0x66 || op != 0x0F) throw std::runtime_error("unsupported DBT prefix");
            uint8_t op2 = runtime_u8(guest, p), m = runtime_u8(guest, p);
            bool memory = (m >> 6) != 3; RuntimeAddress a;
            if (memory) {
                a = runtime_address(guest, p, m); runtime_emit_ea(out, a);
                check_memory(!runtime_fixed_absolute(a), op2 == 0x7f, 16); mark_memory(op2 == 0x7f, 16);
            }
            uint8_t dst = uint8_t((m >> 3) & 7), src = uint8_t(m & 7);
            if (op2 == 0x6E) runtime_sse(out, 0x66, op2, dst, runtime_reg(src), memory);
            else {
                if ((op2 == 0x70 || op2 == 0x72 || op2 == 0x73) && memory)
                    throw std::runtime_error("unsupported DBT SSE memory form");
                runtime_sse(out, 0x66, op2, dst, src, memory);
            }
            if (op2 == 0x70 || op2 == 0x72 || op2 == 0x73) out.b(runtime_u8(guest, p));
            if (memory && op2 == 0x7f) {
                if (runtime_fixed_absolute(a)) touch_static(a, 16);
                exit_dynamic_store(!runtime_fixed_absolute(a), true);
            }
        } else if (op >= 0xD8 && op <= 0xDF) {
            uint32_t memory_ea = 0; bool memory_operand = false, absolute_memory = false, memory_write = false; uint8_t memory_size = 0;
            const size_t len = runtime_instruction(guest, begin, memory_ea, memory_operand, absolute_memory, memory_write, memory_size);
            p = begin + len;
            RuntimeAddress a;
            if (memory_operand) {
                size_t address_p = begin + 2; a = runtime_address(guest, address_p, guest[begin + 1]);
                runtime_emit_ea(out, a); check_memory(!absolute_memory, memory_write, memory_size); mark_memory(memory_write, memory_size);
            }
            out.rex(11, 0, 4, true); out.b(0x8B);
            if (state_slot < 128) { out.modrm(1, 11, 4); out.b(0x24); out.b(state_slot); }
            else { out.modrm(2, 11, 4); out.b(0x24); out.d(state_slot); }
            for (unsigned i = 0; i != 8; ++i) runtime_store_reg_mem(out, 11, i * 4, runtime_reg(i));
            out.b(0x9C); out.b(0x58); out.b(0x25); out.d(0x00000CD7); runtime_store_reg_mem(out, 11, 36, 0);
            out.rex(11, 0, 7, true); out.b(0x89); out.modrm(3, 11, 7);
            uint64_t encoded = 0; for (size_t i = 0; i != len; ++i) encoded |= uint64_t(guest[begin + i]) << (i * 8);
            runtime_mov_imm64(out, 0, reinterpret_cast<uintptr_t>(&runtime_x87));
#if defined(_WIN32)
            out.b(0x48); out.b(0x89); out.b(0xF9); // RCX = State*
            runtime_mov_imm64(out, 2, encoded); runtime_mov_imm(out, 8, uint32_t(len));
#else
            runtime_mov_imm64(out, 6, encoded); runtime_mov_imm(out, 2, uint32_t(len));
#endif
            out.b(0xFF); out.b(0xD0); // call RAX
            if (memory_operand && memory_write) {
                if (absolute_memory) touch_static(a, memory_size);
                exit_dynamic_store(!absolute_memory, true);
            }
            out.rex(11, 0, 4, true); out.b(0x8B);
            if (state_slot < 128) { out.modrm(1, 11, 4); out.b(0x24); out.b(state_slot); }
            else { out.modrm(2, 11, 4); out.b(0x24); out.d(state_slot); }
            for (unsigned i : {0u, 1u, 2u, 3u, 5u, 6u, 7u, 4u}) runtime_mov_reg_mem(out, runtime_reg(i), 11, uint8_t(i * 4));
            runtime_mov_reg_mem(out, 11, 11, 36); out.b(0x41); out.b(0x53); out.b(0x9D);
        } else if (op >= 0x50 && op <= 0x57) {
            runtime_emit_guest_push_reg(out, op & 7);
        } else if (op >= 0x58 && op <= 0x5F) {
            runtime_emit_guest_pop_reg(out, op & 7);
        } else if (op == 0x68 || op == 0x6A) {
            uint32_t value = op == 0x68 ? runtime_u32(guest, p) : uint32_t(int32_t(int8_t(runtime_u8(guest, p))));
            runtime_mov_imm(out, 13, value); runtime_emit_guest_stack_adjust(out, true); runtime_emit_guest_stack_address(out);
            out.rex(13, 0, 11); out.b(0x89); out.modrm(0, 13, 3);
        } else if (op == 0x8F) {
            uint8_t m = runtime_u8(guest, p); RuntimeAddress a;
            if ((m >> 3) & 7) throw std::runtime_error("unsupported pop group");
            if ((m >> 6) != 3) { a = runtime_address(guest, p, m); runtime_emit_guest_stack_address(out); out.rex(14, 0, 11); out.b(0x8B); out.modrm(0, 14, 3); runtime_emit_guest_stack_adjust(out, false); if (!runtime_fixed_absolute(a)) throw std::runtime_error("unsupported translated pop memory form"); out.rex(14, 0, 12); out.b(0x89); out.modrm(2, 14, 4); out.b(0x24); out.d(a.displacement); }
            else { runtime_emit_guest_pop_reg(out, m & 7); }
        } else if (op == 0xC2 || op == 0xC3) {
            uint32_t cleanup = op == 0xC2 ? runtime_u8(guest, p) | (uint32_t(runtime_u8(guest, p)) << 8) : 0;
            runtime_emit_guest_stack_address(out); out.rex(11, 0, 11); out.b(0x8B); out.modrm(0, 11, 3); runtime_emit_guest_stack_adjust(out, false, 4 + cleanup);
            branch_terminated = true; break;
        } else if (op == 0xC9) {
            out.rex(runtime_reg(5), 0, runtime_reg(4)); out.b(0x89); out.modrm(3, runtime_reg(5), runtime_reg(4));
            runtime_emit_guest_pop_reg(out, 5);
        } else if (op == 0xE8) {
            uint32_t return_eip = guest_pc + uint32_t(p + 4); int32_t rel = int32_t(runtime_u32(guest, p));
            runtime_mov_imm(out, 13, return_eip); runtime_emit_guest_stack_adjust(out, true); runtime_emit_guest_stack_address(out);
            out.rex(13, 0, 11); out.b(0x89); out.modrm(0, 13, 3); runtime_mov_imm(out, 11, return_eip + uint32_t(rel));
            branch_terminated = true; break;
        } else if (op >= 0xB8 && op <= 0xBF) runtime_mov_imm(out, runtime_reg(op - 0xB8), runtime_u32(guest, p));
        else if (op == 0x0F) {
            uint8_t op2 = runtime_u8(guest, p), m = runtime_u8(guest, p);
            if (op2 == 0x28 || op2 == 0x29 || op2 == 0x58 || op2 == 0x59) {
                RuntimeAddress a; bool memory = (m >> 6) != 3;
                if (memory) {
                    a = runtime_address(guest, p, m); runtime_emit_ea(out, a);
                    check_memory(!runtime_fixed_absolute(a), op2 == 0x29, 16); mark_memory(op2 == 0x29, 16);
                }
                runtime_sse(out, 0, op2, uint8_t((m >> 3) & 7), uint8_t(m & 7), memory);
                if (memory && op2 == 0x29) {
                    if (runtime_fixed_absolute(a)) touch_static(a, 16);
                    exit_dynamic_store(!runtime_fixed_absolute(a), true);
                }
            } else {
                RuntimeAddress a; bool memory = (m >> 6) != 3;
                uint8_t dst = runtime_reg((m >> 3) & 7), src = memory ? 11 : runtime_reg(m & 7);
                bool high_byte = !memory && (op2 == 0xB6 || op2 == 0xBE) && (m & 7) >= 4;
                if (memory) {
                    a = runtime_address(guest, p, m); runtime_emit_ea(out, a);
                    check_memory(!runtime_fixed_absolute(a), false, (op2 == 0xb6 || op2 == 0xbe) ? 1 : 2);
                    mark_memory(false, (op2 == 0xb6 || op2 == 0xbe) ? 1 : 2);
                }
                if (high_byte) {
                    uint8_t source = runtime_reg((m & 7) - 4);
                    out.b(0x9C); out.rex(11, 0, source); out.b(0x8B); out.modrm(3, 11, source);
                    out.b(0x41); out.b(0xC1); out.b(0xEB); out.b(8); out.b(0x9D);
                    src = 11;
                }
                if (!memory && !high_byte && (op2 == 0xB6 || op2 == 0xBE) && src >= 4 && src < 8 && dst < 8) out.b(0x40);
                out.rex(dst, 0, src); out.b(0x0F); out.b(op2); out.modrm(memory ? 0 : 3, dst, memory || high_byte ? 3 : src);
            }
        } else if (op == 0x8B || op == 0x89 || op == 0x8D ||
                   (op <= 0x3B && (op & 7) == 1) ||
                   (op <= 0x3B && (op & 7) == 3) || op == 0x84 || op == 0x85) {
            uint8_t m = runtime_u8(guest, p); bool memory = (m >> 6) != 3;
            RuntimeAddress a; if (memory) a = runtime_address(guest, p, m);
            uint8_t regfield = uint8_t((m >> 3) & 7), rmfield = uint8_t(m & 7);
            if (op == 0x8D) {
                if (!memory) throw std::runtime_error("lea register operand");
                runtime_emit_ea(out, a, false);
                uint8_t dst = runtime_reg(regfield); out.rex(dst, 0, 11); out.b(0x8B); out.modrm(3, dst, 3);
            } else if (op == 0x8B || op == 0x89) {
                uint8_t reg = runtime_reg(regfield), rm = memory ? 11 : runtime_reg(rmfield);
                if (memory) {
                    runtime_emit_ea(out, a); check_memory(!runtime_fixed_absolute(a), op == 0x89, 4); mark_memory(op == 0x89, 4);
                }
                out.rex(reg, 0, rm);
                out.b(op); out.modrm(memory ? 0 : 3, reg, rm);
                if (memory && op == 0x89) {
                    if (runtime_fixed_absolute(a)) touch_static(a, 4);
                    exit_dynamic_store(!runtime_fixed_absolute(a), true);
                }
            } else if (op == 0x84 || op == 0x85) {
                if (op == 0x84) throw std::runtime_error("byte test unsupported");
                uint8_t rm = memory ? 11 : runtime_reg(rmfield), reg = runtime_reg(regfield);
                if (memory) {
                    runtime_emit_ea(out, a); check_memory(!runtime_fixed_absolute(a), false, op == 0x84 ? 1 : 4); mark_memory(false, op == 0x84 ? 1 : 4);
                }
                out.rex(reg, 0, rm); out.b(op); out.modrm(memory ? 0 : 3, reg, rm);
                out.b(0x9C); out.b(0x41); out.b(0x5B); out.b(0x41); out.b(0x81); out.b(0xE3); out.d(~uint32_t(0x10)); out.b(0x41); out.b(0x53); out.b(0x9D);
            } else {
                uint8_t base = uint8_t(op & 0xFC);
                uint8_t reg = runtime_reg(regfield), rm = memory ? 11 : runtime_reg(rmfield);
                if (memory) {
                    runtime_emit_ea(out, a); check_memory(!runtime_fixed_absolute(a), (op & 7) == 1, 4); mark_memory((op & 7) == 1, 4);
                }
                uint8_t native_op = op;
                out.rex(reg, 0, rm);
                out.b(native_op); out.modrm(memory ? 0 : 3, reg, rm);
                if (memory && (op & 7) == 1) {
                    if (runtime_fixed_absolute(a)) touch_static(a, 4);
                    exit_dynamic_store(!runtime_fixed_absolute(a), true);
                }
                if (base == 0x08 || base == 0x20 || base == 0x30) {
                    out.b(0x9C); out.b(0x41); out.b(0x5B); out.b(0x41); out.b(0x81); out.b(0xE3); out.d(~uint32_t(0x10)); out.b(0x41); out.b(0x53); out.b(0x9D);
                }
            }
        } else if (op == 0x81 || op == 0x83 || op == 0xF6 || op == 0xF7 || op == 0xFE || op == 0xFF) {
            uint8_t m = runtime_u8(guest, p), group = uint8_t((m >> 3) & 7); bool memory = (m >> 6) != 3;
            RuntimeAddress a; if (memory) a = runtime_address(guest, p, m);
            if (op == 0xFF && (group == 2 || group == 4 || group == 6)) {
                if (memory && !runtime_fixed_absolute(a)) throw std::runtime_error("dynamic indirect control transfer needs the interpreter");
                if (memory) { runtime_emit_ea(out, a); out.rex(13, 0, 11); out.b(0x8B); out.modrm(0, 13, 3); }
                else { uint8_t src = runtime_reg(m & 7); out.rex(13, 0, src); out.b(0x8B); out.modrm(3, 13, src); }
                if (group == 6) { runtime_emit_guest_stack_adjust(out, true); runtime_emit_guest_stack_address(out); out.rex(13, 0, 11); out.b(0x89); out.modrm(0, 13, 3); }
                else if (group == 4) { out.rex(11, 0, 13); out.b(0x8B); out.modrm(3, 11, 13); branch_terminated = true; break; }
                else { uint32_t return_eip = guest_pc + uint32_t(p); runtime_emit_guest_stack_adjust(out, true); runtime_emit_guest_stack_address(out); out.rex(0, 0, 11); out.b(0xC7); out.modrm(0, 0, 3); out.d(return_eip); out.rex(11, 0, 13); out.b(0x8B); out.modrm(3, 11, 13); branch_terminated = true; break; }
            } else {
            uint8_t rm = memory ? 11 : runtime_reg(m & 7);
            bool memory_write = (op == 0x81 || op == 0x83) ? group != 7 :
                (op == 0xf6 || op == 0xf7) ? (group == 2 || group == 3) :
                (op == 0xfe || op == 0xff) && group < 2;
            if (memory) {
                runtime_emit_ea(out, a); check_memory(!runtime_fixed_absolute(a), memory_write, (op == 0xf6 || op == 0xfe) ? 1 : 4);
            }
            out.rex(0, 0, rm); out.b(op); out.modrm(memory ? 0 : 3, group, rm);
            if (op == 0x81 || (op == 0xF7 && group == 0)) out.d(runtime_u32(guest, p));
            else if (op == 0x83 || (op == 0xF6 && group == 0)) out.b(runtime_u8(guest, p));
            if (memory && memory_write) {
                if (runtime_fixed_absolute(a)) touch_static(a, (op == 0xf6 || op == 0xfe) ? 1 : 4);
                exit_dynamic_store(!runtime_fixed_absolute(a), true);
            }
            }
            if (op == 0x81 || op == 0x83) {
                if (group == 1 || group == 4 || group == 6) {
                    out.b(0x9C); out.b(0x41); out.b(0x5B); out.b(0x41); out.b(0x81); out.b(0xE3); out.d(~uint32_t(0x10)); out.b(0x41); out.b(0x53); out.b(0x9D);
                }
            }
        } else if (op == 0x6B) {
            uint8_t m = runtime_u8(guest, p), dst = runtime_reg((m >> 3) & 7), src = runtime_reg(m & 7);
            out.rex(dst, 0, src); out.b(op); out.modrm(3, dst, src); out.b(runtime_u8(guest, p));
        } else if (op == 0xC1 || op == 0xD1 || op == 0xD3) {
            uint8_t m = runtime_u8(guest, p), group = (m >> 3) & 7, r = runtime_reg(m & 7);
            out.rex(0, 0, r); out.b(op); out.modrm(3, group, r);
            if (op == 0xC1) out.b(runtime_u8(guest, p));
        } else if (op == 0x74 || op == 0x75) {
            int8_t rel = int8_t(runtime_u8(guest, p));
            uint32_t target = guest_pc + uint32_t(p) + uint32_t(int32_t(rel));
            uint32_t fallthrough = guest_pc + uint32_t(p);
            out.b(0x0F); out.b(op == 0x74 ? 0x84 : 0x85); size_t branch = out.bytes.size(); out.d(0);
            runtime_mov_imm(out, 11, fallthrough); out.b(0xE9); size_t skip = out.bytes.size(); out.d(0);
            size_t taken = out.bytes.size(); runtime_mov_imm(out, 11, target); size_t end = out.bytes.size();
            out.patch32(branch, int32_t(taken - (branch + 4))); out.patch32(skip, int32_t(end - (skip + 4)));
            branch_terminated = true;
            break;
        } else throw std::runtime_error("unsupported DBT opcode");
        if (p <= begin) throw std::runtime_error("DBT decoder made no progress");
    }
    if (!branch_terminated) runtime_mov_imm(out, 11, guest_pc + uint32_t(p));
    const size_t epilogue = out.bytes.size();
    for (size_t jump : fallback_jumps)
        out.patch32(jump, int32_t(epilogue - (jump + 4)));
    for (size_t jump : exit_jumps)
        out.patch32(jump, int32_t(epilogue - (jump + 4)));
    uint8_t eip_slot = uses_xmm ? 32 : 0;
    out.b(0x44); out.b(0x89); out.b(0x5C); out.b(0x24); out.b(eip_slot);
    out.b(0x4C); out.b(0x8B); out.b(0x5C); out.b(0x24); out.b(state_slot);
    for (unsigned i = 0; i != 8; ++i) runtime_store_reg_mem(out, 11, i * 4, runtime_reg(i));
    if (uses_xmm)
        for (unsigned i = 0; i != 8; ++i) runtime_store_xmm_mem(out, 11, 48 + i * 16, uint8_t(i));
    out.b(0x9C); out.b(0x58); out.b(0x25); out.d(0x00000CD7); runtime_store_reg_mem(out, 11, 36, 0);
    out.b(0xFC); out.b(0x8B); out.b(0x44); out.b(0x24); out.b(eip_slot); runtime_store_reg_mem(out, 11, 32, 0);
    if (uses_xmm) {
        runtime_load_xmm_mem(out, 6, 4, 0); runtime_load_xmm_mem(out, 7, 4, 16);
    }
    out.b(0x48); out.b(0x83); out.b(0xC4); out.b(uses_xmm ? 40 : 8);
    if (prot) { out.b(0x41); out.b(0x5F); }
    out.b(0x41); out.b(0x5E); out.b(0x41); out.b(0x5D); out.b(0x41); out.b(0x5C); out.b(0x5F);
    out.b(0x8B); out.b(0x47); out.b(32); out.b(0xC3);
    return out.bytes;
}
} // namespace x86::dbt
