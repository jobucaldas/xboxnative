// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit allowlist of Win32/CRT imports the x86 core can serve today, with
// hand-written checked marshallers (every guest pointer goes through Memory, so a
// bad pointer is a Fault, never a host access). Milestone 1 surface only: a
// handful of kernel32 calls and two cdecl msvcrt calls, one of which calls back
// into guest code. Generated thunks for the wider surface come later (see plan).
#pragma once
#include <functional>
#include "x86_pe.h"
#include "x86_gl_generated.h"
namespace x86 {
struct Host {
    enum class NlsOperation { IsValidCodePage, GetCPInfo, MultiByteToWideChar, WideCharToMultiByte,
        GetStringTypeA, GetStringTypeW, GetStringTypeExW, GetLocaleInfoA, GetLocaleInfoW, GetLocaleInfoEx, GetUserDefaultLocaleName,
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
    struct GlCall {
        std::string name;
        std::vector<uintptr_t> args;
        std::vector<uint8_t> input, output;
        uintptr_t result = 0;
    };
    std::string out, err;                         // captured stdout/stderr bytes
    std::function<void(const std::string&)> tee;  // optional live sink
    std::function<NlsResponse(const NlsRequest&)> nls_call;
    std::function<void(FileCall&)> file_call;     // Play's native kernel32 file layer
    std::function<bool(const char*, const std::u16string&)> destructive_file_guard;
    std::function<void(SocketCall&)> socket_call;
    std::function<uint64_t()> tick_count;          // same monotonic millisecond source for the guest tick APIs
    std::function<void(LocaleCall&)> locale_call; // Play's native kernel32 locale formatter
    std::function<void(GlCall&)> gl_call;
    std::function<uintptr_t(uint32_t)> get_dc;
    std::function<bool(uint32_t, uintptr_t)> release_dc;
    std::map<uint32_t, uintptr_t> dc_handles, gl_contexts;
    uint32_t gl_handle_serial = 0xE0000000u;
    uint32_t vertex_array = 0;
    int32_t vertex_size = 0, vertex_stride = 0;
    uint32_t vertex_type = 0;
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
inline void register_gl(Registry& r, Host& h) {
    auto count = [&h](const std::string& name) { ++h.calls[name]; };
    auto alloc_handle = [&h](std::map<uint32_t, uintptr_t>& handles, uintptr_t native) {
        if (!native) return uint32_t(0);
        if (++h.gl_handle_serial < 0xE0000000u) h.gl_handle_serial = 0xE0000000u;
        handles[h.gl_handle_serial] = native;
        return h.gl_handle_serial;
    };
    auto call = [&h](Cpu& c, Host::GlCall& q) {
        if (!h.gl_call) { c.mem.w32(c.fs_base + 0x34, 120); return false; }
        h.gl_call(q); return true;
    };
    auto dc = [&h](uint32_t token) -> uintptr_t {
        auto it = h.dc_handles.find(token); return it == h.dc_handles.end() ? 0 : it->second;
    };
    auto context = [&h](uint32_t token) -> uintptr_t {
        auto it = h.gl_contexts.find(token); return it == h.gl_contexts.end() ? 0 : it->second;
    };
    r.add("user32.dll", "GetDC", Conv::Stdcall, 1, [&h, alloc_handle, count](Cpu& c) -> uint32_t {
        count("GetDC"); uint32_t hwnd = c.arg(0);
        if (hwnd || !h.get_dc) { c.mem.w32(c.fs_base + 0x34, hwnd ? 6 : 120); return 0; }
        return alloc_handle(h.dc_handles, h.get_dc(hwnd));
    });
    r.add("user32.dll", "ReleaseDC", Conv::Stdcall, 2, [&h, count](Cpu& c) -> uint32_t {
        count("ReleaseDC"); uint32_t hwnd = c.arg(0), token = c.arg(1); auto it = h.dc_handles.find(token);
        if (hwnd || it == h.dc_handles.end() || !h.release_dc) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        bool ok = h.release_dc(hwnd, it->second); if (ok) h.dc_handles.erase(it); return ok ? 1u : 0u;
    });
    r.add("gdi32.dll", "ChoosePixelFormat", Conv::Stdcall, 2, [dc, call, count](Cpu& c) -> uint32_t {
        count("ChoosePixelFormat"); Host::GlCall q{}; q.name = "ChoosePixelFormat"; q.args = {c.arg(0), c.arg(1)};
        if (!dc(q.args[0])) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        q.args[0] = dc(uint32_t(q.args[0])); q.input = c.mem.read_bytes(q.args[1], 40); q.args.resize(1);
        return call(c, q) ? uint32_t(q.result) : 0;
    });
    r.add("gdi32.dll", "SetPixelFormat", Conv::Stdcall, 3, [dc, call, count](Cpu& c) -> uint32_t {
        count("SetPixelFormat"); Host::GlCall q{}; q.name = "SetPixelFormat"; q.args = {c.arg(0), c.arg(1), c.arg(2)};
        if (!dc(q.args[0])) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        q.args[0] = dc(uint32_t(q.args[0])); q.input = c.mem.read_bytes(q.args[2], 40); q.args.resize(2);
        return call(c, q) ? uint32_t(q.result) : 0;
    });
    r.add("gdi32.dll", "DescribePixelFormat", Conv::Stdcall, 4, [dc, call, count](Cpu& c) -> uint32_t {
        count("DescribePixelFormat"); Host::GlCall q{}; q.name = "DescribePixelFormat"; q.args = {c.arg(0), c.arg(1), c.arg(2), c.arg(3)};
        if (!dc(q.args[0])) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        q.args[0] = dc(uint32_t(q.args[0])); q.output.resize(std::min<size_t>(size_t(q.args[2]), 40)); q.args.resize(3);
        if (!call(c, q)) return 0;
        if (c.arg(3) && !q.output.empty()) c.mem.write_bytes(c.arg(3), q.output.data(), uint32_t(q.output.size()));
        return uint32_t(q.result);
    });
    r.add("gdi32.dll", "SwapBuffers", Conv::Stdcall, 1, [dc, call, count](Cpu& c) -> uint32_t {
        count("SwapBuffers"); Host::GlCall q{}; q.name = "SwapBuffers"; q.args = {c.arg(0)};
        if (!dc(q.args[0])) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        q.args[0] = dc(uint32_t(q.args[0])); return call(c, q) ? uint32_t(q.result) : 0;
    });
    r.add("opengl32.dll", "wglCreateContext", Conv::Stdcall, 1, [&h, dc, alloc_handle, call, count](Cpu& c) -> uint32_t {
        count("wglCreateContext"); Host::GlCall q{}; q.name = "wglCreateContext"; q.args = {c.arg(0)};
        if (!dc(q.args[0])) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        q.args[0] = dc(uint32_t(q.args[0])); return call(c, q) ? alloc_handle(h.gl_contexts, q.result) : 0;
    });
    r.add("opengl32.dll", "wglDeleteContext", Conv::Stdcall, 1, [&h, context, call, count](Cpu& c) -> uint32_t {
        count("wglDeleteContext"); Host::GlCall q{}; q.name = "wglDeleteContext"; q.args = {c.arg(0)};
        if (!context(q.args[0])) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        q.args[0] = context(uint32_t(q.args[0])); if (!call(c, q)) return 0;
        if (q.result) h.gl_contexts.erase(c.arg(0));
        return uint32_t(q.result);
    });
    r.add("opengl32.dll", "wglMakeCurrent", Conv::Stdcall, 2, [dc, context, call, count](Cpu& c) -> uint32_t {
        count("wglMakeCurrent"); Host::GlCall q{}; q.name = "wglMakeCurrent"; q.args = {c.arg(0), c.arg(1)};
        if (q.args[0] && !dc(q.args[0])) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        if (q.args[1] && !context(q.args[1])) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        q.args[0] = dc(uint32_t(q.args[0])); q.args[1] = context(uint32_t(q.args[1]));
        return call(c, q) ? uint32_t(q.result) : 0;
    });
    r.add("opengl32.dll", "wglGetCurrentContext", Conv::Stdcall, 0, [&h, count](Cpu&) -> uint32_t {
        count("wglGetCurrentContext"); if (!h.gl_call) return 0;
        Host::GlCall q{}; q.name = "wglGetCurrentContext"; h.gl_call(q);
        for (const auto& item : h.gl_contexts) if (item.second == q.result) return item.first;
        return 0;
    });
    // The dynamic proc-address thunk returns only routes explicitly registered here.
    r.add("opengl32.dll", "wglGetProcAddress", Conv::Stdcall, 1, [&r, count](Cpu& c) -> uint32_t {
        count("wglGetProcAddress"); std::string name = c.mem.read_cstr(c.arg(0));
        auto it = r.map.find({"opengl32.dll", Registry::key_sym(name)});
        if (it == r.map.end()) return 0;
        return c.thunks.add(it->second);
    });
    for (const auto& exported : kGlExports) {
        const std::string name = exported.name;
        const unsigned argc = exported.argc;
        r.add("opengl32.dll", name, Conv::Stdcall, argc, [&h, call, count, name, argc](Cpu& c) -> uint32_t {
            count(name); Host::GlCall q{}; q.name = name;
            for (unsigned i = 0; i < argc; ++i) q.args.push_back(c.arg(i));
            if (name == "glVertexPointer") {
                h.vertex_size = int32_t(q.args[0]); h.vertex_type = q.args[1]; h.vertex_stride = int32_t(q.args[2]); h.vertex_array = q.args[3];
                return 0;
            }
            if (name == "glDrawArrays") {
                if (!h.vertex_array) throw Fault(FaultKind::Unsupported, c.eip, 0, "x86 GL buffer-backed arrays are unsupported");
                if (h.vertex_type != 0x1406u || h.vertex_size < 2 || h.vertex_size > 4 || h.vertex_stride < 0 || q.args[2] > (1u << 20)) {
                    throw Fault(FaultKind::Unsupported, c.eip, h.vertex_array, "x86 GL client array format is unsupported");
                }
                const uint32_t width = uint32_t(h.vertex_size) * 4u;
                const uint32_t stride = h.vertex_stride ? uint32_t(h.vertex_stride) : width;
                const uint64_t first = q.args[1], count = q.args[2];
                const uint64_t last = uint64_t(h.vertex_array) + (count ? (first + count - 1) * stride + width : first * stride);
                if (stride < width || last > UINT32_MAX || q.args[2] * width > (1u << 26))
                    throw Fault(FaultKind::AccessViolation, c.eip, h.vertex_array, "x86 GL client array range overflow");
                q.input.reserve(size_t(q.args[2]) * width);
                for (uint32_t i = 0; i < q.args[2]; ++i) {
                    uint32_t address = uint32_t(uint64_t(h.vertex_array) + (first + i) * stride);
                    auto bytes = c.mem.read_bytes(address, width);
                    q.input.insert(q.input.end(), bytes.begin(), bytes.end());
                }
                q.args[1] = 0; q.args.push_back(uint32_t(h.vertex_size)); q.args.push_back(h.vertex_type);
                q.args.push_back(stride);
            }
            uint32_t read_pixels_dst = 0;
            if (name == "glReadPixels") {
                read_pixels_dst = q.args[6];
                if (q.args[2] > 16384 || q.args[3] > 16384 || q.args[4] != 0x1908u || q.args[5] != 0x1401u)
                    throw Fault(FaultKind::Unsupported, c.eip, read_pixels_dst, "x86 GL readback format is unsupported");
                uint64_t bytes = uint64_t(q.args[2]) * q.args[3] * 4u;
                if (bytes > (1u << 26)) throw Fault(FaultKind::AccessViolation, c.eip, read_pixels_dst, "x86 GL readback is too large");
                q.output.resize(size_t(bytes)); q.args[6] = 0;
            }
            if (!call(c, q)) return 0;
            if (!q.output.empty()) c.mem.write_bytes(read_pixels_dst, q.output.data(), uint32_t(q.output.size()));
            return uint32_t(q.result);
        });
    }
    // WGL entry points also participate in GetProcAddress dispatch.
    r.add("opengl32.dll", "wglGetCurrentDC", Conv::Stdcall, 0, [&h, count](Cpu&) -> uint32_t {
        count("wglGetCurrentDC"); if (!h.gl_call) return 0; Host::GlCall q{}; q.name = "wglGetCurrentDC"; h.gl_call(q);
        for (const auto& item : h.dc_handles) if (item.second == q.result) return item.first;
        return 0;
    });
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
