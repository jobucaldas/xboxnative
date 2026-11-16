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
    uint8_t exit_opcode = 0;
    bool interpreter_boundary = false;
    RuntimeBlock* linked_target = nullptr;
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
                                  uint32_t& memory_ea, bool& memory_operand) {
    size_t p = start; uint8_t op = runtime_u8(b, p);
    memory_operand = false;
    if (op >= 0x50 && op <= 0x5F) return p - start;
    if (op == 0x68) { runtime_u32(b, p); return p - start; }
    if (op == 0x6A) { runtime_u8(b, p); return p - start; }
    if (op == 0xC2) { runtime_u8(b, p); runtime_u8(b, p); return p - start; }
    if (op == 0xC3 || op == 0xC9) return p - start;
    if (op == 0xE8) { runtime_u32(b, p); return p - start; }
    if (op == 0x8F) {
        uint8_t m = runtime_u8(b, p);
        if (((m >> 3) & 7) != 0) throw std::runtime_error("unsupported pop group");
        if ((m >> 6) != 3) runtime_address(b, p, m);
        return p - start;
    }
    if (op >= 0xB8 && op <= 0xBF) { runtime_u32(b, p); return p - start; }
    if (op == 0x0F) {
        uint8_t op2 = runtime_u8(b, p);
        if (op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBE || op2 == 0xBF) {
            uint8_t m = runtime_u8(b, p);
            if ((m >> 6) != 3) {
                RuntimeAddress a = runtime_address(b, p, m); memory_operand = true;
                if (a.absolute) memory_ea = a.displacement;
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
            RuntimeAddress a = runtime_address(b, p, m); memory_operand = true;
            if (a.absolute) memory_ea = a.displacement;
        }
        if (op == 0x81 || (op == 0xF7 && group == 0)) runtime_u32(b, p);
        else if (op == 0x83 || (op == 0xF6 && group == 0)) runtime_u8(b, p);
        if (unary && group > 3 && !(op == 0xFF && (group == 4 || group == 6))) throw std::runtime_error("unsupported unary DBT group");
        if (op == 0xFE && group > 1) throw std::runtime_error("unsupported byte unary DBT group");
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

inline std::vector<uint8_t> runtime_translate(const std::vector<uint8_t>& guest, uint32_t guest_pc) {
    Emitter out;
    out.b(0x57); out.b(0x41); out.b(0x54); out.b(0x41); out.b(0x55); out.b(0x41); out.b(0x56); // save state*, preserve R12-R14
    out.b(0x48); out.b(0x83); out.b(0xEC); out.b(8);
    runtime_mov_reg_mem(out, 11, 7, 36);
    out.b(0x41); out.b(0x53); out.b(0x9D);
    out.rex(12, 0, 7, true); out.b(0x8B); out.modrm(1, 12, 7); out.b(40);
    for (unsigned i : {0u, 1u, 2u, 3u, 5u, 6u, 7u, 4u}) runtime_mov_reg_mem(out, runtime_reg(i), 7, uint8_t(i * 4));
    size_t p = 0;
    bool branch_terminated = false;
    while (p < guest.size()) {
        size_t begin = p; uint8_t op = runtime_u8(guest, p);
        if (op >= 0x50 && op <= 0x57) {
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
            if ((m >> 6) != 3) { a = runtime_address(guest, p, m); runtime_emit_guest_stack_address(out); out.rex(14, 0, 11); out.b(0x8B); out.modrm(0, 14, 3); runtime_emit_guest_stack_adjust(out, false); if (!a.absolute) throw std::runtime_error("unsupported translated pop memory form"); out.rex(14, 0, 12); out.b(0x89); out.modrm(2, 14, 4); out.b(0x24); out.d(a.displacement); }
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
            RuntimeAddress a; bool memory = (m >> 6) != 3;
            uint8_t dst = runtime_reg((m >> 3) & 7), src = memory ? 11 : runtime_reg(m & 7);
            bool high_byte = !memory && (op2 == 0xB6 || op2 == 0xBE) && (m & 7) >= 4;
            if (memory) { a = runtime_address(guest, p, m); runtime_emit_ea(out, a); }
            if (high_byte) {
                uint8_t source = runtime_reg((m & 7) - 4);
                out.b(0x9C); out.rex(11, 0, source); out.b(0x8B); out.modrm(3, 11, source);
                out.b(0x41); out.b(0xC1); out.b(0xEB); out.b(8); out.b(0x9D);
                src = 11;
            }
            if (!memory && !high_byte && (op2 == 0xB6 || op2 == 0xBE) && src >= 4 && src < 8 && dst < 8) out.b(0x40);
            out.rex(dst, 0, src); out.b(0x0F); out.b(op2); out.modrm(memory ? 0 : 3, dst, memory || high_byte ? 3 : src);
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
                if (memory) runtime_emit_ea(out, a);
                out.rex(reg, 0, rm);
                out.b(op); out.modrm(memory ? 0 : 3, reg, rm);
            } else if (op == 0x84 || op == 0x85) {
                if (op == 0x84) throw std::runtime_error("byte test unsupported");
                uint8_t rm = memory ? 11 : runtime_reg(rmfield), reg = runtime_reg(regfield);
                if (memory) runtime_emit_ea(out, a);
                out.rex(reg, 0, rm); out.b(op); out.modrm(memory ? 0 : 3, reg, rm);
                out.b(0x9C); out.b(0x41); out.b(0x5B); out.b(0x41); out.b(0x81); out.b(0xE3); out.d(~uint32_t(0x10)); out.b(0x41); out.b(0x53); out.b(0x9D);
            } else {
                uint8_t base = uint8_t(op & 0xFC);
                uint8_t reg = runtime_reg(regfield), rm = memory ? 11 : runtime_reg(rmfield);
                if (memory) runtime_emit_ea(out, a);
                uint8_t native_op = op;
                out.rex(reg, 0, rm);
                out.b(native_op); out.modrm(memory ? 0 : 3, reg, rm);
                if (base == 0x08 || base == 0x20 || base == 0x30) {
                    out.b(0x9C); out.b(0x41); out.b(0x5B); out.b(0x41); out.b(0x81); out.b(0xE3); out.d(~uint32_t(0x10)); out.b(0x41); out.b(0x53); out.b(0x9D);
                }
            }
        } else if (op == 0x81 || op == 0x83 || op == 0xF6 || op == 0xF7 || op == 0xFE || op == 0xFF) {
            uint8_t m = runtime_u8(guest, p), group = uint8_t((m >> 3) & 7); bool memory = (m >> 6) != 3;
            RuntimeAddress a; if (memory) a = runtime_address(guest, p, m);
            if (op == 0xFF && (group == 2 || group == 4 || group == 6)) {
                if (memory) { runtime_emit_ea(out, a); out.rex(13, 0, 11); out.b(0x8B); out.modrm(0, 13, 3); }
                else { uint8_t src = runtime_reg(m & 7); out.rex(13, 0, src); out.b(0x8B); out.modrm(3, 13, src); }
                if (group == 6) { runtime_emit_guest_stack_adjust(out, true); runtime_emit_guest_stack_address(out); out.rex(13, 0, 11); out.b(0x89); out.modrm(0, 13, 3); }
                else if (group == 4) { out.rex(11, 0, 13); out.b(0x8B); out.modrm(3, 11, 13); branch_terminated = true; break; }
                else { uint32_t return_eip = guest_pc + uint32_t(p); runtime_emit_guest_stack_adjust(out, true); runtime_emit_guest_stack_address(out); out.rex(0, 0, 11); out.b(0xC7); out.modrm(0, 0, 3); out.d(return_eip); out.rex(11, 0, 13); out.b(0x8B); out.modrm(3, 11, 13); branch_terminated = true; break; }
            } else {
            uint8_t rm = memory ? 11 : runtime_reg(m & 7);
            if (memory) runtime_emit_ea(out, a);
            out.rex(0, 0, rm); out.b(op); out.modrm(memory ? 0 : 3, group, rm);
            }
            if (op == 0x81 || (op == 0xF7 && group == 0)) out.d(runtime_u32(guest, p));
            else if (op == 0x83 || (op == 0xF6 && group == 0)) out.b(runtime_u8(guest, p));
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
    out.b(0x44); out.b(0x89); out.b(0x1C); out.b(0x24);
    out.b(0x4C); out.b(0x8B); out.b(0x5C); out.b(0x24); out.b(32);
    for (unsigned i = 0; i != 8; ++i) runtime_store_reg_mem(out, 11, i * 4, runtime_reg(i));
    out.b(0x9C); out.b(0x58); out.b(0x25); out.d(0x00000CD7); runtime_store_reg_mem(out, 11, 36, 0);
    out.b(0xFC); out.b(0x8B); out.b(0x04); out.b(0x24); runtime_store_reg_mem(out, 11, 32, 0);
    out.b(0x48); out.b(0x83); out.b(0xC4); out.b(8); out.b(0x41); out.b(0x5E); out.b(0x41); out.b(0x5D); out.b(0x41); out.b(0x5C); out.b(0x5F);
    out.b(0x8B); out.b(0x47); out.b(32); out.b(0xC3);
    return out.bytes;
}
} // namespace x86::dbt
