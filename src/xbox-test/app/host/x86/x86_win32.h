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
        struct PointerBuffer { uint32_t guest_address = 0; unsigned arg_slot = 0; bool output = false; std::vector<uint8_t> bytes; };
        std::string name;
        std::vector<uintptr_t> args;
        std::vector<GlArgKind> arg_kinds;
        std::vector<unsigned> arg_slots;
        unsigned short signature = 65535;
        std::vector<PointerBuffer> pointers;
        std::vector<std::vector<uint8_t>> shader_sources;
        std::vector<int32_t> shader_lengths;
        std::vector<uint8_t> input, output;
        uintptr_t result = 0;
        bool gap = false;
    };
    struct WindowCreate {
        std::u16string class_name, title;
        uint32_t ex_style = 0, style = 0, x = 0, y = 0, width = 0, height = 0;
        uintptr_t parent = 0, menu = 0, instance = 0;
    };
    struct GlMap { uint32_t address = 0, size = 0, target = 0; uintptr_t offset = 0; bool writable = false; };
    std::string out, err;                         // captured stdout/stderr bytes
    std::function<void(const std::string&)> tee;  // optional live sink
    std::function<NlsResponse(const NlsRequest&)> nls_call;
    std::function<void(FileCall&)> file_call;     // Play's native kernel32 file layer
    std::function<bool(const char*, const std::u16string&)> destructive_file_guard;
    std::function<void(SocketCall&)> socket_call;
    std::function<uint64_t()> tick_count;          // same monotonic millisecond source for the guest tick APIs
    std::function<void(LocaleCall&)> locale_call; // Play's native kernel32 locale formatter
    std::function<void(GlCall&)> gl_call;
    std::function<bool(const std::u16string&)> register_window_class;
    std::function<bool(int, int&)> system_metrics;
    std::function<uintptr_t(const WindowCreate&)> create_window;
    std::function<bool(uintptr_t)> destroy_window;
    std::function<uintptr_t(uintptr_t)> get_dc;
    std::function<bool(uintptr_t, uintptr_t)> release_dc;
    std::map<uint32_t, uintptr_t> window_handles, dc_handles, gl_contexts;
    std::map<uint32_t, GlMap> gl_maps;
    uint32_t gl_handle_serial = 0xE0000000u, window_handle_serial = 0xD0000000u, gl_map_next = 0x70000000u;
    uint32_t vertex_array = 0, gl_array_buffer = 0, gl_element_array_buffer = 0;
    int32_t vertex_size = 0, vertex_stride = 0;
    uint32_t vertex_type = 0;
    uint32_t unpack_alignment = 4, unpack_row_length = 0, unpack_skip_rows = 0, unpack_skip_pixels = 0;
    bool vertex_buffer_backed = false;
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
    r.add("user32.dll", "GetSystemMetrics", Conv::Stdcall, 1, [&h, count](Cpu& c) -> uint32_t {
        count("GetSystemMetrics");
        int value = 0;
        if (h.system_metrics && h.system_metrics(int32_t(c.arg(0)), value)) return uint32_t(value);
#if defined(_WIN32)
        using Fn = int (WINAPI*)(int);
        static HMODULE user = LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto fn = user ? reinterpret_cast<Fn>(GetProcAddress(user, "GetSystemMetrics")) : nullptr;
        if (fn) return uint32_t(fn(int(c.arg(0))));
#endif
        c.mem.w32(c.fs_base + 0x34, 120);  // ERROR_CALL_NOT_IMPLEMENTED on this partition
        return 0;
    });
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
        bool unsupported = false;
        for (GlArgKind kind : q.arg_kinds) unsupported = unsupported || kind == GlArgKind::Unsupported;
        if (!h.gl_call || unsupported || q.gap) { c.mem.w32(c.fs_base + 0x34, 120); q.gap = true; }
        else h.gl_call(q);
        if (q.gap) {
            const std::string line = "x86 GL gap: " + q.name + " (unsupported host dispatch or marshalling)\n";
            h.err += line;
            if (h.tee) h.tee(line);
        }
        return true;
    };
    auto dc = [&h](uint32_t token) -> uintptr_t {
        auto it = h.dc_handles.find(token); return it == h.dc_handles.end() ? 0 : it->second;
    };
    auto context = [&h](uint32_t token) -> uintptr_t {
        auto it = h.gl_contexts.find(token); return it == h.gl_contexts.end() ? 0 : it->second;
    };
    auto window = [&h](uint32_t token) -> uintptr_t {
        auto it = h.window_handles.find(token); return it == h.window_handles.end() ? 0 : it->second;
    };
    auto read_wstr = [](Cpu& c, uint32_t address) {
        std::u16string value;
        for (uint32_t i = 0; i < 32768; ++i) { uint16_t ch = uint16_t(c.mem.r(address + i * 2, 2)); if (!ch) return value; value.push_back(char16_t(ch)); }
        throw Fault(FaultKind::AccessViolation, c.eip, address, "unterminated guest UTF-16 string");
    };
    auto read_astr = [](Cpu& c, uint32_t address) {
        const std::string bytes = c.mem.read_cstr(address, 32768);
        return std::u16string(bytes.begin(), bytes.end());
    };
    auto map_window = [&h](uintptr_t native) {
        if (!native) return uint32_t(0);
        if (++h.window_handle_serial < 0xD0000000u) h.window_handle_serial = 0xD0000000u;
        h.window_handles[h.window_handle_serial] = native;
        return h.window_handle_serial;
    };
    r.add("user32.dll", "RegisterClassExA", Conv::Stdcall, 1, [&h, count, read_astr](Cpu& c) -> uint32_t {
        count("RegisterClassExA"); uint32_t wc = c.arg(0);
        if (c.mem.r32(wc) < 48 || !h.register_window_class) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }
        const uint32_t name = c.mem.r32(wc + 40);
        if (!name) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }
        return h.register_window_class(read_astr(c, name)) ? 1u : 0u;
    });
    r.add("user32.dll", "CreateWindowExA", Conv::Stdcall, 12, [&h, count, read_astr, map_window, window](Cpu& c) -> uint32_t {
        count("CreateWindowExA");
        if (!h.create_window) { c.mem.w32(c.fs_base + 0x34, 120); return 0; }
        Host::WindowCreate q{};
        q.ex_style = c.arg(0); q.class_name = read_astr(c, c.arg(1)); if (c.arg(2)) q.title = read_astr(c, c.arg(2));
        q.style = c.arg(3); q.x = c.arg(4); q.y = c.arg(5); q.width = c.arg(6); q.height = c.arg(7);
        q.parent = c.arg(8) ? window(c.arg(8)) : 0; q.menu = c.arg(9); q.instance = c.arg(10);
        if ((c.arg(8) && !q.parent) || !q.class_name.size()) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        return map_window(h.create_window(q));
    });
    r.add("user32.dll", "RegisterClassExW", Conv::Stdcall, 1, [&h, count, read_wstr](Cpu& c) -> uint32_t {
        count("RegisterClassExW"); uint32_t wc = c.arg(0);
        if (c.mem.r32(wc) < 48 || !h.register_window_class) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }
        const uint32_t name = c.mem.r32(wc + 40);
        if (!name) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }
        return h.register_window_class(read_wstr(c, name)) ? 1u : 0u;
    });
    r.add("user32.dll", "CreateWindowExW", Conv::Stdcall, 12, [&h, count, read_wstr, map_window, window](Cpu& c) -> uint32_t {
        count("CreateWindowExW");
        if (!h.create_window) { c.mem.w32(c.fs_base + 0x34, 120); return 0; }
        Host::WindowCreate q{};
        q.ex_style = c.arg(0); q.class_name = read_wstr(c, c.arg(1)); if (c.arg(2)) q.title = read_wstr(c, c.arg(2));
        q.style = c.arg(3); q.x = c.arg(4); q.y = c.arg(5); q.width = c.arg(6); q.height = c.arg(7);
        q.parent = c.arg(8) ? window(c.arg(8)) : 0; q.menu = c.arg(9); q.instance = c.arg(10);
        if ((c.arg(8) && !q.parent) || !q.class_name.size()) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        return map_window(h.create_window(q));
    });
    r.add("user32.dll", "DestroyWindow", Conv::Stdcall, 1, [&h, count, window](Cpu& c) -> uint32_t {
        count("DestroyWindow"); uint32_t token = c.arg(0); auto it = h.window_handles.find(token);
        if (it == h.window_handles.end() || !h.destroy_window || !h.destroy_window(it->second)) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        h.window_handles.erase(it); return 1;
    });
    r.add("user32.dll", "GetDC", Conv::Stdcall, 1, [&h, alloc_handle, count, window](Cpu& c) -> uint32_t {
        count("GetDC"); uint32_t token = c.arg(0); uintptr_t native = token ? window(token) : 0;
        if ((token && !native) || !h.get_dc) { c.mem.w32(c.fs_base + 0x34, token ? 6 : 120); return 0; }
        return alloc_handle(h.dc_handles, h.get_dc(native));
    });
    r.add("user32.dll", "ReleaseDC", Conv::Stdcall, 2, [&h, count, window](Cpu& c) -> uint32_t {
        count("ReleaseDC"); uint32_t token = c.arg(0), dc_token = c.arg(1); auto it = h.dc_handles.find(dc_token);
        uintptr_t native = token ? window(token) : 0;
        if ((token && !native) || it == h.dc_handles.end() || !h.release_dc) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        bool ok = h.release_dc(native, it->second); if (ok) h.dc_handles.erase(it); return ok ? 1u : 0u;
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
    for (unsigned export_index = 0; export_index < sizeof(kGlExports) / sizeof(kGlExports[0]); ++export_index) {
        const auto& exported = kGlExports[export_index];
        const unsigned short signature = kGlSignatureIds[export_index];
        const std::string name = exported.name;
        const unsigned argc = exported.argc;
        const unsigned first_arg = exported.first_arg;
        const unsigned arg_count = exported.arg_count;
        r.add("opengl32.dll", name, Conv::Stdcall, argc, [&h, call, count, name, argc, first_arg, arg_count, signature](Cpu& c) -> uint32_t {
            count(name); Host::GlCall q{}; q.name = name; q.signature = signature;
            for (unsigned i = 0; i < argc; ++i) q.args.push_back(c.arg(i));
            for (unsigned i = 0; i < arg_count; ++i) {
                q.arg_kinds.push_back(kGlArgs[first_arg + i].kind);
                q.arg_slots.push_back(kGlArgs[first_arg + i].slots);
            }
            if (name == "glMapBufferRange" || name == "glMapBuffer") {
                const uint32_t target = uint32_t(q.args[0]);
                const uint32_t size = name == "glMapBufferRange" ? uint32_t(q.args[2]) : 0;
                const uint32_t access = uint32_t(q.args[name == "glMapBufferRange" ? 3 : 1]);
                if (h.gl_maps.count(target) || (name == "glMapBufferRange" && (!size || size > (1u << 26)))) {
                    c.mem.w32(c.fs_base + 0x34, 87); return 0;
                }
                if (!call(c, q) || q.gap || q.output.empty() || (size && q.output.size() != size) || q.output.size() > (1u << 26)) return 0;
                uint32_t address = (h.gl_map_next + Memory::PAGE - 1) & ~(Memory::PAGE - 1);
                bool found = false;
                for (unsigned attempt = 0; attempt < 4096; ++attempt) {
                    if (c.mem.free_range(address, uint32_t(q.output.size()))) { found = true; break; }
                    address += uint32_t((q.output.size() + Memory::PAGE - 1) & ~(size_t(Memory::PAGE - 1)));
                    if (address < 0x70000000u || address >= 0xF0000000u) address = 0x70000000u;
                }
                if (!found) return 0;
                const uint32_t map_size = uint32_t(q.output.size());
                c.mem.map(address, map_size, PROT_R | PROT_W);
                c.mem.write_bytes(address, q.output.data(), map_size);
                const uintptr_t offset = name == "glMapBufferRange" ? uintptr_t(q.args[1]) : 0;
                h.gl_maps[target] = {address, map_size, target, offset, name == "glMapBufferRange" ? (access & 0x2u) != 0 : access != 0x88B8u};
                h.gl_map_next = address + uint32_t((map_size + Memory::PAGE - 1) & ~(size_t(Memory::PAGE - 1)));
                return address;
            }
            if (name == "glUnmapBuffer") {
                const uint32_t target = uint32_t(q.args[0]); auto it = h.gl_maps.find(target);
                if (it == h.gl_maps.end()) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }
                Host::GlCall unmap{}; unmap.name = "x86GlUnmapBuffer"; unmap.args = {target, it->second.offset};
                if (it->second.writable) unmap.input = c.mem.read_bytes(it->second.address, it->second.size);
                bool ok = call(c, unmap) && !unmap.gap && unmap.result != 0;
                c.mem.unmap(it->second.address, it->second.size); h.gl_maps.erase(it);
                return ok ? 1u : 0u;
            }
            if (name == "glBindBuffer" && q.args.size() == 2 && (q.args[0] == 0x8892u || q.args[0] == 0x8893u)) {
                if (!call(c, q) || q.gap) return 0;
                if (q.args[0] == 0x8892u) h.gl_array_buffer = uint32_t(q.args[1]);
                else h.gl_element_array_buffer = uint32_t(q.args[1]);
                return uint32_t(q.result);
            }
            if (name == "glVertexPointer") {
                h.vertex_size = int32_t(q.args[0]); h.vertex_type = q.args[1]; h.vertex_stride = int32_t(q.args[2]); h.vertex_array = q.args[3];
                h.vertex_buffer_backed = h.gl_array_buffer != 0;
                if (h.vertex_buffer_backed) call(c, q);
                return 0;
            }
            if (name == "glDrawElements") {
                const uint32_t count = uint32_t(q.args[1]), type = uint32_t(q.args[2]);
                if (h.vertex_buffer_backed && h.gl_element_array_buffer) return call(c, q) ? uint32_t(q.result) : 0;
                if (h.vertex_buffer_backed || h.gl_element_array_buffer) { q.gap = true; call(c, q); return 0; }
                if (!h.vertex_array || h.vertex_type != 0x1406u || h.vertex_size < 2 || h.vertex_size > 4 || h.vertex_stride < 0 || count > (1u << 20))
                    throw Fault(FaultKind::Unsupported, c.eip, uint32_t(q.args[3]), "x86 GL client indexed-array format is unsupported");
                const uint32_t index_width = type == 0x1401u ? 1u : type == 0x1403u ? 2u : type == 0x1405u ? 4u : 0u;
                if (!index_width) throw Fault(FaultKind::Unsupported, c.eip, uint32_t(q.args[3]), "x86 GL index type is unsupported");
                const uint32_t width = uint32_t(h.vertex_size) * 4u, stride = h.vertex_stride ? uint32_t(h.vertex_stride) : width;
                if (stride < width || uint64_t(count) * width > (1u << 26)) throw Fault(FaultKind::AccessViolation, c.eip, uint32_t(q.args[3]), "x86 GL indexed-array range overflow");
                auto indices = c.mem.read_bytes(uint32_t(q.args[3]), count * index_width);
                q.input.reserve(size_t(count) * width);
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t index = 0; for (uint32_t b = 0; b < index_width; ++b) index |= uint32_t(indices[size_t(i) * index_width + b]) << (8 * b);
                    uint64_t address = uint64_t(h.vertex_array) + uint64_t(index) * stride;
                    if (address + width > UINT32_MAX + uint64_t(1)) throw Fault(FaultKind::AccessViolation, c.eip, h.vertex_array, "x86 GL indexed-array address overflow");
                    auto bytes = c.mem.read_bytes(uint32_t(address), width); q.input.insert(q.input.end(), bytes.begin(), bytes.end());
                }
                q.name = "x86GlDrawElementsClient"; q.args = {q.args[0], count, uint32_t(h.vertex_size), h.vertex_type, stride};
                return call(c, q) ? uint32_t(q.result) : 0;
            }
            if (name == "glDrawArrays" && h.vertex_buffer_backed) return call(c, q) ? uint32_t(q.result) : 0;
            if (name == "glDrawArrays") {
                if (!h.vertex_array) throw Fault(FaultKind::Unsupported, c.eip, 0, "x86 GL client array pointer is null");
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
            uint32_t output_dst = 0;
            if (name == "glReadPixels") {
                output_dst = uint32_t(q.args[6]);
                if (q.args[2] > 16384 || q.args[3] > 16384 || q.args[4] != 0x1908u || q.args[5] != 0x1401u)
                    throw Fault(FaultKind::Unsupported, c.eip, output_dst, "x86 GL readback format is unsupported");
                uint64_t bytes = uint64_t(q.args[2]) * q.args[3] * 4u;
                if (bytes > (1u << 26)) throw Fault(FaultKind::AccessViolation, c.eip, output_dst, "x86 GL readback is too large");
                q.output.resize(size_t(bytes)); q.args[6] = 0;
            }
            if (name == "glGetIntegerv" || name == "glGetFloatv") {
                const uint32_t pname = uint32_t(q.args[0]);
                uint32_t count = pname == 0x0BA2u || pname == 0x0C10u || pname == 0x0C23u || pname == 0x0C22u ? 4u :
                    pname == 0x0D3Au ? 2u : pname == 0x0BA6u || pname == 0x0BA7u || pname == 0x0BA8u ? 16u :
                    pname == 0x0D33u || pname == 0x0D36u || pname == 0x0D38u || pname == 0x0D3Bu ? 1u : 0u;
                if (!count || !q.args[1]) { q.gap = true; call(c, q); return 0; }
                output_dst = uint32_t(q.args[1]); q.output.resize(size_t(count) * sizeof(uint32_t)); q.args[1] = 0;
            }
            if (name == "glTexImage2D") {
                const uint32_t dst = uint32_t(q.args[8]);
                const uint32_t width = uint32_t(q.args[3]), height = uint32_t(q.args[4]);
                const uint32_t format = uint32_t(q.args[6]), type = uint32_t(q.args[7]);
                uint32_t components = format == 0x1903u || format == 0x1906u || format == 0x1902u || format == 0x1909u ? 1u :
                    format == 0x8227u || format == 0x190Au ? 2u : format == 0x1907u || format == 0x80E0u ? 3u :
                    format == 0x1908u || format == 0x80E1u ? 4u : 0u;
                uint32_t component_size = type == 0x1400u || type == 0x1401u ? 1u : type == 0x1402u || type == 0x1403u || type == 0x140Bu ? 2u :
                    type == 0x1404u || type == 0x1405u || type == 0x1406u ? 4u : 0u;
                uint32_t packed_size = type == 0x8032u || type == 0x8362u ? 1u :
                    type == 0x8363u || type == 0x8364u || type == 0x8033u || type == 0x8034u || type == 0x8035u || type == 0x8366u ? 2u :
                    type == 0x8036u || type == 0x8037u || type == 0x8038u || type == 0x8368u ? 4u : 0u;
                const uint64_t pixel_bytes = packed_size ? packed_size : uint64_t(components) * component_size;
                const uint32_t row_pixels = h.unpack_row_length ? h.unpack_row_length : width;
                const uint64_t raw_row = uint64_t(row_pixels) * pixel_bytes;
                const uint32_t alignment = h.unpack_alignment;
                if (width > 16384 || height > 16384 || !components || !component_size || row_pixels < width ||
                    (alignment != 1 && alignment != 2 && alignment != 4 && alignment != 8)) {
                    q.gap = true; call(c, q); return 0;
                }
                const uint64_t row_stride = (raw_row + alignment - 1) & ~(uint64_t(alignment) - 1);
                const uint64_t bytes = height ? uint64_t(h.unpack_skip_rows) * row_stride + uint64_t(h.unpack_skip_pixels) * pixel_bytes + uint64_t(height - 1) * row_stride + uint64_t(width) * pixel_bytes : 0;
                if (bytes > (1u << 26)) { q.gap = true; call(c, q); return 0; }
                if (dst) q.input = c.mem.read_bytes(dst, uint32_t(bytes));
                q.args[8] = 0;
            }
            if (name.size() >= 10 && name.compare(0, 9, "glUniform") == 0 && name.find("Matrix") == std::string::npos && name.back() == 'v') {
                const size_t suffix = name.size() - 3;
                const uint32_t components = suffix < name.size() && name[suffix] >= '1' && name[suffix] <= '4' ? uint32_t(name[suffix] - '0') : 0u;
                const uint32_t count = uint32_t(q.args[1]), source = uint32_t(q.args[2]);
                const uint32_t scalar_size = name.find("dv") != std::string::npos || name.find("i64") != std::string::npos ? 8u : 4u;
                uint64_t bytes = uint64_t(count) * components * scalar_size;
                if (!components || (count && !source) || count > (1u << 20) || bytes > (1u << 26)) { q.gap = true; call(c, q); return 0; }
                if (bytes) q.input = c.mem.read_bytes(source, uint32_t(bytes));
                q.args[2] = 0;
            }
            if (name.size() >= 16 && name.compare(0, 9, "glUniform") == 0 && name.find("Matrix") != std::string::npos && name.back() == 'v') {
                const size_t matrix = name.find("Matrix") + 6;
                const uint32_t dimension = matrix < name.size() && name[matrix] >= '2' && name[matrix] <= '4' ? uint32_t(name[matrix] - '0') : 0u;
                const uint32_t count = uint32_t(q.args[1]), source = uint32_t(q.args[3]);
                const uint32_t scalar_size = name.find("dv") != std::string::npos ? 8u : 4u;
                uint64_t bytes = uint64_t(count) * dimension * dimension * scalar_size;
                if (!dimension || (count && !source) || count > (1u << 20) || bytes > (1u << 26)) { q.gap = true; call(c, q); return 0; }
                if (bytes) q.input = c.mem.read_bytes(source, uint32_t(bytes));
                q.args[3] = 0;
            }
            if (name == "glShaderSource") {
                const uint32_t count = uint32_t(q.args[1]), strings = uint32_t(q.args[2]), lengths = uint32_t(q.args[3]);
                if (!count || count > 1024 || !strings || uint64_t(strings) + uint64_t(count) * 4 > UINT32_MAX + uint64_t(1) ||
                    (lengths && uint64_t(lengths) + uint64_t(count) * 4 > UINT32_MAX + uint64_t(1))) { q.gap = true; call(c, q); return 0; }
                uint64_t total = 0; q.shader_sources.reserve(count); if (lengths) q.shader_lengths.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    const uint32_t source = c.mem.r32(strings + i * 4);
                    if (!source) { q.gap = true; call(c, q); return 0; }
                    std::vector<uint8_t> bytes;
                    if (lengths) {
                        const int32_t length = int32_t(c.mem.r32(lengths + i * 4));
                        if (length < 0 || length > (1 << 20)) { q.gap = true; call(c, q); return 0; }
                        bytes = c.mem.read_bytes(source, uint32_t(length)); q.shader_lengths.push_back(length);
                        total += uint32_t(length);
                    } else {
                        const std::string text = c.mem.read_cstr(source, 1u << 20);
                        bytes.assign(text.begin(), text.end()); bytes.push_back(0); total += text.size();
                    }
                    if (total > (1u << 20)) { q.gap = true; call(c, q); return 0; }
                    if (bytes.empty()) bytes.push_back(0);
                    q.shader_sources.push_back(std::move(bytes));
                }
                q.args[2] = q.args[3] = 0;
            }
            const bool custom_pointer = name == "glReadPixels" || name == "glTexImage2D" || name == "glShaderSource" ||
                name == "glGetIntegerv" || name == "glGetFloatv" || name.compare(0, 9, "glUniform") == 0;
            if (!custom_pointer) {
                q.pointers.reserve(arg_count);
                unsigned arg_slot = 0;
                for (unsigned i = 0; i < arg_count; ++i) {
                    const GlArg& metadata = kGlArgs[first_arg + i];
                    if (metadata.kind == GlArgKind::Pointer || metadata.kind == GlArgKind::OutputPointer) {
                        const uint32_t guest_address = uint32_t(q.args[arg_slot]);
                        if (guest_address) {
                            std::string expression = metadata.length;
                            uint64_t elements = 1; size_t position = 0; bool valid = !expression.empty(), first = true; char operation = '*';
                            auto parse_factor = [&](uint64_t& factor) {
                                while (position < expression.size() && expression[position] == ' ') ++position;
                                if (position >= expression.size()) return false;
                                if (expression[position] >= '0' && expression[position] <= '9') {
                                    factor = 0;
                                    while (position < expression.size() && expression[position] >= '0' && expression[position] <= '9') {
                                        unsigned digit = unsigned(expression[position++] - '0');
                                        if (factor > (UINT64_MAX - digit) / 10) return false;
                                        factor = factor * 10 + digit;
                                    }
                                    return true;
                                }
                                size_t start = position;
                                while (position < expression.size() && ((expression[position] >= 'a' && expression[position] <= 'z') ||
                                    (expression[position] >= 'A' && expression[position] <= 'Z') || expression[position] == '_')) ++position;
                                if (start == position) return false;
                                std::string identifier = expression.substr(start, position - start); unsigned length_slot = 0;
                                for (unsigned j = 0; j < arg_count; ++j) {
                                    if (identifier == kGlArgs[first_arg + j].name) { factor = uint32_t(q.args[length_slot]); return true; }
                                    length_slot += kGlArgs[first_arg + j].slots;
                                }
                                return false;
                            };
                            while (valid && position < expression.size()) {
                                uint64_t factor = 0;
                                if (!parse_factor(factor)) { valid = false; break; }
                                if (first) { elements = factor; first = false; }
                                else if (operation == '*') { if (factor && elements > UINT64_MAX / factor) valid = false; else elements *= factor; }
                                else if (!factor || elements % factor) valid = false;
                                else elements /= factor;
                                while (position < expression.size() && expression[position] == ' ') ++position;
                                if (position < expression.size()) {
                                    if (expression[position] != '*' && expression[position] != '/') { valid = false; break; }
                                    operation = expression[position++];
                                }
                            }
                            const uint64_t element_size = kGlArgSizes[first_arg + i];
                            if (element_size && elements > UINT64_MAX / element_size) valid = false;
                            uint64_t bytes = valid ? elements * element_size : 0;
                            if (!valid || bytes > (1u << 26) || bytes > UINT32_MAX) { q.gap = true; call(c, q); return 0; }
                            Host::GlCall::PointerBuffer buffer{}; buffer.guest_address = guest_address; buffer.arg_slot = arg_slot;
                            buffer.output = metadata.kind == GlArgKind::OutputPointer; buffer.bytes.resize(size_t(bytes));
                            if (!buffer.output && bytes) buffer.bytes = c.mem.read_bytes(guest_address, uint32_t(bytes));
                            q.pointers.push_back(std::move(buffer));
                            q.args[arg_slot] = q.pointers.back().bytes.empty() ? 0 : reinterpret_cast<uintptr_t>(q.pointers.back().bytes.data());
                        } else if (metadata.kind == GlArgKind::OutputPointer && !metadata.length[0]) {
                            q.gap = true; call(c, q); return 0;
                        }
                    }
                    arg_slot += metadata.slots;
                }
            }
            if (name == "glPixelStorei") {
                if (!call(c, q) || q.gap) return 0;
                const uint32_t pname = uint32_t(q.args[0]), value = uint32_t(q.args[1]);
                if (pname == 0x0CF5u && (value == 1 || value == 2 || value == 4 || value == 8)) h.unpack_alignment = value;
                else if (pname == 0x0CF2u && value <= (1u << 20)) h.unpack_row_length = value;
                else if (pname == 0x0CF3u && value <= (1u << 20)) h.unpack_skip_rows = value;
                else if (pname == 0x0CF4u && value <= (1u << 20)) h.unpack_skip_pixels = value;
                return uint32_t(q.result);
            }
            if (!call(c, q)) return 0;
            if (!q.gap && !q.output.empty() && output_dst) c.mem.write_bytes(output_dst, q.output.data(), uint32_t(q.output.size()));
            if (!q.gap) for (const auto& pointer_buffer : q.pointers) if (pointer_buffer.output && !pointer_buffer.bytes.empty())
                c.mem.write_bytes(pointer_buffer.guest_address, pointer_buffer.bytes.data(), uint32_t(pointer_buffer.bytes.size()));
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
