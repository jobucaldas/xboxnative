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
    enum class NlsOperation { IsValidCodePage, GetCPInfo, MultiByteToWideChar, WideCharToMultiByte,
        GetStringTypeA, GetStringTypeW, GetStringTypeExW, GetLocaleInfoA, GetLocaleInfoW, GetLocaleInfoEx,
        LCMapStringW, LCMapStringEx, CompareStringW, CompareStringEx, FoldStringW };
    struct NlsRequest {
        NlsOperation operation;
        uint32_t code_page = 0, flags = 0, output_capacity = 0, locale = 0, type = 0;
        int32_t input_length = 0, input2_length = 0;
        bool input_null = false, input2_null = false, locale_name_null = false, output_null = true, wants_used_default = false;
        std::u16string locale_name;
        std::vector<uint8_t> input, input2, default_char;
    };
    struct NlsResponse {
        uint32_t result = 0, last_error = 0;
        bool used_default = false;
        std::vector<uint8_t> output;
    };
    struct SocketCall {
        enum class Kind { Startup, Cleanup, LastError } kind;
        uint16_t version = 0;
        std::vector<uint8_t> data;
        int result = 0;
    };
    struct FileCall {
        enum class Kind { CreateA, CreateW, Close, Read, Write, Size, Time, AttributesA, Seek, SeekEx, End, TimeConvert, TimeLocal, CreateDirectoryW, RemoveDirectoryA, RemoveDirectoryW, GetCurrentDirectoryA, GetCurrentDirectoryW, SetCurrentDirectoryA, SetCurrentDirectoryW, FindFirstA, FindFirstW, FindFirstExA, FindFirstExW, FindNextA, FindNextW, FindClose } kind;
        enum class Operation { None, CreateDirectoryA, DeleteA, DeleteW, SystemToFileTime, SystemToTzLocal } operation = Operation::None;
        std::string path_a; std::u16string path_w;
        uint64_t handle = 0, template_handle = 0, value = 0; int64_t distance = 0, position = 0; uint32_t method = 0, args[5]{}, error = 0; bool succeeded = false;
        std::vector<uint8_t> data;
        explicit FileCall(Kind k) : kind(k) {}
    };
    struct LocaleCall {
        bool date = false, has_system_time = false, has_format = false, has_output_buffer = false;
        uint32_t locale = 0, flags = 0;
        uint16_t system_time[8]{};
        std::u16string format;
        int32_t buffer_chars = 0, result = 0;
        uint32_t error = 0;
        std::vector<uint8_t> data;
        bool succeeded = false;
    };
    std::string out, err;                         // captured stdout/stderr bytes
    std::function<void(const std::string&)> tee;  // optional live sink
    std::function<NlsResponse(const NlsRequest&)> nls_call;
    std::function<void(FileCall&)> file_call;     // Play's native kernel32 file layer
    std::function<bool(const char*, const std::u16string&)> destructive_file_guard;
    std::function<void(SocketCall&)> socket_call;
    std::function<uint64_t()> tick_count;          // same monotonic millisecond source for the guest tick APIs
    std::function<void(LocaleCall&)> locale_call; // Play's native kernel32 locale formatter
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
inline void register_ws2(Registry& r, Host& h) {
    auto startup=[&h](Cpu& c)->uint32_t {
        Host::SocketCall q{};q.kind=Host::SocketCall::Kind::Startup;q.version=uint16_t(c.arg(0));
        if(!h.socket_call){c.mem.w32(c.fs_base+0x34,10093);return 10093;}h.socket_call(q);
        if(q.result==0){if(q.data.size()!=400)throw Fault(FaultKind::AccessViolation,c.eip,c.arg(1),"WSAStartup host returned malformed WSADATA");c.mem.write_bytes(c.arg(1),q.data.data(),400);}
        return uint32_t(q.result);
    };
    auto cleanup=[&h](Cpu&)->uint32_t{Host::SocketCall q{};q.kind=Host::SocketCall::Kind::Cleanup;if(h.socket_call)h.socket_call(q);return uint32_t(q.result);};
    auto last_error=[&h](Cpu&)->uint32_t{Host::SocketCall q{};q.kind=Host::SocketCall::Kind::LastError;if(h.socket_call)h.socket_call(q);return uint32_t(q.result);};
    r.add("ws2_32.dll","WSAStartup",Conv::Stdcall,2,startup);r.add("ws2_32.dll","#115",Conv::Stdcall,2,startup);
    r.add("ws2_32.dll","WSACleanup",Conv::Stdcall,0,cleanup);r.add("ws2_32.dll","#116",Conv::Stdcall,0,cleanup);
    r.add("ws2_32.dll","WSAGetLastError",Conv::Stdcall,0,last_error);r.add("ws2_32.dll","#111",Conv::Stdcall,0,last_error);
}
}
