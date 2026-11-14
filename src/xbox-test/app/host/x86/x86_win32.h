// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit allowlist of Win32/CRT imports the x86 core can serve today, with
// hand-written checked marshallers (every guest pointer goes through Memory, so a
// bad pointer is a Fault, never a host access). Milestone 1 surface only: a
// handful of kernel32 calls and two cdecl msvcrt calls, one of which calls back
// into guest code. Generated thunks for the wider surface come later (see plan).
#pragma once
#include <functional>
#include "x86_pe.h"
namespace x86 {
struct Host {
    struct FileCall {
        enum class Kind { CreateA, CreateW, Close, Read, Size, Time, AttributesA, Seek, End, TimeConvert, TimeLocal } kind;
        std::string path_a; std::u16string path_w;
        uint64_t handle = 0, template_handle = 0, value = 0; uint32_t args[5]{}, error = 0; bool succeeded = false;
        std::vector<uint8_t> data;
        explicit FileCall(Kind k) : kind(k) {}
    };
    std::string out, err;                         // captured stdout/stderr bytes
    std::function<void(const std::string&)> tee;  // optional live sink
    std::function<void(FileCall&)> file_call;     // Play's native kernel32 file layer
    std::map<std::string, unsigned> calls;        // import -> call count
    static constexpr uint32_t H_STDIN = 3, H_STDOUT = 7, H_STDERR = 11;
};
inline void register_win32(Registry& r, Host& h) {
    auto count = [&h](const char* n) { ++h.calls[n]; };
    r.add("kernel32.dll", "GetStdHandle", Conv::Stdcall, 1, [&h, count](Cpu& c) -> uint32_t {
        count("GetStdHandle"); switch (int32_t(c.arg(0))) { case -10: return Host::H_STDIN; case -11: return Host::H_STDOUT; case -12: return Host::H_STDERR; }
        return 0xFFFFFFFFu; });
    r.add("kernel32.dll", "WriteFile", Conv::Stdcall, 5, [&h, count](Cpu& c) -> uint32_t {
        count("WriteFile"); uint32_t handle = c.arg(0), buf = c.arg(1), n = c.arg(2), written = c.arg(3), ovl = c.arg(4);
        if (ovl || (handle != Host::H_STDOUT && handle != Host::H_STDERR) || n > (1u << 24)) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }  // ERROR_INVALID_PARAMETER
        auto bytes = c.mem.read_bytes(buf, n); std::string s(bytes.begin(), bytes.end());
        (handle == Host::H_STDOUT ? h.out : h.err) += s; if (h.tee) h.tee(s);
        if (written) c.mem.w32(written, n);
        return 1; });
    r.add("kernel32.dll", "SetLastError", Conv::Stdcall, 1, [count](Cpu& c) -> uint32_t { count("SetLastError"); c.mem.w32(c.fs_base + 0x34, c.arg(0)); return 0; });
    r.add("kernel32.dll", "GetLastError", Conv::Stdcall, 0, [count](Cpu& c) -> uint32_t { count("GetLastError"); return c.mem.r32(c.fs_base + 0x34); });
    r.add("kernel32.dll", "ExitProcess", Conv::Stdcall, 1, [count](Cpu& c) -> uint32_t { count("ExitProcess"); c.exited = true; c.exit_code = c.arg(0); return 0; });
    r.add("msvcrt.dll", "strlen", Conv::Cdecl, 1, [count](Cpu& c) -> uint32_t { count("strlen"); return uint32_t(c.mem.read_cstr(c.arg(0)).size()); });
    r.add("msvcrt.dll", "qsort", Conv::Cdecl, 4, [count](Cpu& c) -> uint32_t {
        count("qsort"); uint32_t base = c.arg(0), n = c.arg(1), size = c.arg(2), cmp = c.arg(3);
        if (size == 0 || size > 4096 || uint64_t(n) * size > (1u << 26)) throw Fault(FaultKind::AccessViolation, c.eip, base, "qsort: bad size");
        // In-place heapsort so the guest comparator sees guest pointers to the real elements (like the CRT).
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
