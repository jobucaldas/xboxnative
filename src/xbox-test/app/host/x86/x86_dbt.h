// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace x86::dbt {
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
        address = VirtualAllocFromApp(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!address) throw std::runtime_error("VirtualAllocFromApp RW failed");
        std::memcpy(address, code.data(), code.size());
        DWORD old_protection = 0;
        if (!VirtualProtectFromApp(address, size, PAGE_EXECUTE_READ, &old_protection)) {
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
};
static_assert(offsetof(RuntimeState, eip) == 32, "DBT state layout");
static_assert(offsetof(RuntimeState, eflags) == 36, "DBT state layout");
static_assert(offsetof(RuntimeState, memory) == 40, "DBT state layout");

struct RuntimeBlock {
    std::unique_ptr<Executable> code;
    std::vector<uint8_t> guest_bytes;
    uint32_t guest_length = 0;
    uint32_t guest_pc = 0;
};

inline uint8_t runtime_reg(unsigned guest) {
    static constexpr uint8_t map[8] = {0, 1, 2, 6, 7, 8, 9, 10};
    return map[guest];
}
inline void runtime_mov_reg_mem(Emitter& out, uint8_t dst, uint8_t base, uint8_t disp) {
    out.rex(dst, 0, base); out.b(0x8B); out.modrm(1, dst, base);
    if ((base & 7) == 4) out.b(0x24);
    out.b(disp);
}
inline void runtime_store_reg_mem(Emitter& out, uint8_t base, uint32_t disp, uint8_t src) {
    out.rex(src, 0, base); out.b(0x89); out.modrm(2, src, base);
    if ((base & 7) == 4) out.b(0x24);
    out.d(disp);
}
inline void runtime_mov_imm(Emitter& out, uint8_t dst, uint32_t value) {
    out.rex(0, 0, dst); out.b(uint8_t(0xB8 + (dst & 7))); out.d(value);
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
    out.rex(11, 11, 12, true); out.b(0x8D); out.modrm(0, 11, 4); out.b(0x5C); // r11 = r12 + r11, without changing flags
    out.rex(r, 0, 11); out.b(op); out.modrm(0, r, 3);
}

// Decode one encoding from the emitter's explicit subset. Memory accesses are
// intentionally restricted to absolute dwords; the dispatcher validates both
// R and W before allowing them into native code.
inline size_t runtime_instruction(const std::vector<uint8_t>& b, size_t start,
                                  uint32_t& memory_ea, bool& memory_operand) {
    size_t p = start; uint8_t op = runtime_u8(b, p);
    memory_operand = false;
    if (op >= 0xB8 && op <= 0xBF) { runtime_u32(b, p); return p - start; }
    if (op == 0x8B || op == 0x01 || op == 0x29 || op == 0x21 || op == 0x09 || op == 0x31 || op == 0x39) {
        if ((runtime_u8(b, p) >> 6) != 3) throw std::runtime_error("unsupported register form");
        return p - start;
    }
    if (op == 0x03 || op == 0x89) {
        uint8_t m = runtime_u8(b, p);
        if ((m >> 6) != 0 || (m & 7) != 5) throw std::runtime_error("unsupported memory form");
        memory_ea = runtime_u32(b, p); memory_operand = true; return p - start;
    }
    if (op == 0x8D) {
        uint8_t m = runtime_u8(b, p);
        if ((m >> 6) != 1 || (m & 7) != 4) throw std::runtime_error("unsupported lea form");
        uint8_t sib = runtime_u8(b, p);
        if (((sib >> 3) & 7) == 4 || (sib & 7) == 5) throw std::runtime_error("unsupported lea SIB");
        runtime_u8(b, p); return p - start;
    }
    if (op == 0x81 || op == 0x83) {
        uint8_t m = runtime_u8(b, p); unsigned group = (m >> 3) & 7;
        if ((m >> 6) != 3 || (group != 0 && group != 1 && group != 4 && group != 5 && group != 6 && group != 7))
            throw std::runtime_error("unsupported immediate ALU form");
        if (op == 0x81) runtime_u32(b, p); else runtime_u8(b, p);
        return p - start;
    }
    if (op == 0x6B) {
        if ((runtime_u8(b, p) >> 6) != 3) throw std::runtime_error("unsupported imul form");
        runtime_u8(b, p); return p - start;
    }
    if (op == 0xC1) {
        uint8_t m = runtime_u8(b, p); unsigned group = (m >> 3) & 7;
        if ((m >> 6) != 3 || (group != 4 && group != 5 && group != 7)) throw std::runtime_error("unsupported shift form");
        runtime_u8(b, p); return p - start;
    }
    if (op == 0x74 || op == 0x75) { runtime_u8(b, p); return p - start; }
    throw std::runtime_error("unsupported DBT opcode");
}

inline std::vector<uint8_t> runtime_translate(const std::vector<uint8_t>& guest, uint32_t guest_pc) {
    Emitter out;
    out.b(0x57); out.b(0x41); out.b(0x54); // save state* and preserve R12
    out.b(0x48); out.b(0x83); out.b(0xEC); out.b(8);
    runtime_mov_reg_mem(out, 11, 7, 36);
    out.b(0x41); out.b(0x53); out.b(0x9D);
    out.rex(12, 0, 7, true); out.b(0x8B); out.modrm(1, 12, 7); out.b(40);
    for (unsigned i : {0u, 1u, 2u, 3u, 5u, 6u, 7u, 4u}) runtime_mov_reg_mem(out, runtime_reg(i), 7, uint8_t(i * 4));
    size_t p = 0;
    while (p < guest.size()) {
        size_t begin = p; uint8_t op = runtime_u8(guest, p);
        if (op >= 0xB8 && op <= 0xBF) runtime_mov_imm(out, runtime_reg(op - 0xB8), runtime_u32(guest, p));
        else if (op == 0x8B) {
            uint8_t m = runtime_u8(guest, p), dst = runtime_reg((m >> 3) & 7), src = runtime_reg(m & 7);
            out.rex(dst, 0, src); out.b(op); out.modrm(3, dst, src);
        } else if (op == 0x01 || op == 0x29 || op == 0x21 || op == 0x09 || op == 0x31 || op == 0x39) {
            uint8_t m = runtime_u8(guest, p); runtime_regop(out, op, runtime_reg(m & 7), runtime_reg((m >> 3) & 7));
        } else if (op == 0x03 || op == 0x89) {
            uint8_t m = runtime_u8(guest, p); uint32_t ea = runtime_u32(guest, p);
            runtime_emit_memory(out, op, (m >> 3) & 7, ea);
        } else if (op == 0x8D) {
            uint8_t m = runtime_u8(guest, p), sib = runtime_u8(guest, p); int8_t disp = int8_t(runtime_u8(guest, p));
            uint8_t dst = runtime_reg((m >> 3) & 7), idx = runtime_reg((sib >> 3) & 7), base = runtime_reg(sib & 7);
            out.rex(dst, idx, base); out.b(0x8D); out.modrm(1, dst, 4);
            out.b(uint8_t(((sib >> 6) << 6) | ((idx & 7) << 3) | (base & 7))); out.b(uint8_t(disp));
        } else if (op == 0x81 || op == 0x83) {
            uint8_t m = runtime_u8(guest, p); uint8_t group = (m >> 3) & 7, r = runtime_reg(m & 7);
            out.rex(0, 0, r); out.b(op); out.modrm(3, group, r);
            if (op == 0x81) out.d(runtime_u32(guest, p)); else out.b(runtime_u8(guest, p));
        } else if (op == 0x6B) {
            uint8_t m = runtime_u8(guest, p), dst = runtime_reg((m >> 3) & 7), src = runtime_reg(m & 7);
            out.rex(dst, 0, src); out.b(op); out.modrm(3, dst, src); out.b(runtime_u8(guest, p));
        } else if (op == 0xC1) {
            uint8_t m = runtime_u8(guest, p), group = (m >> 3) & 7, r = runtime_reg(m & 7);
            out.rex(0, 0, r); out.b(op); out.modrm(3, group, r); out.b(runtime_u8(guest, p));
        } else if (op == 0x74 || op == 0x75) {
            int8_t rel = int8_t(runtime_u8(guest, p));
            uint32_t target = guest_pc + uint32_t(p) + uint32_t(int32_t(rel));
            uint32_t fallthrough = guest_pc + uint32_t(p);
            out.b(0x0F); out.b(op == 0x74 ? 0x84 : 0x85); size_t branch = out.bytes.size(); out.d(0);
            runtime_mov_imm(out, 11, fallthrough); out.b(0xE9); size_t skip = out.bytes.size(); out.d(0);
            size_t taken = out.bytes.size(); runtime_mov_imm(out, 11, target); size_t end = out.bytes.size();
            out.patch32(branch, int32_t(taken - (branch + 4))); out.patch32(skip, int32_t(end - (skip + 4)));
            break;
        } else throw std::runtime_error("unsupported DBT opcode");
        if (p <= begin) throw std::runtime_error("DBT decoder made no progress");
    }
    if (p == guest.size() && (guest.empty() || (guest.back() != 0x74 && guest.back() != 0x75)))
        runtime_mov_imm(out, 11, guest_pc + uint32_t(p));
    out.b(0x44); out.b(0x89); out.b(0x1C); out.b(0x24);
    out.b(0x4C); out.b(0x8B); out.b(0x5C); out.b(0x24); out.b(16);
    for (unsigned i = 0; i != 8; ++i) runtime_store_reg_mem(out, 11, i * 4, runtime_reg(i));
    out.b(0x9C); out.b(0x58); out.b(0x25); out.d(0x00000CD7); runtime_store_reg_mem(out, 11, 36, 0);
    out.b(0xFC); out.b(0x8B); out.b(0x04); out.b(0x24); runtime_store_reg_mem(out, 11, 32, 0);
    out.b(0x48); out.b(0x83); out.b(0xC4); out.b(8); out.b(0x41); out.b(0x5C); out.b(0x5F);
    out.b(0x8B); out.b(0x47); out.b(32); out.b(0xC3);
    return out.bytes;
}
} // namespace x86::dbt
