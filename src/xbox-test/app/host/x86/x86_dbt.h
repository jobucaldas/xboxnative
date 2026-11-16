// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
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
} // namespace x86::dbt
