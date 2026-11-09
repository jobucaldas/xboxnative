// SPDX-License-Identifier: GPL-3.0-or-later
// Guest OS surface for the PE32 interpreter (milestone 1/2): the kernel32 and
// msvcrt calls a real i686 CRT startup and a console program make. Every guest
// pointer is bounds- and permission-checked through Memory, so a bad pointer is
// a Fault and never a host access. Buffers the guest hands us are copied in and
// out; no guest address is ever treated as a host pointer.
//
// Only the (dll, symbol) pairs registered here are served. Anything else stays a
// named UnresolvedImport Fault, so an unimplemented API is reported by name
// instead of being silently stubbed. That is deliberate: a milestone subset is
// never mistaken for general Win32 support.
#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "x86_win32.h"
namespace x86 {

// ---- guest heap: first-fit free list over a lazily mapped guest arena ----
struct GuestHeap {
    Memory& mem;
    uint32_t base, mapped_end = 0, top = 0;
    struct Block { uint32_t addr, size; bool free; };
    std::vector<Block> blocks;
    GuestHeap(Memory& m, uint32_t at) : mem(m), base(at) {}
    void map_upto(uint32_t end) {
        if (end <= mapped_end) return;
        uint32_t a = mapped_end ? mapped_end : base;
        uint32_t want = ((end - a) + 0xffffu) & ~0xffffu;
        mem.map(a, want, PROT_R | PROT_W);
        mapped_end = a + want;
    }
    uint32_t alloc(uint32_t n) {
        if (!n) n = 1;
        n = (n + 7u) & ~7u;
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (!blocks[i].free || blocks[i].size < n) continue;
            uint32_t a = blocks[i].addr, sz = blocks[i].size;
            if (sz >= n + 16) {
                blocks[i].size = n; blocks[i].free = false;
                blocks.insert(blocks.begin() + long(i) + 1, Block{a + n, sz - n, true});
            } else blocks[i].free = false;
            return a;
        }
        if (!top) top = base;
        map_upto(top + n);
        uint32_t a = top;
        blocks.push_back(Block{a, n, false});
        top = a + n;
        return a;
    }
    uint32_t size_of(uint32_t addr) const {
        for (const Block& b : blocks) if (b.addr == addr) return b.size;
        return 0;
    }
    void release(uint32_t addr) {
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i].addr != addr) continue;
            blocks[i].free = true;
            for (size_t j = i + 1; j < blocks.size() && blocks[j].free; ++j) {  // coalesce forward
                blocks[i].size += blocks[j].size; blocks.erase(blocks.begin() + long(j)); --j;
            }
            if (i && blocks[i - 1].free) {  // coalesce backward
                blocks[i - 1].size += blocks[i].size; blocks.erase(blocks.begin() + long(i));
            }
            return;
        }
    }
    uint32_t realloc(uint32_t addr, uint32_t n) {
        uint32_t old = size_of(addr);
        uint32_t p = alloc(n);
        if (old) mem.write_bytes(p, mem.read_bytes(addr, old < n ? old : n).data(), old < n ? old : n);
        if (addr) release(addr);
        return p;
    }
};

// ---- the guest process surface ----
struct Guest {
    Memory& mem; Host& host; Registry& reg;
    GuestHeap heap;
    std::string exe_path, cmdline;
    std::map<std::string, uint32_t> modules;   // lowercase module name -> guest base
    std::vector<uint32_t> atexit_fns;
    std::vector<uint8_t> iob;                  // 3 FILE structs, guest-visible
    uint32_t iob_addr = 0x7FFD0000;
    uint32_t initenv = 0, commode = 0, fmode = 0, mb_cur_max = 0, errno_ptr = 0;
    uint32_t startupinfo = 0, cmdline_ptr = 0, env_ptr = 0, module_name_ptr = 0;
    uint32_t image_base = 0;
    unsigned heap_serial = 0;
    std::map<uint32_t, uint32_t> heap_handles;  // handle -> arena base
    std::map<uint32_t, uint32_t> virtual_alloc; // guest addr -> size
    uint32_t virtual_top = 0x20000000;
    unsigned next_tls = 0;
    Guest(Memory& m, Host& h, Registry& r) : mem(m), host(h), reg(r), heap(m, 0x10000000) {}
    void register_all();
    // Module names are case-insensitive on Windows, and GetModuleHandleA
    // lowercases its lookup, so every insert goes through here: a mixed-case
    // executable name ("Proteus.exe") must still be found by the guest.
    void add_module(std::string name, uint32_t base) {
        for (auto& ch : name) ch = char(std::tolower((unsigned char)ch));
        modules[name] = base;
    }
    // --- guest-memory helpers ---
    std::string str(uint32_t p) const { return p ? mem.read_cstr(p) : std::string(); }
    uint32_t put_str(const std::string& s) {
        uint32_t p = heap.alloc(uint32_t(s.size() + 1));
        mem.write_bytes(p, s.c_str(), uint32_t(s.size() + 1));
        return p;
    }
    uint32_t put_wstr(const std::u16string& s) {
        uint32_t p = heap.alloc(uint32_t((s.size() + 1) * 2));
        for (size_t i = 0; i < s.size(); ++i) { mem.w8(p + uint32_t(i * 2), uint8_t(s[i])); mem.w8(p + uint32_t(i * 2 + 1), uint8_t(s[i] >> 8)); }
        mem.w8(p + uint32_t(s.size() * 2), 0); mem.w8(p + uint32_t(s.size() * 2 + 1), 0);
        return p;
    }
    void copy_out(uint32_t dst, const void* src, uint32_t n) { if (dst && n) mem.write_bytes(dst, src, n); }
    void copy_in(void* dst, uint32_t src, uint32_t n) { if (src && n) { auto b = mem.read_bytes(src, n); std::memcpy(dst, b.data(), n); } }
    uint32_t last_error() const { return mem.r32(TEB_ADDR + 0x34); }
    void set_last_error(uint32_t v) { mem.w32(TEB_ADDR + 0x34, v); }
    // A FILE* is one of our three iob entries; anything else is a bad handle.
    // The index is the C stream number: 0 stdin, 1 stdout, 2 stderr.
    int file_index(uint32_t f) const {
        if (f >= iob_addr && f < iob_addr + 3 * 32 && (f - iob_addr) % 32 == 0) return int((f - iob_addr) / 32);
        return -1;
    }
    uint32_t file_ptr(int i) const { return iob_addr + uint32_t(i) * 32; }
    // `which` is a C stream number (0/1/2), the same one file_index returns, so
    // stderr reaches host.err on both the FILE* and the WriteFile path.
    void emit(int which, const std::string& s) {
        (which == 2 ? host.err : host.out) += s;
        if (host.tee) host.tee(s);
    }
    void write_file(int which, const std::string& s) { emit(which, s); }
    // printf-family core. `next` yields the next 32-bit argument slot: the guest
    // stack for printf/fprintf/sprintf, or a walk over the guest va_list for the
    // v* forms (on i386 a va_list is a char* to the first vararg). The i386
    // calling convention aligns every argument to 4 bytes only: a double is two
    // consecutive slots wherever they fall, with no padding before it (mingw's
    // va_arg and msvcrt's own printf both read it that way; measured, not
    // assumed). A `long long`/`__int64` is likewise two slots, low dword first.
    struct FmtArgs { std::function<uint32_t()> next; };
    std::string format(FmtArgs a, const std::string& fmt);
    // Copy a formatted result into guest memory. A negative `cap` means
    // unbounded (sprintf); 0 writes nothing (snprintf with a zero size). With
    // append_nul the result is NUL-terminated inside cap. Returns the full
    // length the format would have produced, as C99 snprintf does.
    int store(const std::string& s, uint32_t out_ptr, int cap, bool append_nul);
    std::string format_stack(Cpu& c, const std::string& fmt, unsigned first_arg) {
        unsigned ai = first_arg;
        return format({[&c, &ai]() { return c.arg(ai++); }}, fmt);
    }
    std::string format_va(const std::string& fmt, uint32_t& ap) {
        return format({[this, &ap]() { uint32_t v = mem.r32(ap); ap += 4; return v; }}, fmt);
    }
    // sprintf/_snprintf/snprintf take their varargs on the stack, not a va_list:
    // only the v* forms walk a pointer.
    int format_stack_to(Cpu& c, uint32_t fmt_ptr, unsigned first_arg, uint32_t out_ptr, int cap, bool append_nul) {
        return store(format_stack(c, str(fmt_ptr), first_arg), out_ptr, cap, append_nul);
    }
    int format_va_to(uint32_t fmt_ptr, uint32_t argp, uint32_t out_ptr, int cap, bool append_nul) {
        return store(format_va(str(fmt_ptr), argp), out_ptr, cap, append_nul);
    }
    // SYSTEMTIME is eight WORDs (year, month, dayOfWeek, day, hour, minute,
    // second, milliseconds), 16 bytes total, so every field is a 2-byte store.
    void fill_system_time(uint32_t p) {
        if (!p) return;
        static const uint16_t f[8] = {2026, 11, 0, 9, 12, 0, 0, 0};
        for (unsigned i = 0; i < 8; ++i) mem.w(p + i * 2, f[i], 2);
    }
    void run_atexit(Cpu& c);
    void do_exit(Cpu& c, uint32_t code);
};

// ---- number formatting shared by the printf family ----
namespace guest_fmt {
inline std::string pad(const std::string& body, int width, bool left, char fill) {
    if (width <= int(body.size())) return body;
    std::string p(size_t(width - int(body.size())), fill);
    return left ? body + p : p + body;
}
inline std::string int_to(uint64_t v, int base, bool upper, bool neg) {
    const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string s;
    do { s.push_back(d[v % uint64_t(base)]); v /= uint64_t(base); } while (v);
    if (neg) s.push_back('-');
    return std::string(s.rbegin(), s.rend());
}
inline std::string uint_to(uint64_t v, int base, bool upper) {
    const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string s;
    do { s.push_back(d[v % uint64_t(base)]); v /= uint64_t(base); } while (v);
    return std::string(s.rbegin(), s.rend());
}
}

inline std::string Guest::format(FmtArgs a, const std::string& fmt) {
    auto& next = a.next;
    std::string out;
    size_t i = 0;
    while (i < fmt.size()) {
        char ch = fmt[i++];
        if (ch != '%') { out.push_back(ch); continue; }
        if (i < fmt.size() && fmt[i] == '%') { out.push_back('%'); ++i; continue; }
        bool left = false, plus = false, space = false, alt = false, zero = false;
        for (;;) {
            if (i >= fmt.size()) break;
            char f = fmt[i];
            if (f == '-') left = true; else if (f == '+') plus = true; else if (f == ' ') space = true;
            else if (f == '#') alt = true; else if (f == '0') zero = true; else break;
            ++i;
        }
        int width = 0;
        if (i < fmt.size() && fmt[i] == '*') { width = int(next()); if (width < 0) { left = true; width = -width; } ++i; }
        else while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') width = width * 10 + (fmt[i++] - '0');
        int prec = -1;
        if (i < fmt.size() && fmt[i] == '.') {
            ++i; prec = 0;
            if (i < fmt.size() && fmt[i] == '*') { prec = int(next()); ++i; if (prec < 0) prec = -1; }
            else while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') prec = prec * 10 + (fmt[i++] - '0');
        }
        enum { LEN_NONE, LEN_H, LEN_HH, LEN_L, LEN_LL, LEN_LDBL, LEN_Z, LEN_I64 } len = LEN_NONE;
        if (i < fmt.size()) {
            char l = fmt[i];
            if (l == 'h') { ++i; if (i < fmt.size() && fmt[i] == 'h') { ++i; len = LEN_HH; } else len = LEN_H; }
            else if (l == 'l') { ++i; if (i < fmt.size() && fmt[i] == 'l') { ++i; len = LEN_LL; } else len = LEN_L; }
            else if (l == 'L') { ++i; len = LEN_LDBL; }
            else if (l == 'z') { ++i; len = LEN_Z; }
            else if (l == 'I') {
                ++i;
                if (i + 1 < fmt.size() && fmt[i] == '6' && fmt[i + 1] == '4') { i += 2; len = LEN_I64; }
                else if (i + 1 < fmt.size() && fmt[i] == '3' && fmt[i + 1] == '2') { i += 2; len = LEN_NONE; }
                else if (i < fmt.size() && fmt[i] == 'd') { len = LEN_Z; }  // %Id
            }
        }
        if (i >= fmt.size()) break;
        char conv = fmt[i++];
        switch (conv) {
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': {
            // On Win32 `long` is 32 bits, so %l is one slot; %ll and %I64 are
            // two (low dword first). %z is size_t, 32 bits on i386.
            uint64_t v = next();
            bool wide = len == LEN_LL || len == LEN_I64;
            if (wide) v |= uint64_t(next()) << 32;
            else v &= 0xffffffffu;
            if (len == LEN_H) v &= 0xffffu;
            if (len == LEN_HH) v &= 0xffu;
            bool neg = false; std::string body;
            if (conv == 'd' || conv == 'i') {
                int64_t s = wide ? int64_t(v) : int64_t(int32_t(uint32_t(v)));
                if (len == LEN_H) s = int16_t(v);
                if (len == LEN_HH) s = int8_t(v);
                neg = s < 0;
                body = guest_fmt::uint_to(neg ? uint64_t(0) - uint64_t(s) : uint64_t(s), 10, false);
            } else body = guest_fmt::uint_to(v, conv == 'o' ? 8 : conv == 'u' ? 10 : 16, conv == 'X');
            if (prec >= 0 && int(body.size()) < prec) body = std::string(size_t(prec - int(body.size())), '0') + body;
            if (alt) {
                if (conv == 'x') body = "0x" + body;
                else if (conv == 'X') body = "0X" + body;
                else if (conv == 'o' && (body.empty() || body[0] != '0')) body = "0" + body;
            }
            if (neg) body = "-" + body;
            else if (plus && (conv == 'd' || conv == 'i')) body = "+" + body;
            else if (space && (conv == 'd' || conv == 'i')) body = " " + body;
            out += guest_fmt::pad(body, width, left, zero && !left && prec < 0 ? '0' : ' ');
            break; }
        case 'c': { char v = char(next()); out += guest_fmt::pad(std::string(1, v), width, left, ' '); break; }
        case 's': {
            uint32_t p = next(); std::string s = p ? str(p) : std::string("(null)");
            if (prec >= 0 && int(s.size()) > prec) s.resize(size_t(prec));
            out += guest_fmt::pad(s, width, left, ' ');
            break; }
        case 'p': {
            uint32_t p = next();
            std::string body = "00000000" + guest_fmt::uint_to(p, 16, false);
            out += "0x" + body.substr(body.size() - 8);
            break; }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            // A double vararg occupies two 32-bit stack slots (little-endian: low
            // dword first) with no alignment padding on i386. long double is 12
            // bytes on i386, but no CRT printf path in a game passes one without
            // an explicit cast, and MSVC's %Lf is not used by the fixtures; treat
            // it as double and say so.
            double v;
            uint32_t lo = next(), hi = next(); uint64_t bits = uint64_t(hi) << 32 | lo; std::memcpy(&v, &bits, 8);
            char spec[32]; int k = 0;
            spec[k++] = '%';
            if (left) spec[k++] = '-';
            if (plus) spec[k++] = '+';
            if (space) spec[k++] = ' ';
            if (alt) spec[k++] = '#';
            if (zero) spec[k++] = '0';
            if (width) k += std::snprintf(spec + k, sizeof spec - size_t(k), "%d", width);
            if (prec >= 0) k += std::snprintf(spec + k, sizeof spec - size_t(k), ".%d", prec);
            spec[k++] = conv; if (conv == 'F') spec[k - 1] = 'f'; if (conv == 'E') spec[k - 1] = 'e'; if (conv == 'G') spec[k - 1] = 'g';
            spec[k] = 0;
            char buf[512];
            int n = std::snprintf(buf, sizeof buf, spec, v);
            if (n < 0) n = 0;
            if (size_t(n) >= sizeof buf) n = int(sizeof buf) - 1;
            out.append(buf, size_t(n));
            break; }
        case 'n': { uint32_t p = next(); if (p) mem.w32(p, uint32_t(out.size())); break; }
        default: out.push_back('%'); out.push_back(conv); break;
        }
    }
    return out;
}

inline int Guest::store(const std::string& s, uint32_t out_ptr, int cap, bool append_nul) {
    int n = int(s.size());
    if (cap != 0) {
        // cap < 0 is an unbounded buffer: every byte fits, plus the terminator.
        int room = cap < 0 ? n : (append_nul ? cap - 1 : cap);
        if (room < 0) room = 0;
        int put = n < room ? n : room;
        if (put > 0) mem.write_bytes(out_ptr, s.data(), uint32_t(put));
        if (append_nul) mem.w8(out_ptr + uint32_t(put), 0);
    }
    return n;
}

inline void Guest::run_atexit(Cpu& c) {
    while (!atexit_fns.empty()) {
        uint32_t fn = atexit_fns.back(); atexit_fns.pop_back();
        if (fn) c.call(fn, {});
    }
}
inline void Guest::do_exit(Cpu& c, uint32_t code) {
    try { run_atexit(c); } catch (...) { c.exited = true; c.exit_code = code; throw; }
    c.exited = true; c.exit_code = code;
}

// ---- registration ----
inline void Guest::register_all() {
    reg.map.clear();
    register_win32(reg, host);  // the stage-1 allowlist stays the base surface
    // Fixed guest structures: three FILE structs for stdin/stdout/stderr.
    mem.map(iob_addr, 4096, PROT_R | PROT_W);
    iob.assign(3 * 32, 0);
    mem.write_bytes(iob_addr, iob.data(), uint32_t(iob.size()));
    for (int i = 0; i < 3; ++i) mem.w32(iob_addr + uint32_t(i * 32), 0xFFFFFFFFu);  // _file = -1
    initenv = heap.alloc(16); commode = heap.alloc(16); fmode = heap.alloc(16);
    mb_cur_max = heap.alloc(16); errno_ptr = heap.alloc(16);
    mem.w32(commode, 0); mem.w32(fmode, 0); mem.w32(mb_cur_max, 1); mem.w32(errno_ptr, 0);
    startupinfo = heap.alloc(68); mem.write_bytes(startupinfo, std::vector<uint8_t>(68, 0).data(), 68);
    mem.w32(startupinfo, 68);  // cb
    cmdline_ptr = put_str(cmdline.empty() ? exe_path : cmdline);
    env_ptr = heap.alloc(4); mem.w32(env_ptr, 0);
    module_name_ptr = put_str(exe_path);
    auto C = [](const char* n) { return n; };
    (void)C;

    // kernel32: process, error, memory, sync, TLS
    reg.add("kernel32.dll", "GetLastError", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return last_error(); });
    reg.add("kernel32.dll", "SetLastError", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { set_last_error(c.arg(0)); return 0; });
    reg.add("kernel32.dll", "GetCurrentProcess", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0xFFFFFFFFu; });
    reg.add("kernel32.dll", "GetCurrentProcessId", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0x1000; });
    reg.add("kernel32.dll", "GetCurrentThread", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0xFFFFFFFEu; });
    reg.add("kernel32.dll", "GetCurrentThreadId", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0x1004; });
    reg.add("kernel32.dll", "GetCommandLineA", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return cmdline_ptr; });
    reg.add("kernel32.dll", "GetCommandLineW", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return put_wstr(u"x"); });
    reg.add("kernel32.dll", "GetStartupInfoA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        auto b = mem.read_bytes(startupinfo, 68); copy_out(c.arg(0), b.data(), 68); return 0; });
    reg.add("kernel32.dll", "GetEnvironmentStrings", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return env_ptr; });
    reg.add("kernel32.dll", "GetEnvironmentStringsW", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return put_wstr(u"\0"); });
    reg.add("kernel32.dll", "FreeEnvironmentStringsA", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "FreeEnvironmentStringsW", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "GetModuleHandleA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        std::string n = str(c.arg(0)); if (n.empty()) return image_base;
        for (auto& ch : n) ch = char(tolower((unsigned char)ch));
        auto it = modules.find(n); return it == modules.end() ? 0 : it->second; });
    reg.add("kernel32.dll", "GetModuleHandleW", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        if (!c.arg(0)) return image_base;
        return 0; });
    // FreeLibrary: the guest cannot unload a module it did not load through a
    // path this surface tracks, so a handle for the image or a known module is
    // accepted and nothing is unmapped. Anything else fails with
    // ERROR_MOD_NOT_FOUND, which is what Windows' LdrUnloadDll and Wine answer.
    reg.add("kernel32.dll", "FreeLibrary", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t h = c.arg(0);
        if (h == image_base) return 1;
        for (const auto& m : modules) if (m.second == h) return 1;
        set_last_error(126);  // ERROR_MOD_NOT_FOUND
        return 0; });
    // IsDBCSLeadByteEx: the code pages this surface answers (1252, 437, 65001)
    // are all single-byte or UTF-8, so no byte is a lead byte.
    reg.add("kernel32.dll", "IsDBCSLeadByteEx", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "IsDBCSLeadByte", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    // (hModule, lpFilename, nSize): arg(0) is the module handle, not the buffer.
    reg.add("kernel32.dll", "GetModuleFileNameA", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t buf = c.arg(1), n = c.arg(2); if (!buf || !n) return 0;
        uint32_t put = uint32_t(exe_path.size()) < n ? uint32_t(exe_path.size()) : n - 1;
        mem.write_bytes(buf, exe_path.data(), put); mem.w8(buf + put, 0); return put; });
    reg.add("kernel32.dll", "GetModuleFileNameW", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t buf = c.arg(1), n = c.arg(2); if (!buf || !n) return 0;
        std::u16string w(exe_path.begin(), exe_path.end());
        uint32_t put = uint32_t(w.size()) < n ? uint32_t(w.size()) : n - 1;
        for (uint32_t i = 0; i < put; ++i) { mem.w8(buf + i * 2, uint8_t(w[i])); mem.w8(buf + i * 2 + 1, uint8_t(w[i] >> 8)); }
        mem.w8(buf + put * 2, 0); mem.w8(buf + put * 2 + 1, 0); return put; });
    reg.add("kernel32.dll", "ExitProcess", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { do_exit(c, c.arg(0)); return 0; });
    reg.add("kernel32.dll", "TerminateProcess", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { do_exit(c, c.arg(1)); return 1; });
    reg.add("kernel32.dll", "Sleep", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "SleepEx", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "SetUnhandledExceptionFilter", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "UnhandledExceptionFilter", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "RaiseException", Conv::Stdcall, 4, [](Cpu& c) -> uint32_t {
        throw Fault(FaultKind::Unsupported, c.eip, c.arg(0), "RaiseException (guest SEH not implemented)"); });
    // GetVersion packs major in the low byte and minor in the next: 10.0.19045.
    reg.add("kernel32.dll", "GetVersion", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0x4A65000Au; });
    reg.add("kernel32.dll", "GetVersionExA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (!p) return 0;
        uint32_t cb = mem.r32(p); if (cb < 148) { set_last_error(122); return 0; }
        mem.w32(p + 4, 10); mem.w32(p + 8, 0); mem.w32(p + 12, 19045); mem.w32(p + 16, 2);
        // OSVERSIONINFOA: the five dwords, then szCSDVersion[128] at offset 20,
        // so the struct is 148 bytes. OSVERSIONINFOEXA appends wServicePackMajor,
        // wServicePackMinor, wSuiteMask, wProductType and wReserved (156 bytes).
        // Nothing is written past the size the caller declared.
        std::string cs = "Service Pack 0";
        mem.write_bytes(p + 20, cs.c_str(), uint32_t(cs.size() + 1));
        if (cb >= 156) {
            mem.w(p + 148, 0, 2);  // wServicePackMajor
            mem.w(p + 150, 0, 2);  // wServicePackMinor
            mem.w(p + 152, 0, 2);  // wSuiteMask
            mem.w(p + 154, 1, 2);  // wProductType: VER_NT_WORKSTATION
            mem.w8(p + 155, 0);    // wReserved
        }
        return 1; });
    reg.add("kernel32.dll", "IsProcessorFeaturePresent", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t {
        return c.arg(0) == 10 ? 1u : 0u; });  // PF_XMMI64_INSTRUCTIONS_AVAILABLE (SSE2)
    reg.add("kernel32.dll", "GetACP", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 1252; });
    reg.add("kernel32.dll", "GetOEMCP", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 437; });
    reg.add("kernel32.dll", "GetCPInfo", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t cp = c.arg(0), p = c.arg(1); if (!p) return 0;
        mem.w32(p, cp == 65001 ? 65001u : 0u); mem.w32(p + 4, 1); mem.w8(p + 8, '?'); mem.w8(p + 9, 0);
        for (int i = 10; i < 20; ++i) mem.w8(p + uint32_t(i), 0);
        return 1; });
    reg.add("kernel32.dll", "MultiByteToWideChar", Conv::Stdcall, 6, [this](Cpu& c) -> uint32_t {
        uint32_t cp = c.arg(0), src = c.arg(2), n = c.arg(3), dst = c.arg(4), cap = c.arg(5);
        (void)cp; std::string s = n == 0xFFFFFFFFu ? str(src) : std::string();
        if (n != 0xFFFFFFFFu) { auto b = mem.read_bytes(src, n); s.assign(b.begin(), b.end()); }
        if (!dst || !cap) return uint32_t(s.size() + 1);
        uint32_t put = uint32_t(s.size()) < cap ? uint32_t(s.size()) : cap - 1;
        for (uint32_t i = 0; i < put; ++i) { mem.w8(dst + i * 2, uint8_t(s[i])); mem.w8(dst + i * 2 + 1, 0); }
        if (put < cap) { mem.w8(dst + put * 2, 0); mem.w8(dst + put * 2 + 1, 0); }
        return put; });
    reg.add("kernel32.dll", "WideCharToMultiByte", Conv::Stdcall, 8, [this](Cpu& c) -> uint32_t {
        uint32_t src = c.arg(2), n = c.arg(3), dst = c.arg(4), cap = c.arg(5);
        std::string s;
        if (n == 0xFFFFFFFFu) { for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(i * 2 + src, 2)); if (!w) break; s.push_back(char(w & 0xff)); } }
        else for (uint32_t i = 0; i < n; ++i) s.push_back(char(mem.r(src + i * 2, 2) & 0xff));
        if (!dst || !cap) return uint32_t(s.size() + 1);
        uint32_t put = uint32_t(s.size()) < cap ? uint32_t(s.size()) : cap - 1;
        mem.write_bytes(dst, s.data(), put); if (put < cap) mem.w8(dst + put, 0);
        return put; });
    // Heap
    reg.add("kernel32.dll", "GetProcessHeap", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0x50000000u; });
    reg.add("kernel32.dll", "HeapCreate", Conv::Stdcall, 3, [this](Cpu&) -> uint32_t {
        uint32_t h = 0x50000001u + heap_serial++; heap_handles[h] = 0; return h; });
    reg.add("kernel32.dll", "HeapDestroy", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { heap_handles.erase(c.arg(0)); return 1; });
    reg.add("kernel32.dll", "HeapAlloc", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t n = c.arg(2); if (!n) n = 1;
        uint32_t p = heap.alloc(n);
        if (c.arg(1) & 8) { auto z = std::vector<uint8_t>(n, 0); mem.write_bytes(p, z.data(), n); }  // HEAP_ZERO_MEMORY
        return p; });
    reg.add("kernel32.dll", "HeapFree", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t { heap.release(c.arg(2)); return 1; });
    reg.add("kernel32.dll", "HeapReAlloc", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t { return heap.realloc(c.arg(2), c.arg(3)); });
    reg.add("kernel32.dll", "HeapSize", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t { return heap.size_of(c.arg(2)); });
    reg.add("kernel32.dll", "LocalAlloc", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { return heap.alloc(c.arg(1)); });
    reg.add("kernel32.dll", "LocalFree", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { heap.release(c.arg(0)); return 0; });
    reg.add("kernel32.dll", "GlobalAlloc", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { return heap.alloc(c.arg(1)); });
    reg.add("kernel32.dll", "GlobalFree", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { heap.release(c.arg(0)); return 0; });
    // Virtual memory
    reg.add("kernel32.dll", "VirtualAlloc", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        uint32_t want = (c.arg(1) + 0xfffu) & ~0xfffu; if (!want) want = 0x1000;
        uint32_t p = virtual_top; virtual_top += want;
        mem.map(p, want, PROT_R | PROT_W); virtual_alloc[p] = want;
        if (c.arg(3) & 0x40) { auto z = std::vector<uint8_t>(want, 0); mem.write_bytes(p, z.data(), want); }
        return p; });
    reg.add("kernel32.dll", "VirtualFree", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        auto it = virtual_alloc.find(c.arg(0)); if (it == virtual_alloc.end()) return 0;
        virtual_alloc.erase(it); return 1; });
    reg.add("kernel32.dll", "VirtualProtect", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        uint32_t a = c.arg(0) & ~0xfffu, n = (c.arg(1) + 0xfffu) & ~0xfffu, prot = c.arg(2), old = c.arg(3);
        uint8_t p = PROT_R | ((prot & 0x04) ? PROT_W : 0) | ((prot & 0x20) ? PROT_X : 0);
        if (!n) n = 0x1000;
        try { mem.protect(a, n, p); } catch (...) { set_last_error(487); return 0; }
        if (old) mem.w32(old, 0x04);
        return 1; });
    reg.add("kernel32.dll", "VirtualQuery", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t a = c.arg(0), p = c.arg(1); if (!p) return 0;
        uint32_t base = a & ~0xfffu, size = 0x1000;
        mem.w32(p + 0, base); mem.w32(p + 4, base); mem.w32(p + 8, 0x04);
        mem.w32(p + 12, size); mem.w32(p + 16, mem.mapped(a) ? 0x1000u : 0u); mem.w32(p + 20, 0x04); mem.w32(p + 24, 0x20000);
        return 28; });
    // Critical sections: one guest thread per CPU, so they are no-ops.
    for (const char* n : {"InitializeCriticalSection", "EnterCriticalSection", "LeaveCriticalSection", "DeleteCriticalSection"})
        reg.add("kernel32.dll", n, Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "InitializeCriticalSectionAndSpinCount", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "TryEnterCriticalSection", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "InitializeCriticalSectionEx", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 1; });
    // TLS
    reg.add("kernel32.dll", "TlsAlloc", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t {
        if (next_tls >= 64) return 0xFFFFFFFFu;
        return next_tls++; });
    reg.add("kernel32.dll", "TlsFree", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "TlsGetValue", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t i = c.arg(0); if (i >= 64) { set_last_error(87); return 0; }
        return mem.r32(TEB_ADDR + 0xE10 + i * 4); });
    reg.add("kernel32.dll", "TlsSetValue", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t i = c.arg(0); if (i >= 64) { set_last_error(87); return 0; }
        mem.w32(TEB_ADDR + 0xE10 + i * 4, c.arg(1)); return 1; });
    // Interlocked
    reg.add("kernel32.dll", "InterlockedIncrement", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t v = mem.r32(c.arg(0)) + 1; mem.w32(c.arg(0), v); return v; });
    reg.add("kernel32.dll", "InterlockedDecrement", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t v = mem.r32(c.arg(0)) - 1; mem.w32(c.arg(0), v); return v; });
    reg.add("kernel32.dll", "InterlockedExchange", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t old = mem.r32(c.arg(0)); mem.w32(c.arg(0), c.arg(1)); return old; });
    reg.add("kernel32.dll", "InterlockedCompareExchange", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t old = mem.r32(c.arg(0)); if (old == c.arg(2)) mem.w32(c.arg(0), c.arg(1)); return old; });
    reg.add("kernel32.dll", "InterlockedExchangeAdd", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t old = mem.r32(c.arg(0)); mem.w32(c.arg(0), old + c.arg(1)); return old; });
    // Time
    reg.add("kernel32.dll", "GetTickCount", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 12345; });
    reg.add("kernel32.dll", "GetTickCount64", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 12345; });
    reg.add("kernel32.dll", "QueryPerformanceCounter", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) { mem.w32(p, 1000); mem.w32(p + 4, 0); } return 1; });
    reg.add("kernel32.dll", "QueryPerformanceFrequency", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) { mem.w32(p, 1000000); mem.w32(p + 4, 0); } return 1; });
    reg.add("kernel32.dll", "GetSystemTimeAsFileTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) { mem.w32(p, 0x80000000u); mem.w32(p + 4, 0x01D00000u); } return 0; });
    reg.add("kernel32.dll", "GetSystemTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        fill_system_time(c.arg(0)); return 0; });
    reg.add("kernel32.dll", "GetLocalTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        fill_system_time(c.arg(0)); return 0; });
    reg.add("kernel32.dll", "GetSystemInfo", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (!p) return 0;
        mem.w32(p + 0, 0); mem.w32(p + 4, 4096); mem.w32(p + 8, 0x10000); mem.w32(p + 12, 0x7FFE0000u);
        mem.w32(p + 16, 1); mem.w32(p + 20, 1); mem.w32(p + 24, 0); mem.w32(p + 28, 0); mem.w32(p + 32, 9);
        return 0; });
    // Console output handles: stdout/stderr are the Host sinks.
    reg.add("kernel32.dll", "GetStdHandle", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t {
        switch (int32_t(c.arg(0))) { case -10: return Host::H_STDIN; case -11: return Host::H_STDOUT; case -12: return Host::H_STDERR; }
        return 0xFFFFFFFFu; });
    reg.add("kernel32.dll", "WriteFile", Conv::Stdcall, 5, [this](Cpu& c) -> uint32_t {
        uint32_t handle = c.arg(0), buf = c.arg(1), n = c.arg(2), written = c.arg(3);
        if (handle != Host::H_STDOUT && handle != Host::H_STDERR) { set_last_error(6); return 0; }  // ERROR_INVALID_HANDLE
        if (n > (1u << 24)) { set_last_error(87); return 0; }
        auto bytes = mem.read_bytes(buf, n);
        emit(handle == Host::H_STDOUT ? 1 : 2, std::string(bytes.begin(), bytes.end()));
        if (written) mem.w32(written, n);
        return 1; });
    reg.add("kernel32.dll", "GetFileType", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t {
        return (c.arg(0) == Host::H_STDOUT || c.arg(0) == Host::H_STDERR) ? 2u : 0u; });  // FILE_TYPE_CHAR
    reg.add("kernel32.dll", "SetConsoleCtrlHandler", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "FlushFileBuffers", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "CloseHandle", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "SetHandleCount", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t { return c.arg(0); });

    // msvcrt: CRT startup, heap, strings, stdio
    reg.add("msvcrt.dll", "_lock", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "_unlock", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "__lc_codepage", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return mb_cur_max; });
    reg.add("msvcrt.dll", "wcslen", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        uint32_t n = 0; while (mem.r(c.arg(0) + n * 2, 2)) ++n; return n; });
    reg.add("msvcrt.dll", "strerror", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        static const char* msgs[] = {"No error", "Operation not permitted", "No such file or directory", "No such process"};
        uint32_t e = c.arg(0); return put_str(e < 4 ? msgs[e] : "Unknown error"); });
    reg.add("msvcrt.dll", "localeconv", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t {
        // struct lconv: 10 char* fields then 8 chars; all but the decimal point are "".
        uint32_t p = heap.alloc(48); std::vector<uint8_t> z(48, 0); mem.write_bytes(p, z.data(), 48);
        uint32_t empty = put_str(""), dot = put_str(".");
        for (int i = 0; i < 10; ++i) mem.w32(p + uint32_t(i * 4), i == 0 ? dot : empty);
        return p; });
    // setlocale(category, locale): the only locale this surface implements is
    // "C"/"" (the process default), so a query or a request for it answers
    // "C" and anything else answers NULL, as msvcrt does for an unavailable
    // locale. mingw's CRT calls it from its startup path.
    reg.add("msvcrt.dll", "setlocale", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint32_t name = c.arg(1);
        if (name) {
            std::string want = str(name);
            if (!want.empty() && want != "C" && want != "POSIX") return 0;
        }
        return put_str("C"); });
    reg.add("kernel32.dll", "GetProcAddress", Conv::Stdcall, 2, [](Cpu& c) -> uint32_t {
        throw Fault(FaultKind::UnresolvedImport, c.eip, 0, "GetProcAddress: no export table for guest modules"); });
    reg.add("kernel32.dll", "LoadLibraryA", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t {
        throw Fault(FaultKind::Unsupported, c.eip, 0, "LoadLibraryA from guest code is not implemented"); });
    reg.add("kernel32.dll", "LoadLibraryW", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t {
        throw Fault(FaultKind::Unsupported, c.eip, 0, "LoadLibraryW from guest code is not implemented"); });
    reg.add("msvcrt.dll", "__set_app_type", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "__setusermatherr", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "_amsg_exit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { do_exit(c, c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "__p___initenv", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return initenv; });
    reg.add("msvcrt.dll", "__p__commode", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return commode; });
    reg.add("msvcrt.dll", "__p__fmode", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return fmode; });
    reg.add("msvcrt.dll", "__p___mb_cur_max", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return mb_cur_max; });
    reg.add("msvcrt.dll", "_errno", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return errno_ptr; });
    reg.add("msvcrt.dll", "__p__iob", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return iob_addr; });
    // CRT startup: __getmainargs fills _argc/_argv/_environ and the startupinfo
    // pointer. argv[0] is the exe path, as on Windows.
    reg.add("msvcrt.dll", "__getmainargs", Conv::Cdecl, 5, [this](Cpu& c) -> uint32_t {
        uint32_t argc_p = c.arg(0), argv_p = c.arg(1), env_p = c.arg(2), si_p = c.arg(4);
        if (!argc_p || !argv_p) return 0xFFFFFFFFu;
        uint32_t name = put_str(exe_path);
        uint32_t argv = heap.alloc(8); mem.w32(argv, name); mem.w32(argv + 4, 0);
        mem.w32(argc_p, 1); mem.w32(argv_p, argv); if (env_p) mem.w32(env_p, env_ptr);
        if (si_p) mem.w32(si_p, startupinfo);
        return 0; });
    reg.add("msvcrt.dll", "__wgetmainargs", Conv::Cdecl, 5, [this](Cpu& c) -> uint32_t {
        uint32_t argc_p = c.arg(0), argv_p = c.arg(1), env_p = c.arg(2), si_p = c.arg(4);
        if (!argc_p || !argv_p) return 0xFFFFFFFFu;
        uint32_t name = put_wstr(std::u16string(exe_path.begin(), exe_path.end()));
        uint32_t argv = heap.alloc(8); mem.w32(argv, name); mem.w32(argv + 4, 0);
        mem.w32(argc_p, 1); mem.w32(argv_p, argv); if (env_p) mem.w32(env_p, env_ptr);
        if (si_p) mem.w32(si_p, startupinfo);
        return 0; });
    reg.add("msvcrt.dll", "_get_osfhandle", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        uint32_t fd = c.arg(0);
        return fd == 0 ? Host::H_STDIN : fd == 1 ? Host::H_STDOUT : fd == 2 ? Host::H_STDERR : 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_set_fmode", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { mem.w32(fmode, c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "_initterm", Conv::Cdecl, 2, [](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0), end = c.arg(1);
        for (; p < end; p += 4) { uint32_t fn = c.mem.r32(p); if (fn) c.call(fn, {}); }
        return 0; });
    reg.add("msvcrt.dll", "_initterm_e", Conv::Cdecl, 2, [](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0), end = c.arg(1);
        for (; p < end; p += 4) { uint32_t fn = c.mem.r32(p); if (fn) { uint32_t r = c.call(fn, {}); if (r) return r; } }
        return 0; });
    reg.add("msvcrt.dll", "atexit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { atexit_fns.push_back(c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "_onexit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { atexit_fns.push_back(c.arg(0)); return c.arg(0); });
    reg.add("msvcrt.dll", "_cexit", Conv::Cdecl, 0, [this](Cpu& c) -> uint32_t { run_atexit(c); return 0; });
    reg.add("msvcrt.dll", "exit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { do_exit(c, c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "_exit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { c.exited = true; c.exit_code = c.arg(0); return 0; });
    reg.add("msvcrt.dll", "abort", Conv::Cdecl, 0, [this](Cpu& c) -> uint32_t { c.exited = true; c.exit_code = 3; return 0; });
    reg.add("msvcrt.dll", "signal", Conv::Cdecl, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "setvbuf", Conv::Cdecl, 4, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "fflush", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    // heap
    reg.add("msvcrt.dll", "malloc", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return heap.alloc(c.arg(0)); });
    reg.add("msvcrt.dll", "calloc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint32_t n = c.arg(0), sz = c.arg(1); uint64_t total = uint64_t(n) * sz;
        if (total > 0x10000000ull) return 0;
        uint32_t p = heap.alloc(uint32_t(total));
        if (total) { auto z = std::vector<uint8_t>(size_t(total), 0); mem.write_bytes(p, z.data(), uint32_t(total)); }
        return p; });
    reg.add("msvcrt.dll", "free", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { heap.release(c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "realloc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t { return heap.realloc(c.arg(0), c.arg(1)); });
    reg.add("msvcrt.dll", "_malloc", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return heap.alloc(c.arg(0)); });
    reg.add("msvcrt.dll", "_free", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { heap.release(c.arg(0)); return 0; });
    // strings/memory
    reg.add("msvcrt.dll", "strlen", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return uint32_t(str(c.arg(0)).size()); });
    reg.add("msvcrt.dll", "strnlen", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); return uint32_t(s.size()) < c.arg(1) ? uint32_t(s.size()) : c.arg(1); });
    reg.add("msvcrt.dll", "strcmp", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1)); int r = a.compare(b); return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    reg.add("msvcrt.dll", "strncmp", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1)); uint32_t n = c.arg(2);
        int r = a.compare(0, n, b, 0, n); return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    reg.add("msvcrt.dll", "strcpy", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(1)); mem.write_bytes(c.arg(0), s.c_str(), uint32_t(s.size() + 1)); return c.arg(0); });
    reg.add("msvcrt.dll", "strncpy", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(1)); uint32_t n = c.arg(2);
        std::vector<uint8_t> b(n, 0); for (uint32_t i = 0; i < n && i < s.size(); ++i) b[i] = uint8_t(s[i]);
        mem.write_bytes(c.arg(0), b.data(), n); return c.arg(0); });
    reg.add("msvcrt.dll", "strcat", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1)); a += b;
        mem.write_bytes(c.arg(0), a.c_str(), uint32_t(a.size() + 1)); return c.arg(0); });
    // strchr(s, 0) returns the terminator, so the search covers the NUL that
    // read_cstr drops.
    reg.add("msvcrt.dll", "strchr", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); char ch = char(c.arg(1));
        size_t p = ch ? s.find(ch) : s.size();
        return p == std::string::npos ? 0u : c.arg(0) + uint32_t(p); });
    reg.add("msvcrt.dll", "strrchr", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); char ch = char(c.arg(1));
        size_t p = ch ? s.rfind(ch) : s.size();
        return p == std::string::npos ? 0u : c.arg(0) + uint32_t(p); });
    reg.add("msvcrt.dll", "strstr", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)), n = str(c.arg(1));
        size_t p = s.find(n); return p == std::string::npos ? 0u : c.arg(0) + uint32_t(p); });
    reg.add("msvcrt.dll", "memcpy", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), s = c.arg(1), n = c.arg(2);
        if (n) mem.write_bytes(d, mem.read_bytes(s, n).data(), n);
        return d; });
    reg.add("msvcrt.dll", "memmove", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), s = c.arg(1), n = c.arg(2);
        if (n) { auto b = mem.read_bytes(s, n); mem.write_bytes(d, b.data(), n); } return d; });
    reg.add("msvcrt.dll", "memset", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), n = c.arg(2); uint8_t v = uint8_t(c.arg(1));
        std::vector<uint8_t> b(n, v); if (n) mem.write_bytes(d, b.data(), n); return d; });
    reg.add("msvcrt.dll", "memcmp", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t n = c.arg(2); auto a = mem.read_bytes(c.arg(0), n), b = mem.read_bytes(c.arg(1), n);
        int r = n ? std::memcmp(a.data(), b.data(), n) : 0; return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    reg.add("msvcrt.dll", "memchr", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t n = c.arg(2); auto b = mem.read_bytes(c.arg(0), n); uint8_t v = uint8_t(c.arg(1));
        for (uint32_t i = 0; i < n; ++i) if (b[i] == v) return c.arg(0) + i;
        return 0; });
    reg.add("msvcrt.dll", "atoi", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return uint32_t(std::atoi(str(c.arg(0)).c_str())); });
    reg.add("msvcrt.dll", "atol", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return uint32_t(std::atol(str(c.arg(0)).c_str())); });
    reg.add("msvcrt.dll", "toupper", Conv::Cdecl, 1, [](Cpu& c) -> uint32_t { int v = int(c.arg(0)); return uint32_t(v >= 'a' && v <= 'z' ? v - 32 : v); });
    reg.add("msvcrt.dll", "tolower", Conv::Cdecl, 1, [](Cpu& c) -> uint32_t { int v = int(c.arg(0)); return uint32_t(v >= 'A' && v <= 'Z' ? v + 32 : v); });
    reg.add("msvcrt.dll", "_stricmp", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1));
        for (auto& ch : a) ch = char(tolower((unsigned char)ch));
        for (auto& ch : b) ch = char(tolower((unsigned char)ch));
        int r = a.compare(b); return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    // stdio
    reg.add("msvcrt.dll", "printf", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        std::string s = format_stack(c, str(c.arg(0)), 1); emit(1, s); return uint32_t(s.size()); });
    reg.add("msvcrt.dll", "vprintf", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint32_t ap = c.arg(1);
        std::string s = format_va(str(c.arg(0)), ap); emit(1, s); return uint32_t(s.size()); });
    reg.add("msvcrt.dll", "fprintf", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(0)); if (fi < 0) return 0xFFFFFFFFu;
        std::string s = format_stack(c, str(c.arg(1)), 2); emit(fi, s); return uint32_t(s.size()); });
    reg.add("msvcrt.dll", "vfprintf", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(0)); if (fi < 0) return 0xFFFFFFFFu;
        uint32_t ap = c.arg(2);  // an i386 va_list is a pointer to the first vararg
        std::string s = format_va(str(c.arg(1)), ap); emit(fi, s); return uint32_t(s.size()); });
    // The non-v forms take their varargs on the stack (first one at arg 2 or 3);
    // only the v* forms receive a va_list pointer.
    reg.add("msvcrt.dll", "sprintf", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_stack_to(c, c.arg(1), 2, c.arg(0), -1, true)); });
    reg.add("msvcrt.dll", "_snprintf", Conv::Cdecl, 4, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_stack_to(c, c.arg(2), 3, c.arg(0), int(c.arg(1)), true)); });
    reg.add("msvcrt.dll", "snprintf", Conv::Cdecl, 4, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_stack_to(c, c.arg(2), 3, c.arg(0), int(c.arg(1)), true)); });
    reg.add("msvcrt.dll", "vsprintf", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_va_to(c.arg(1), c.arg(2), c.arg(0), -1, true)); });
    reg.add("msvcrt.dll", "_vsnprintf", Conv::Cdecl, 4, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_va_to(c.arg(2), c.arg(3), c.arg(0), int(c.arg(1)), true)); });
    reg.add("msvcrt.dll", "puts", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); emit(1, s + "\n"); return uint32_t(s.size() + 1); });
    reg.add("msvcrt.dll", "fputs", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(1)); if (fi < 0) return 0xFFFFFFFFu;
        emit(fi, str(c.arg(0))); return 0; });
    reg.add("msvcrt.dll", "putchar", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        emit(1, std::string(1, char(c.arg(0)))); return c.arg(0) & 0xff; });
    reg.add("msvcrt.dll", "fputc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(1)); if (fi < 0) return 0xFFFFFFFFu;
        emit(fi, std::string(1, char(c.arg(0)))); return c.arg(0) & 0xff; });
    reg.add("msvcrt.dll", "fwrite", Conv::Cdecl, 4, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(3)); if (fi < 0) return 0;
        uint32_t sz = c.arg(1), n = c.arg(2), total = sz * n;
        if (total) { auto b = mem.read_bytes(c.arg(0), total); emit(fi, std::string(b.begin(), b.end())); }
        return n; });
    reg.add("msvcrt.dll", "fread", Conv::Cdecl, 4, [this](Cpu& c) -> uint32_t { (void)c; return 0; });
    reg.add("msvcrt.dll", "ferror", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "feof", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("msvcrt.dll", "_fileno", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { int fi = file_index(c.arg(0)); return fi < 0 ? 0xFFFFFFFFu : uint32_t(fi); });
    reg.add("msvcrt.dll", "_isatty", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("msvcrt.dll", "setbuf", Conv::Cdecl, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "_setmode", Conv::Cdecl, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "getenv", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "clock", Conv::Cdecl, 0, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "time", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        uint32_t v = 1760000000u; if (c.arg(0)) mem.w32(c.arg(0), v); return v; });
    reg.add("msvcrt.dll", "qsort", Conv::Cdecl, 4, [](Cpu& c) -> uint32_t {
        uint32_t base = c.arg(0), n = c.arg(1), size = c.arg(2), cmp = c.arg(3);
        if (size == 0 || size > 4096 || uint64_t(n) * size > (1u << 26)) throw Fault(FaultKind::AccessViolation, c.eip, base, "qsort: bad size");
        auto less = [&](uint32_t i, uint32_t j) { return int32_t(c.call(cmp, {base + i * size, base + j * size})) < 0; };
        auto swap = [&](uint32_t i, uint32_t j) { auto a = c.mem.read_bytes(base + i * size, size), b = c.mem.read_bytes(base + j * size, size);
            c.mem.write_bytes(base + i * size, b.data(), size); c.mem.write_bytes(base + j * size, a.data(), size); };
        auto sift = [&](uint32_t root, uint32_t end) {
            for (;;) { uint32_t child = 2 * root + 1; if (child >= end) return;
                if (child + 1 < end && less(child, child + 1)) ++child;
                if (!less(root, child)) return;
                swap(root, child); root = child; } };
        if (n > 1) { for (uint32_t i = n / 2; i-- > 0;) sift(i, n); for (uint32_t e = n - 1; e > 0; --e) { swap(0, e); sift(0, e); } }
        return 0; });
}

}
