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
#include "x86com.h"
namespace x86 {
struct Host {
    // RECT/POINT marshalled between guest i686 and the host record layer: both
    // are four/two 32-bit LONGs on every Windows ABI, so the field list is shared.
    struct UserRect { uint32_t left = 0, top = 0, right = 0, bottom = 0; };
    struct UserPoint { uint32_t x = 0, y = 0; };
    // One request shape for the record-backed user32 surface (uwph-u32c.dll):
    // window geometry/state/text, monitors, keyboard/cursor/icon state and the
    // posted-message queue. The guest's HWND/HCURSOR are interpreter tokens;
    // `window`/`handle`/`result` carry the host-native values across the
    // callback and the route maps them through its token tables. The shim
    // always relays GetLastError() into `error`; each route decides when the
    // guest's TEB may see it (failure paths only, as the API contract reads).
    struct UserCall {
        enum class Kind {
            GetClientRect, GetWindowRect, AdjustWindowRect, AdjustWindowRectEx, SetWindowPos, MoveWindow,
            ClientToScreen, ScreenToClient, MapWindowPoints,
            IsWindow, IsIconic, IsZoomed, IsWindowVisible, IsWindowEnabled, EnableWindow, ShowWindow,
            GetParent, GetAncestor, GetTopWindow, GetWindow, GetDesktopWindow,
            GetWindowLong, SetWindowLong, GetWindowLongPtr, SetWindowLongPtr,
            GetWindowTextLength, GetWindowText, SetWindowText, IsWindowUnicode,
            GetFocus, SetFocus, GetActiveWindow, SetActiveWindow,
            GetForegroundWindow, SetForegroundWindow, SetCapture, ReleaseCapture,
            GetCursorPos, SetCursorPos, ClipCursor, GetClipCursor, SetCursor, ShowCursor,
            GetKeyState, GetAsyncKeyState, GetKeyboardState, MapVirtualKey, ToUnicode,
            LoadImage, DestroyIcon, GetIconInfo, CopyImage,
            PeekMessage, GetMessage, PostMessage, PostThreadMessage, PostQuitMessage,
            DispatchTimer, TranslateMessage, SetTimer, KillTimer, GetQueueStatus, GetMessageTime, GetMessagePos,
            SendMessage, DefWindowProc,
            MonitorFromWindow, MonitorFromRect, MonitorFromPoint, GetMonitorInfo, SystemParametersInfo, GetWindowPlacement,
        };
        Kind kind = Kind::GetClientRect;
        bool wide = false, ok = false;              // wide: use the W record export; ok: the record call answered success
        uint32_t action = 0, flags = 0, error = 0;  // action: SPI_*/map flags; error: record-thread GetLastError relay
        int32_t x = 0, y = 0, width = 0, height = 0, index = 0;
        uint32_t value = 0;                          // EnableWindow state, ShowWindow command, keyboard vk, timer period
        uintptr_t window = 0, window2 = 0;           // token-mapped native HWNDs
        uintptr_t handle = 0;                        // native HCURSOR/HICON/HMONITOR
        uintptr_t result = 0;                        // LRESULT/LONG_PTR/HANDLE answer, already 32-bit meaningful
        uintptr_t id = 0;                            // SetTimer/KillTimer/PostThreadMessage identifiers
        uintptr_t name_id = 0;                       // LoadImage INTRESOURCE when name is empty
        UserRect rect{}, rect2{}, rect_out{};        // in/out geometry (rect2: the second operand)
        UserPoint point{}, point2{};                 // POINT value/in-out and the second MapWindowPoints anchor
        std::u16string text;                         // window title / message text, UTF-16 on both sides
        std::string name;                            // LoadImage resource name, narrow-normalized
        std::vector<uint8_t> bytes;                  // GetKeyboardState(256)/ToUnicode state/ANSI window text
    };
    std::function<void(UserCall&)> user_call;
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
    // The record layer's message queue is per-thread on the host, and the guest
    // threads (see x86_guest.h's scheduler) each own a TEB — one host thread id
    // (the launcher's GetCurrentThreadId) would fold them together. The id is
    // whatever the configured shim keys queues by: the guest's own id.
    std::function<void(uint32_t)> set_message_thread;
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
    std::map<uint32_t, uintptr_t> window_handles, dc_handles, gl_contexts, icon_handles;
    std::map<uint32_t, GlMap> gl_maps;
    uint32_t gl_handle_serial = 0xE0000000u, window_handle_serial = 0xD0000000u, gl_map_next = 0x70000000u;
    uint32_t vertex_array = 0, gl_array_buffer = 0, gl_element_array_buffer = 0;
    std::function<uintptr_t(bool, uint32_t, uintptr_t*)> d3d9_factory;
    std::function<uintptr_t(uint32_t)> d3d9_window;
    ComSpace com;
    uint32_t d3d9_guest_top = 0x7D000000u;
    std::map<uint32_t, std::tuple<void*, uint32_t, uint32_t>> d3d9_locks;
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
    auto put_error = [](Cpu& c, uint32_t code) { c.mem.w32(c.fs_base + 0x34, code); };
    // The record exports live in a DLL whose thread owns its own GetLastError;
    // the shim copies it into UserCall::error after each call. Routes relay it
    // onto the guest TEB only on failure paths, then force success paths clean.
    auto relay = [&put_error](Cpu& c, const Host::UserCall& q) { if (!q.ok) put_error(c, q.error ? q.error : 6u); else put_error(c, 0); };
    auto map_rect = [](Host::UserRect& dst, uint32_t src, Cpu& c) {
        dst = {c.mem.r32(src), c.mem.r32(src + 4), c.mem.r32(src + 8), c.mem.r32(src + 12)};
    };
    auto write_rect = [](Cpu& c, uint32_t dst, const Host::UserRect& r) {
        c.mem.w32(dst, r.left); c.mem.w32(dst + 4, r.top); c.mem.w32(dst + 8, r.right); c.mem.w32(dst + 12, r.bottom);
    };
    auto read_point = [](Cpu& c, uint32_t src) { return Host::UserPoint{c.mem.r32(src), c.mem.r32(src + 4)}; };
    auto write_point = [](Cpu& c, uint32_t dst, const Host::UserPoint& p) { c.mem.w32(dst, p.x); c.mem.w32(dst + 4, p.y); };
    auto call_user = [&h, &put_error, &relay](Cpu& c, Host::UserCall& q) -> bool {
        if (!h.user_call) { put_error(c, 120); return false; }  // ERROR_CALL_NOT_IMPLEMENTED: no record layer configured
        h.user_call(q);
        relay(c, q);
        return q.ok;
    };
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
    // Record answers are native HWND/HCURSOR/HICON/HMONITOR values; routes hand
    // guests the token that already maps to them (0 when the record made none).
    auto token_for = [&h](uintptr_t native) -> uint32_t {
        for (const auto& item : h.window_handles) if (item.second == native) return item.first;
        return 0;
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

    // ---- user32 record-backed routes (uwph-u32c.dll via UserCall). Stdcall
    // arity is the guest i686 one: one slot per 32-bit argument. Text travels
    // as UTF-16 either way; ANSI exports normalize to the narrow resource names
    // the record table keys (see c_LoadImageA/window_tree.h).

    // ---- Geometry and placement -------------------------------------------
    // GetClientRect(hwnd, RECT*): {0,0,cw,ch}, FALSE + ERROR 6 on a bad hwnd.
    r.add("user32.dll", "GetClientRect", Conv::Stdcall, 2, [&h, count, put_error, call_user, write_rect](Cpu& c) -> uint32_t {
        count("GetClientRect"); uint32_t token = c.arg(0), out = c.arg(1);
        if (!out) { put_error(c, 87); return 0; }
        auto it = h.window_handles.find(token);
        if (token && it == h.window_handles.end()) { put_error(c, 6); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetClientRect; q.window = it == h.window_handles.end() ? 0 : it->second;
        if (!call_user(c, q)) return 0;
        write_rect(c, out, q.rect_out); return 1;
    });
    // GetWindowRect(hwnd, RECT*): screen-space frame including the nonclient area.
    r.add("user32.dll", "GetWindowRect", Conv::Stdcall, 2, [&h, count, put_error, call_user, write_rect](Cpu& c) -> uint32_t {
        count("GetWindowRect"); uint32_t token = c.arg(0), out = c.arg(1);
        auto it = h.window_handles.find(token);
        if (!out || (token && it == h.window_handles.end())) { put_error(c, out ? 6 : 87); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetWindowRect; q.window = token ? it->second : 0;
        if (!call_user(c, q)) return 0;
        write_rect(c, out, q.rect_out); return 1;
    });
    // AdjustWindowRect(Ex)(lpRect, style, menu[, ex]): inflates the caller's
    // client rect in place to the window rect the style implies.
    for (bool ex : {false, true}) r.add("user32.dll", ex ? "AdjustWindowRectEx" : "AdjustWindowRect", Conv::Stdcall, ex ? 4u : 3u,
        [put_error, map_rect, write_rect, call_user, count, ex](Cpu& c) -> uint32_t {
            count(ex ? "AdjustWindowRectEx" : "AdjustWindowRect"); uint32_t rect = c.arg(0);
            if (!rect) { put_error(c, 87); return 0; }
            Host::UserCall q{}; q.kind = ex ? Host::UserCall::Kind::AdjustWindowRectEx : Host::UserCall::Kind::AdjustWindowRect;
            map_rect(q.rect, rect, c);
            q.value = c.arg(2); q.x = int32_t(c.arg(1)); q.flags = ex ? c.arg(3) : 0;
            if (!call_user(c, q)) return 0;
            write_rect(c, rect, q.rect_out);
            return 1;
        });
    // SetWindowPos(hwnd, after, x, y, cx, cy, flags): the record layer tracks no
    // z-order; the shim answers move/size/show and drives WM_SIZE/WM_MOVE.
    r.add("user32.dll", "SetWindowPos", Conv::Stdcall, 7, [&h, count, put_error, call_user](Cpu& c) -> uint32_t {
        count("SetWindowPos"); uint32_t token = c.arg(0);
        auto it = h.window_handles.find(token);
        if (it == h.window_handles.end()) { put_error(c, 6); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::SetWindowPos; q.window = it->second;
        auto after = h.window_handles.find(c.arg(1)); q.window2 = after == h.window_handles.end() ? 0 : after->second;
        q.x = int32_t(c.arg(2)); q.y = int32_t(c.arg(3)); q.width = int32_t(c.arg(4)); q.height = int32_t(c.arg(5));
        q.value = c.arg(6); return call_user(c, q) ? 1u : 0u;
    });
    // MoveWindow(hwnd, x, y, cx, cy, repaint).
    r.add("user32.dll", "MoveWindow", Conv::Stdcall, 6, [&h, count, put_error, call_user](Cpu& c) -> uint32_t {
        count("MoveWindow"); uint32_t token = c.arg(0);
        auto it = h.window_handles.find(token);
        if (it == h.window_handles.end()) { put_error(c, 6); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::MoveWindow; q.window = it->second;
        q.x = int32_t(c.arg(1)); q.y = int32_t(c.arg(2)); q.width = int32_t(c.arg(3)); q.height = int32_t(c.arg(4));
        return call_user(c, q) ? 1u : 0u;
    });
    // ClientToScreen/ScreenToClient(hwnd, POINT*): in-out point.
    for (bool to_screen : {false, true}) r.add("user32.dll", to_screen ? "ClientToScreen" : "ScreenToClient", Conv::Stdcall, 2,
        [&h, count, put_error, call_user, read_point, write_point, to_screen](Cpu& c) -> uint32_t {
            count(to_screen ? "ClientToScreen" : "ScreenToClient"); uint32_t token = c.arg(0), pt = c.arg(1);
            auto it = h.window_handles.find(token);
            if (!pt || (token && it == h.window_handles.end())) { put_error(c, pt ? 6 : 87); return 0; }
            Host::UserCall q{}; q.kind = to_screen ? Host::UserCall::Kind::ClientToScreen : Host::UserCall::Kind::ScreenToClient;
            q.window = token ? it->second : 0; q.point = read_point(c, pt);
            if (!call_user(c, q)) return 0;
            write_point(c, pt, q.point); return 1;
        });
    // MapWindowPoints(from, to, POINT*, count): returns MAKELONG(dx, dy).
    r.add("user32.dll", "MapWindowPoints", Conv::Stdcall, 4, [&h, count, put_error, call_user, read_point, write_point](Cpu& c) -> uint32_t {
        count("MapWindowPoints"); uint32_t from = c.arg(0), to = c.arg(1), pts = c.arg(2), n = c.arg(3);
        auto f = h.window_handles.find(from), t = h.window_handles.find(to);
        if ((from && f == h.window_handles.end()) || (to && t == h.window_handles.end()) || (n && !pts) || n > 4096) { put_error(c, 6); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::MapWindowPoints;
        q.window = from ? f->second : 0; q.window2 = to ? t->second : 0; q.value = n;
        for (uint32_t i = 0; i < n; ++i) {
            Host::UserPoint p = read_point(c, pts + i * 8);
            q.bytes.push_back(p.x & 0xff); q.bytes.push_back((p.x >> 8) & 0xff); q.bytes.push_back((p.x >> 16) & 0xff); q.bytes.push_back(p.x >> 24);
            q.bytes.push_back(p.y & 0xff); q.bytes.push_back((p.y >> 8) & 0xff); q.bytes.push_back((p.y >> 16) & 0xff); q.bytes.push_back(p.y >> 24);
        }
        if (!call_user(c, q)) return 0;
        for (uint32_t i = 0; i < n; ++i) write_point(c, pts + i * 8, Host::UserPoint{
            uint32_t(q.bytes[i * 8]) | uint32_t(q.bytes[i * 8 + 1]) << 8 | uint32_t(q.bytes[i * 8 + 2]) << 16 | uint32_t(q.bytes[i * 8 + 3]) << 24,
            uint32_t(q.bytes[i * 8 + 4]) | uint32_t(q.bytes[i * 8 + 5]) << 8 | uint32_t(q.bytes[i * 8 + 6]) << 16 | uint32_t(q.bytes[i * 8 + 7]) << 24});
        return q.result;
    });

    // ---- Rectangle helpers: pure computation, no record state --------------
    // SetRect(RECT*, l, t, r, b) / SetRectEmpty(RECT*) / CopyRect(dst, src).
    r.add("user32.dll", "SetRect", Conv::Stdcall, 5, [put_error, write_rect, count](Cpu& c) -> uint32_t {
        count("SetRect"); uint32_t dst = c.arg(0); if (!dst) { put_error(c, 87); return 0; }
        write_rect(c, dst, Host::UserRect{c.arg(1), c.arg(2), c.arg(3), c.arg(4)}); return 1;
    });
    r.add("user32.dll", "SetRectEmpty", Conv::Stdcall, 1, [put_error, write_rect, count](Cpu& c) -> uint32_t {
        count("SetRectEmpty"); uint32_t dst = c.arg(0); if (!dst) { put_error(c, 87); return 0; }
        write_rect(c, dst, Host::UserRect{}); return 1;
    });
    r.add("user32.dll", "CopyRect", Conv::Stdcall, 2, [put_error, write_rect, count](Cpu& c) -> uint32_t {
        count("CopyRect"); uint32_t dst = c.arg(0), src = c.arg(1);
        if (!dst || !src) { put_error(c, 87); return 0; }
        write_rect(c, dst, Host::UserRect{c.mem.r32(src), c.mem.r32(src + 4), c.mem.r32(src + 8), c.mem.r32(src + 12)}); return 1;
    });
    r.add("user32.dll", "OffsetRect", Conv::Stdcall, 3, [put_error, write_rect, count](Cpu& c) -> uint32_t {
        count("OffsetRect"); uint32_t dst = c.arg(0); if (!dst) { put_error(c, 87); return 0; }
        Host::UserRect r{c.mem.r32(dst), c.mem.r32(dst + 4), c.mem.r32(dst + 8), c.mem.r32(dst + 12)};
        int32_t dx = int32_t(c.arg(1)), dy = int32_t(c.arg(2));
        write_rect(c, dst, Host::UserRect{uint32_t(int32_t(r.left) + dx), uint32_t(int32_t(r.top) + dy),
            uint32_t(int32_t(r.right) + dx), uint32_t(int32_t(r.bottom) + dy)}); return 1;
    });
    r.add("user32.dll", "InflateRect", Conv::Stdcall, 3, [put_error, write_rect, count](Cpu& c) -> uint32_t {
        count("InflateRect"); uint32_t dst = c.arg(0); if (!dst) { put_error(c, 87); return 0; }
        Host::UserRect r{c.mem.r32(dst), c.mem.r32(dst + 4), c.mem.r32(dst + 8), c.mem.r32(dst + 12)};
        int32_t dx = int32_t(c.arg(1)), dy = int32_t(c.arg(2));
        write_rect(c, dst, Host::UserRect{uint32_t(int32_t(r.left) - dx), uint32_t(int32_t(r.top) - dy),
            uint32_t(int32_t(r.right) + dx), uint32_t(int32_t(r.bottom) + dy)}); return 1;
    });
    // UnionRect/IntersectRect(dst, a, b): Wine's empty-set rules; FALSE marks an
    // empty result with dst zeroed.
    for (bool united : {false, true}) r.add("user32.dll", united ? "UnionRect" : "IntersectRect", Conv::Stdcall, 3,
        [put_error, write_rect, count, united](Cpu& c) -> uint32_t {
            count(united ? "UnionRect" : "IntersectRect"); uint32_t dst = c.arg(0), a = c.arg(1), b = c.arg(2);
            if (!dst || !a || !b) { put_error(c, 87); return 0; }
            auto ra = Host::UserRect{c.mem.r32(a), c.mem.r32(a + 4), c.mem.r32(a + 8), c.mem.r32(a + 12)};
            auto rb = Host::UserRect{c.mem.r32(b), c.mem.r32(b + 4), c.mem.r32(b + 8), c.mem.r32(b + 12)};
            auto empty = [](const Host::UserRect& r) { return r.left >= r.right || r.top >= r.bottom; };
            if (!united) {
                if (empty(ra) || empty(rb) || ra.left >= rb.right || rb.left >= ra.right || ra.top >= rb.bottom || rb.top >= ra.bottom) {
                    write_rect(c, dst, Host::UserRect{}); return 0;
                }
                write_rect(c, dst, Host::UserRect{std::max(ra.left, rb.left), std::max(ra.top, rb.top), std::min(ra.right, rb.right), std::min(ra.bottom, rb.bottom)});
                return 1;
            }
            if (empty(ra) && empty(rb)) { write_rect(c, dst, Host::UserRect{}); return 0; }
            if (empty(ra)) { write_rect(c, dst, rb); return empty(rb) ? 0u : 1u; }
            if (empty(rb)) { write_rect(c, dst, ra); return 1; }
            write_rect(c, dst, Host::UserRect{std::min(ra.left, rb.left), std::min(ra.top, rb.top), std::max(ra.right, rb.right), std::max(ra.bottom, rb.bottom)});
            return empty(Host::UserRect{std::min(ra.left, rb.left), std::min(ra.top, rb.top), std::max(ra.right, rb.right), std::max(ra.bottom, rb.bottom)}) ? 0u : 1u;
        });
    r.add("user32.dll", "IsRectEmpty", Conv::Stdcall, 1, [count](Cpu& c) -> uint32_t {
        count("IsRectEmpty"); uint32_t src = c.arg(0);
        if (!src) return 1;  // NULL: Windows answers TRUE (documented).
        Host::UserRect r{c.mem.r32(src), c.mem.r32(src + 4), c.mem.r32(src + 8), c.mem.r32(src + 12)};
        return r.left >= r.right || r.top >= r.bottom ? 1u : 0u;
    });
    // PtInRect(rect, POINT): POINT is passed by value on i686 (two stack slots).
    // The left/top edges are inside, right/bottom are not; NULL rect answers
    // FALSE without an error.
    r.add("user32.dll", "PtInRect", Conv::Stdcall, 3, [count](Cpu& c) -> uint32_t {
        count("PtInRect"); uint32_t src = c.arg(0);
        if (!src) return 0;
        Host::UserRect r{c.mem.r32(src), c.mem.r32(src + 4), c.mem.r32(src + 8), c.mem.r32(src + 12)};
        Host::UserPoint p{c.arg(1), c.arg(2)};
        return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom ? 1u : 0u;
    });
    r.add("user32.dll", "EqualRect", Conv::Stdcall, 2, [count](Cpu& c) -> uint32_t {
        count("EqualRect"); uint32_t a = c.arg(0), b = c.arg(1);
        if (!a || !b) return 0;
        return c.mem.r32(a) == c.mem.r32(b) && c.mem.r32(a + 4) == c.mem.r32(b + 4) &&
               c.mem.r32(a + 8) == c.mem.r32(b + 8) && c.mem.r32(a + 12) == c.mem.r32(b + 12) ? 1u : 0u;
    });

    // ---- Window state queries ----------------------------------------------
    auto state_query = [&](const char* name, Host::UserCall::Kind kind, unsigned argc) {
        r.add("user32.dll", name, Conv::Stdcall, argc, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t token = c.arg(0);
            auto it = h.window_handles.find(token);
            if (it == h.window_handles.end()) { put_error(c, 0); return 0; }  // FALSE, no last-error change (record contract)
            Host::UserCall q{}; q.kind = kind; q.window = it->second; q.ok = true;
            call_user(c, q); return q.result ? 1u : 0u;
        });
    };
    state_query("IsIconic", Host::UserCall::Kind::IsIconic, 1);
    state_query("IsZoomed", Host::UserCall::Kind::IsZoomed, 1);
    r.add("user32.dll", "IsWindow", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("IsWindow"); return h.window_handles.count(c.arg(0)) ? 1u : 0u;
    });
    auto window_bool = [&](const char* name, Host::UserCall::Kind kind) {
        r.add("user32.dll", name, Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t token = c.arg(0);
            auto it = h.window_handles.find(token);
            if (it == h.window_handles.end()) { put_error(c, 6); return 0; }
            Host::UserCall q{}; q.kind = kind; q.window = it->second;
            if (!call_user(c, q)) return 0;
            return q.result ? 1u : 0u;
        });
    };
    window_bool("IsWindowVisible", Host::UserCall::Kind::IsWindowVisible);
    window_bool("IsWindowEnabled", Host::UserCall::Kind::IsWindowEnabled);
    window_bool("IsWindowUnicode", Host::UserCall::Kind::IsWindowUnicode);
    // ShowWindow(hwnd, cmd): returns the previous WS_VISIBLE.
    r.add("user32.dll", "ShowWindow", Conv::Stdcall, 2, [=, &h](Cpu& c) -> uint32_t {
        count("ShowWindow"); uint32_t token = c.arg(0);
        auto it = h.window_handles.find(token);
        if (it == h.window_handles.end()) { put_error(c, 6); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::ShowWindow; q.window = it->second; q.value = c.arg(1);
        if (!call_user(c, q)) return 0;
        return q.result ? 1u : 0u;
    });
    // EnableWindow(hwnd, enable): returns the previous WS_DISABLED state.
    r.add("user32.dll", "EnableWindow", Conv::Stdcall, 2, [=, &h](Cpu& c) -> uint32_t {
        count("EnableWindow"); uint32_t token = c.arg(0);
        auto it = h.window_handles.find(token);
        if (it == h.window_handles.end()) { put_error(c, 6); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::EnableWindow; q.window = it->second; q.value = c.arg(1);
        if (!call_user(c, q)) return 0;
        return q.result ? 1u : 0u;
    });

    // ---- Window relations ---------------------------------------------------
    // GetParent(hwnd): GWLP_HWNDPARENT as a token, 0 when none.
    r.add("user32.dll", "GetParent", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("GetParent"); uint32_t token = c.arg(0);
        auto it = h.window_handles.find(token);
        if (it == h.window_handles.end()) { put_error(c, 6); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetParent; q.window = it->second; q.ok = true;
        call_user(c, q);
        for (const auto& item : h.window_handles) if (item.second == q.result) return item.first;
        return 0;
    });
    // GetDesktopWindow(): map or create the record desktop once, tokenizing it
    // like any other window so child-relative routes can pass it around.
    r.add("user32.dll", "GetDesktopWindow", Conv::Stdcall, 0, [=, &h](Cpu& c) -> uint32_t {
        count("GetDesktopWindow");
        static uint32_t desktop_token = 0;
        if (desktop_token && h.window_handles.count(desktop_token)) return desktop_token;
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetDesktopWindow; q.ok = true;
        if (!call_user(c, q) || !q.result) return 0;
        if (++h.window_handle_serial < 0xD0000000u) h.window_handle_serial = 0xD0000000u;
        desktop_token = h.window_handle_serial;
        h.window_handles[desktop_token] = q.result;
        return desktop_token;
    });
    // GetTopWindow(hwnd)/GetWindow(hwnd, cmd)/GetAncestor(hwnd, flags): token in,
    // token out, 0 when the record answers none.
    auto relation_route = [&](const char* name, unsigned argc, Host::UserCall::Kind kind, bool command_arg) {
        r.add("user32.dll", name, Conv::Stdcall, argc, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t token = c.arg(0);
            auto it = h.window_handles.find(token);
            if ((token && it == h.window_handles.end()) || (argc == 2 && command_arg && c.arg(1) > 6 && kind == Host::UserCall::Kind::GetWindow)) {
                put_error(c, 6); return 0;
            }
            Host::UserCall q{}; q.kind = kind; q.window = token ? it->second : 0;
            if (argc == 2) q.value = c.arg(1);
            if (!call_user(c, q)) return 0;
            for (const auto& item : h.window_handles) if (item.second == q.result) return item.first;
            return 0;
        });
    };
    relation_route("GetTopWindow", 1, Host::UserCall::Kind::GetTopWindow, false);
    relation_route("GetAncestor", 2, Host::UserCall::Kind::GetAncestor, false);
    r.add("user32.dll", "GetWindow", Conv::Stdcall, 2, [=, &h](Cpu& c) -> uint32_t {
        count("GetWindow"); uint32_t token = c.arg(0), cmd = c.arg(1);
        if (cmd > 6) { put_error(c, 1414); return 0; }  // ERROR_INVALID_GW_COMMAND
        auto it = h.window_handles.find(token);
        if (it == h.window_handles.end()) { put_error(c, 6); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetWindow; q.window = it->second; q.value = cmd;
        if (!call_user(c, q)) return 0;
        for (const auto& item : h.window_handles) if (item.second == q.result) return item.first;
        return 0;
    });

    // ---- Window longs --------------------------------------------------------
    // Get/SetWindowLong(Ptr)(hwnd, index[, value]): one slot each on i686 for
    // every form (LONG_PTR is 32-bit here), GWL_* indices negative.
    for (bool wide : {false, true}) for (bool ptr : {false, true}) for (bool set : {false, true}) {
        std::string name = set ? "SetWindowLong" : "GetWindowLong";
        name += ptr ? "Ptr" : ""; name += wide ? "W" : "A";
        r.add("user32.dll", name, Conv::Stdcall, set ? 3u : 2u, [=, &h, kind = set ? Host::UserCall::Kind::SetWindowLongPtr : Host::UserCall::Kind::GetWindowLongPtr](Cpu& c) -> uint32_t {
            count(name); uint32_t token = c.arg(0); int32_t index = int32_t(c.arg(1));
            auto it = h.window_handles.find(token);
            if (it == h.window_handles.end()) { put_error(c, 6); return 0; }
            // A-hosted records keep one value() per index; the route checks only
            // the pointer-vs-integer confusion the record refuses.
            if (!ptr && (index == -4 || index == -21 || index == -8 || index == -12)) { put_error(c, 87); return 0; }  // GWLP_* on GetWindowLong
            Host::UserCall q{}; q.kind = kind; q.window = it->second; q.index = index;
            if (set) q.result = c.arg(2);
            if (!call_user(c, q)) return 0;
            return uint32_t(q.result);
        });
    }

    // ---- Window text ---------------------------------------------------------
    // The record exposes GetWindowTextLengthW only (title.size() in UTF-16
    // units); the A form answers the same count. The fixture asserts ASCII
    // titles, where UTF-16 and ANSI lengths agree.
    for (bool wide : {false, true}) for (bool get : {false, true}) {
        std::string name = get ? "GetWindowTextLength" : "SetWindowText";
        name += wide ? "W" : "A";
        r.add("user32.dll", name, Conv::Stdcall, get ? 1u : 2u, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t token = c.arg(0);
            auto it = h.window_handles.find(token);
            if (it == h.window_handles.end()) { put_error(c, 6); return 0; }
            Host::UserCall q{}; q.kind = get ? Host::UserCall::Kind::GetWindowTextLength : Host::UserCall::Kind::SetWindowText;
            q.wide = wide; q.window = it->second;
            if (!get && c.arg(1)) q.text = wide ? read_wstr(c, c.arg(1)) : read_astr(c, c.arg(1));
            if (!call_user(c, q)) return 0;
            return get ? uint32_t(q.result) : q.result;
        });
    }
    // GetWindowText(hwnd, LP[T]STR, max): copies at most max-1 characters plus
    // the terminator (ANSI callers count bytes after the record's CP_ACP
    // convert); 0-length or NULL buffers answer 0 without an error.
    for (bool wide : {false, true}) {
        std::string name = wide ? "GetWindowTextW" : "GetWindowTextA";
        r.add("user32.dll", name, Conv::Stdcall, 3, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t token = c.arg(0), out = c.arg(1); int32_t size = int32_t(c.arg(2));
            auto it = h.window_handles.find(token);
            if (!out || size <= 0 || it == h.window_handles.end()) { put_error(c, out && size > 0 ? 6 : 0); return 0; }
            Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetWindowText; q.wide = wide; q.window = it->second;
            q.value = uint32_t(size);
            if (!call_user(c, q)) return 0;
            if (wide) {
                const uint32_t chars = uint32_t(q.text.size() < size_t(size) ? q.text.size() : size_t(size) - 1);
                for (uint32_t i = 0; i < chars; ++i) c.mem.w(out + i * 2, uint16_t(q.text[i]), 2);
                c.mem.w(out + chars * 2, 0, 2);
                return chars;
            }
            // ANSI: q.bytes holds the CP_ACP conversion the record produced for
            // the narrow caller; truncate to size-1 bytes like c_GetWindowTextA.
            const uint32_t bytes = uint32_t(q.bytes.size() < size_t(size) ? q.bytes.size() : size_t(size) - 1);
            c.mem.write_bytes(out, q.bytes.data(), bytes);
            c.mem.w8(out + bytes, 0);
            return bytes;
        });
    }

    // ---- Focus, activation, foreground, capture -----------------------------
    // GetFocus/GetActiveWindow/GetForegroundWindow: this thread's state as a
    // token; the shim calls them on the guest thread whose id set_message_thread
    // publishes, so record per-thread answers match the caller.
    auto focus_query = [&](const char* name, Host::UserCall::Kind kind) {
        r.add("user32.dll", name, Conv::Stdcall, 0, [=, &h](Cpu& c) -> uint32_t {
            count(name); Host::UserCall q{}; q.kind = kind; q.ok = true;
            call_user(c, q);
            return token_for(q.result);
        });
    };
    focus_query("GetFocus", Host::UserCall::Kind::GetFocus);
    focus_query("GetActiveWindow", Host::UserCall::Kind::GetActiveWindow);
    focus_query("GetForegroundWindow", Host::UserCall::Kind::GetForegroundWindow);
    // SetFocus(hwnd)/SetActiveWindow(hwnd): HWND old, NULL + relayed error when
    // the record refuses (bad window: 1400-class relay; cross-thread: 5).
    auto focus_set = [&](const char* name, Host::UserCall::Kind kind) {
        r.add("user32.dll", name, Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t token = c.arg(0);
            auto it = h.window_handles.find(token);
            if (token && it == h.window_handles.end()) { put_error(c, 6); return 0; }
            Host::UserCall q{}; q.kind = kind; q.window = token ? it->second : 0;
            if (!call_user(c, q)) return 0;
            return token_for(q.result);
        });
    };
    focus_set("SetFocus", Host::UserCall::Kind::SetFocus);
    focus_set("SetActiveWindow", Host::UserCall::Kind::SetActiveWindow);
    r.add("user32.dll", "SetForegroundWindow", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("SetForegroundWindow"); uint32_t token = c.arg(0);
        auto it = h.window_handles.find(token);
        if (it == h.window_handles.end()) { put_error(c, 6); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::SetForegroundWindow; q.window = it->second;
        return call_user(c, q) ? 1u : 0u;
    });
    // SetCapture(hwnd): HWND old (validated by the record); ReleaseCapture(): TRUE.
    r.add("user32.dll", "SetCapture", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("SetCapture"); uint32_t token = c.arg(0);
        auto it = h.window_handles.find(token);
        if (token && it == h.window_handles.end()) { put_error(c, 6); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::SetCapture; q.window = token ? it->second : 0;
        if (!call_user(c, q)) return 0;
        return token_for(q.result);
    });
    r.add("user32.dll", "ReleaseCapture", Conv::Stdcall, 0, [=, &h](Cpu& c) -> uint32_t {
        count("ReleaseCapture"); Host::UserCall q{}; q.kind = Host::UserCall::Kind::ReleaseCapture;
        return call_user(c, q) ? 1u : 0u;
    });

    // ---- Cursor position and visibility --------------------------------------
    // GetCursorPos(POINT*) / SetCursorPos(x, y): record virtual-desktop state.
    r.add("user32.dll", "GetCursorPos", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("GetCursorPos"); uint32_t out = c.arg(0);
        if (!out) { put_error(c, 0); return 0; }  // record answers FALSE, no documented error change
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetCursorPos;
        if (!call_user(c, q)) return 0;
        write_point(c, out, q.point); return 1;
    });
    r.add("user32.dll", "SetCursorPos", Conv::Stdcall, 2, [=, &h](Cpu& c) -> uint32_t {
        count("SetCursorPos"); Host::UserCall q{}; q.kind = Host::UserCall::Kind::SetCursorPos;
        q.x = int32_t(c.arg(0)); q.y = int32_t(c.arg(1));
        return call_user(c, q) ? 1u : 0u;
    });
    // ClipCursor(RECT*) / GetClipCursor(RECT*): NULL clip releases the clip.
    for (bool get : {false, true}) r.add("user32.dll", get ? "GetClipCursor" : "ClipCursor", Conv::Stdcall, 1,
        [=, &h](Cpu& c) -> uint32_t {
            count(get ? "GetClipCursor" : "ClipCursor"); uint32_t out = c.arg(0);
            Host::UserCall q{}; q.kind = get ? Host::UserCall::Kind::GetClipCursor : Host::UserCall::Kind::ClipCursor;
            if (!get && out) map_rect(q.rect, out, c);
            if (!call_user(c, q)) return 0;
            if (get && out) write_rect(c, out, q.rect_out);
            return 1;
        });
    // SetCursor(hcur): HCURSOR old, 0 when none/invalid token.
    r.add("user32.dll", "SetCursor", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("SetCursor"); uint32_t token = c.arg(0);
        auto it = h.icon_handles.find(token);
        if (token && it == h.icon_handles.end()) { put_error(c, 0); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::SetCursor; q.handle = token ? it->second : 0;
        if (!call_user(c, q)) return 0;
        for (const auto& item : h.icon_handles) if (item.second == q.result) return item.first;
        return 0;
    });
    // ShowCursor(BOOL): the record's per-thread display counter.
    r.add("user32.dll", "ShowCursor", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("ShowCursor"); Host::UserCall q{}; q.kind = Host::UserCall::Kind::ShowCursor;
        q.value = c.arg(0) ? 1u : 0u;
        if (!call_user(c, q)) return 0;
        return uint32_t(q.result);
    });

    // ---- Keyboard state and translation --------------------------------------
    // GetKeyState(vk): SHORT down(0x8000)|toggle(0x01) from the record's table;
    // GetAsyncKeyState(vk): 0x8000 down plus the pressed-since bit it consumes.
    r.add("user32.dll", "GetKeyState", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("GetKeyState"); Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetKeyState; q.value = c.arg(0);
        if (!call_user(c, q)) return 0;
        return uint32_t(int32_t(q.result) & 0xFFFFu);
    });
    r.add("user32.dll", "GetAsyncKeyState", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("GetAsyncKeyState"); Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetAsyncKeyState; q.value = c.arg(0);
        if (!call_user(c, q)) return 0;
        return uint32_t(int32_t(q.result) & 0xFFFFu);
    });
    // GetKeyboardState(BYTE[256]): the record's 256-byte table, NULL refused 87.
    r.add("user32.dll", "GetKeyboardState", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("GetKeyboardState"); uint32_t out = c.arg(0);
        if (!out) { put_error(c, 87); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetKeyboardState;
        if (!call_user(c, q) || q.bytes.size() != 256) { if (q.ok && q.bytes.size() != 256) put_error(c, 87); return 0; }
        c.mem.write_bytes(out, q.bytes.data(), 256); return 1;
    });
    // MapVirtualKey(A/W/ExA)(code, type): pure vk<->scan translation. MAPVK_VSC
    // inputs carry the scan code, MAPVK_VK inputs the virtual key.
    auto map_vk = [&](const char* name, unsigned argc) {
        r.add("user32.dll", name, Conv::Stdcall, argc, [=, &h](Cpu& c) -> uint32_t {
            count(name); Host::UserCall q{}; q.kind = Host::UserCall::Kind::MapVirtualKey;
            q.value = c.arg(0); q.action = c.arg(1);
            if (!call_user(c, q)) return 0;
            return q.result;
        });
    };
    map_vk("MapVirtualKeyA", 2); map_vk("MapVirtualKeyW", 2); map_vk("MapVirtualKeyExA", 3);
    // ToUnicode(vk, scan, BYTE[256] state, WCHAR* buf, int size, flags): the
    // composed characters, 0 when the key translates to nothing. A NULL state
    // reads as all-up, the record's own default.
    r.add("user32.dll", "ToUnicode", Conv::Stdcall, 6, [=, &h](Cpu& c) -> uint32_t {
        count("ToUnicode"); uint32_t vk = c.arg(0), state = c.arg(2), out = c.arg(3);
        int32_t size = int32_t(c.arg(4));
        if (!out || size < 1) return 0;  // record answers 0, no error relay
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::ToUnicode; q.value = vk; q.x = int32_t(c.arg(1));
        q.width = size; q.flags = c.arg(5);
        if (state) q.bytes = c.mem.read_bytes(state, 256);
        if (!call_user(c, q)) return 0;
        const uint32_t chars = uint32_t(q.text.size() < size_t(size) ? q.text.size() : size_t(size));
        for (uint32_t i = 0; i < chars; ++i) c.mem.w(out + i * 2, uint16_t(q.text[i]), 2);
        return chars;
    });

    // ---- Icons and cursors (the record's one shared handle table) ------------
    // Handles are guest tokens over native HICON/HCURSOR values; the record's
    // LoadCursorW routes through LoadImageW(LR_DEFAULTSIZE|LR_SHARED) and the
    // A forms normalize through c_LoadImageA, so every export lands on one
    // LoadImage record call keyed by IMAGE_*.
    auto image_type_guard = [&](Cpu& c, uint32_t type) -> bool {
        if (type == 0 /*IMAGE_BITMAP*/) { put_error(c, 50); return false; }  // ERROR_NOT_SUPPORTED: no GDI bitmap backend
        if (type != 1 && type != 2) { put_error(c, 87); return false; }      // IMAGE_ICON/IMAGE_CURSOR only
        return true;
    };
    auto load_image_route = [&](const char* name, unsigned argc, bool wide, bool cursor, bool icon) {
        r.add("user32.dll", name, Conv::Stdcall, argc, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t name_arg = c.arg(1);
            const uint32_t type = icon ? 1u /*IMAGE_ICON*/ : cursor ? 2u /*IMAGE_CURSOR*/ : c.arg(2);
            if (!image_type_guard(c, type)) return 0;
            Host::UserCall q{}; q.kind = Host::UserCall::Kind::LoadImage; q.wide = wide; q.action = type;
            q.x = cursor || icon ? 0 : int32_t(c.arg(3)); q.y = cursor || icon ? 0 : int32_t(c.arg(4));
            q.flags = cursor || icon ? 0x8040u /*LR_SHARED|LR_DEFAULTSIZE*/ : c.arg(5);
            if (name_arg) {
                if (name_arg >> 16) q.name_id = name_arg;              // IS_INTRESOURCE
                else q.name = wide ? [&read_wstr] (Cpu& c, uint32_t a) { auto s = read_wstr(c, a); return std::string(s.begin(), s.end()); } (c, name_arg)
                                  : std::string(c.mem.read_cstr(name_arg, 32768));
            }
            if (!call_user(c, q) || !q.result) return 0;
            return alloc_handle(h.icon_handles, q.result);
        });
    };
    load_image_route("LoadImageW", 6, true, false, false);
    load_image_route("LoadImageA", 6, false, false, false);
    load_image_route("LoadCursorW", 2, true, true, false);
    load_image_route("LoadCursorA", 2, false, true, false);
    load_image_route("LoadIconW", 2, true, false, true);
    load_image_route("LoadIconA", 2, false, false, true);
    // DestroyIcon/DestroyCursor: shared handles stay (TRUE), unknown: FALSE 1402.
    auto destroy_icon_route = [&](const char* name) {
        r.add("user32.dll", name, Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t token = c.arg(0);
            auto it = h.icon_handles.find(token);
            if (token && it == h.icon_handles.end()) { put_error(c, 1402); return 0; }  // ERROR_INVALID_CURSOR_HANDLE
            Host::UserCall q{}; q.kind = Host::UserCall::Kind::DestroyIcon; q.handle = token ? it->second : 0;
            if (!call_user(c, q)) return 0;
            h.icon_handles.erase(token);
            return q.result ? 1u : 0u;
        });
    };
    destroy_icon_route("DestroyIcon");
    destroy_icon_route("DestroyCursor");
    // GetIconInfo(hicon, ICONINFO*): guest 20-byte struct; hbmMask/hbmColor come
    // back NULL (documented record gap: no GDI bitmap backend).
    r.add("user32.dll", "GetIconInfo", Conv::Stdcall, 2, [=, &h](Cpu& c) -> uint32_t {
        count("GetIconInfo"); uint32_t token = c.arg(0), out = c.arg(1);
        auto it = h.icon_handles.find(token);
        if (!out || (token && it == h.icon_handles.end())) { put_error(c, 1402); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetIconInfo; q.handle = token ? it->second : 0;
        if (!call_user(c, q)) return 0;
        c.mem.w32(out, q.result ? 1u : 0u);            // fIcon
        c.mem.w32(out + 4, q.x); c.mem.w32(out + 8, uint32_t(q.y));  // xHotspot, yHotspot
        c.mem.w32(out + 12, 0); c.mem.w32(out + 16, 0);  // hbmMask, hbmColor: NULL
        return 1;
    });
    // CopyImage(handle, type, cx, cy, flags): a new non-shared record handle.
    r.add("user32.dll", "CopyImage", Conv::Stdcall, 5, [=, &h](Cpu& c) -> uint32_t {
        count("CopyImage"); uint32_t token = c.arg(0), type = c.arg(1);
        auto it = h.icon_handles.find(token);
        if (!image_type_guard(c, type)) return 0;
        if (token && it == h.icon_handles.end()) { put_error(c, 1402); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::CopyImage; q.handle = token ? it->second : 0;
        q.action = type; q.x = int32_t(c.arg(2)); q.y = int32_t(c.arg(3)); q.flags = c.arg(4);
        if (!call_user(c, q) || !q.result) return 0;
        return alloc_handle(h.icon_handles, q.result);
    });

    // ---- Message pump ---------------------------------------------------------
    // The record's queue is keyed by the host thread id it sees; pump routes
    // publish the guest thread's id (TEB fs:[0x24]) first so posts, peeks and
    // wake-ups land on the caller's own queue. wParam/lParam travel as 8 bytes
    // of q.bytes (LE) and answers come back in q.result.
    auto pump_args = [](Cpu& c, Host::UserCall& q, unsigned first_slot) {
        uint32_t wp = c.arg(first_slot), lp = c.arg(first_slot + 1);
        for (int i = 0; i < 4; ++i) q.bytes.push_back((wp >> (8 * i)) & 0xff);
        for (int i = 0; i < 4; ++i) q.bytes.push_back((lp >> (8 * i)) & 0xff);
    };
    auto pump_thread = [&h, &put_error](Cpu& c, Host::UserCall&) -> bool {
        if (!h.set_message_thread) { put_error(c, 120); return false; }  // no pump host configured
        h.set_message_thread(c.mem.r32(c.fs_base + 0x24));
        return true;
    };
    // A retrieved MSG marshals field-by-field: guest i686 MSG is 28 bytes
    // {HWND, UINT, WPARAM, LPARAM, DWORD time, POINT pt}, the record's is its
    // 48-byte Win64 shape; hwnd round-trips through the token table.
    auto write_msg = [&](Cpu& c, uint32_t out, const Host::UserCall& q) -> uint32_t {
        c.mem.w32(out, token_for(q.window2));                 // hwnd
        c.mem.w32(out + 4, q.value);                          // message
        uint32_t wp = 0, lp = 0;
        for (int i = 0; i < 4; ++i) wp |= uint32_t(q.bytes[i]) << (8 * i);
        for (int i = 0; i < 4; ++i) lp |= uint32_t(q.bytes[4 + i]) << (8 * i);
        c.mem.w32(out + 8, wp); c.mem.w32(out + 12, lp);
        c.mem.w32(out + 16, q.id);                            // time
        c.mem.w32(out + 20, q.point.x); c.mem.w32(out + 24, q.point.y);
        return wp;
    };
    for (bool wide : {false, true}) for (bool get : {false, true}) {
        std::string name = get ? "GetMessage" : "PeekMessage";
        name += wide ? "W" : "A";
        r.add("user32.dll", name, Conv::Stdcall, get ? 4u : 5u, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t out = c.arg(0), filter = c.arg(1);
            auto it = h.window_handles.find(filter);
            // (HWND)-1 means message-only windows; an unknown real hwnd is an
            // invalid filter (GetMessage: -1/1400, PeekMessage: FALSE).
            if (filter != 0xFFFFFFFFu && filter && it == h.window_handles.end()) {
                put_error(c, get ? 1400u : 6u);
                return get ? 0xFFFFFFFFu : 0u;
            }
            if (!out) { put_error(c, get ? 1400u : 87u); return get ? 0xFFFFFFFFu : 0u; }
            Host::UserCall q{}; q.kind = get ? Host::UserCall::Kind::GetMessage : Host::UserCall::Kind::PeekMessage;
            q.wide = wide;
            // (HWND)-1 filters message-only windows: the record keys on a NULL
            // native hwnd but must not validate the filter as a real window;
            // NULL accepts everything; otherwise the token's native window.
            q.window = filter == 0xFFFFFFFFu ? uintptr_t(1) : filter ? it->second : 0;
            q.value = c.arg(2);                                  // first message
            q.id = c.arg(get ? 3u : 2u);                         // last message
            if (!get) q.flags = c.arg(4);                        // PM_* flags
            if (!pump_thread(c, q)) return get ? 0xFFFFFFFFu : 0u;
            if (!call_user(c, q)) return get ? 0xFFFFFFFFu : 0u;
            if (!q.result) return q.ok ? 0u : 0xFFFFFFFFu;       // none/WM_QUIT: Peek FALSE, GetMessage 0
            return write_msg(c, out, q) == 0x0012u /*WM_QUIT*/ && get ? 0u : 1u;
        });
    }
    // PostMessage(hwnd|NULL, msg, wp, lp): FALSE + relayed error when the
    // record refuses (broadcast: 50, WM_CHAR-family >127 payload: 50, text
    // classes: 1159); a posted id rides q.value.
    for (bool wide : {false, true}) {
        std::string name = wide ? "PostMessageW" : "PostMessageA";
        r.add("user32.dll", name, Conv::Stdcall, 4, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t token = c.arg(0);
            auto it = h.window_handles.find(token);
            if (token && it == h.window_handles.end()) { put_error(c, 6); return 0; }
            Host::UserCall q{}; q.kind = Host::UserCall::Kind::PostMessage; q.wide = wide;
            q.window = token ? it->second : 0;
            q.value = c.arg(1);                                  // message id
            if (!pump_thread(c, q)) return 0;
            pump_args(c, q, 2);                                  // wParam, lParam
            if (!call_user(c, q)) return 0;
            return q.result ? 1u : 0u;
        });
    }
    // PostThreadMessage(id, msg, wp, lp): the record answers 1444 for a thread
    // with no queue; the guest id the shim publishes is what the record sees.
    r.add("user32.dll", "PostThreadMessageW", Conv::Stdcall, 4, [=, &h](Cpu& c) -> uint32_t {
        count("PostThreadMessageW");
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::PostThreadMessage;
        q.id = c.arg(0); q.value = c.arg(1);
        if (!pump_thread(c, q)) return 0;
        pump_args(c, q, 2);
        if (!call_user(c, q)) return 0;
        return q.result ? 1u : 0u;
    });
    // PostQuitMessage(code): coalesces into this thread's queued WM_QUIT.
    r.add("user32.dll", "PostQuitMessage", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("PostQuitMessage");
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::PostQuitMessage; q.result = c.arg(0);
        if (!pump_thread(c, q)) return 0;
        call_user(c, q); return 0;
    });
    // TranslateMessage(msg*): key-down posts WM_CHAR/WM_SYSCHAR from the record
    // table; key-up answers TRUE; anything else FALSE. wParam rides q.value.
    r.add("user32.dll", "TranslateMessage", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("TranslateMessage"); uint32_t msg = c.arg(0);
        if (!msg) return 0;                                       // record answers FALSE
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::TranslateMessage;
        q.value = c.mem.r32(msg + 4);                             // message id
        auto msg_window = h.window_handles.find(c.mem.r32(msg));
        q.window2 = msg_window == h.window_handles.end() ? 0 : msg_window->second;
        if (!pump_thread(c, q)) return 0;
        pump_args(c, q, 0);                                       // unused; wParam/lParam read below
        q.bytes.clear();
        uint32_t wp = c.mem.r32(msg + 8), lp = c.mem.r32(msg + 12);
        for (int i = 0; i < 4; ++i) q.bytes.push_back((wp >> (8 * i)) & 0xff);
        for (int i = 0; i < 4; ++i) q.bytes.push_back((lp >> (8 * i)) & 0xff);
        if (!call_user(c, q)) return 0;
        return q.result ? 1u : 0u;
    });
    // DispatchMessage(msg*): WM_TIMER with a lParam proc re-enters guest code
    // through the record's callback contract (q.result carries the guest
    // thunks' proc); otherwise SendMessageW semantics inside the record.
    for (bool wide : {false, true}) {
        std::string name = wide ? "DispatchMessageW" : "DispatchMessageA";
        r.add("user32.dll", name, Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t msg = c.arg(0);
            if (!msg) { put_error(c, 87); return 0; }
            Host::UserCall q{}; q.kind = Host::UserCall::Kind::DispatchTimer; q.wide = wide;
            auto msg_window = h.window_handles.find(c.mem.r32(msg));
            q.window2 = msg_window == h.window_handles.end() ? 0 : msg_window->second;
            q.value = c.mem.r32(msg + 4);
            uint32_t wp = c.mem.r32(msg + 8), lp = c.mem.r32(msg + 12);
            for (int i = 0; i < 4; ++i) q.bytes.push_back((wp >> (8 * i)) & 0xff);
            for (int i = 0; i < 4; ++i) q.bytes.push_back((lp >> (8 * i)) & 0xff);
            q.id = c.mem.r32(msg + 16);
            q.point = read_point(c, msg + 20);
            if (!call_user(c, q)) return 0;
            return uint32_t(q.result);
        });
    }
    // SetTimer(hwnd, id, elapse, proc): bookkeeping only in the record (no
    // WM_TIMER source); NULL hwnd or id makes the record allocate one.
    r.add("user32.dll", "SetTimer", Conv::Stdcall, 4, [=, &h](Cpu& c) -> uint32_t {
        count("SetTimer"); uint32_t token = c.arg(0);
        auto it = h.window_handles.find(token);
        if (token && it == h.window_handles.end()) { put_error(c, 6); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::SetTimer;
        q.window = token ? it->second : 0; q.id = c.arg(1);
        q.value = c.arg(2);                                       // elapse
        q.result = c.arg(3);                                      // proc pointer (guest-side thunk)
        if (!pump_thread(c, q)) return 0;
        if (!call_user(c, q)) return 0;
        return uint32_t(q.result);
    });
    r.add("user32.dll", "KillTimer", Conv::Stdcall, 2, [=, &h](Cpu& c) -> uint32_t {
        count("KillTimer"); uint32_t token = c.arg(0);
        auto it = h.window_handles.find(token);
        if (token && it == h.window_handles.end()) { put_error(c, 6); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::KillTimer;
        q.window = token ? it->second : 0; q.id = c.arg(1);
        if (!pump_thread(c, q)) return 0;
        if (!call_user(c, q)) return 0;
        return q.result ? 1u : 0u;
    });
    // GetQueueStatus(flags): MAKELONG(bits, bits) from QS_POSTMESSAGE|QS_ALLPOSTMESSAGE.
    r.add("user32.dll", "GetQueueStatus", Conv::Stdcall, 1, [=, &h](Cpu& c) -> uint32_t {
        count("GetQueueStatus");
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetQueueStatus; q.flags = c.arg(0);
        if (!pump_thread(c, q)) return 0;
        if (!call_user(c, q)) return 0;
        return uint32_t(q.result);
    });
    r.add("user32.dll", "GetMessageTime", Conv::Stdcall, 0, [=, &h](Cpu& c) -> uint32_t {
        count("GetMessageTime");
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetMessageTime;
        if (!pump_thread(c, q)) return 0;
        if (!call_user(c, q)) return 0;
        return uint32_t(q.result);
    });
    r.add("user32.dll", "GetMessagePos", Conv::Stdcall, 0, [=, &h](Cpu& c) -> uint32_t {
        count("GetMessagePos");
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetMessagePos;
        if (!pump_thread(c, q)) return 0;
        if (!call_user(c, q)) return 0;
        return uint32_t(q.result);
    });

    // ---- SendMessage / DefWindowProc -----------------------------------------
    // Synchronous calls into the record's window procedure. Scalar payloads
    // only here: WM_SETTEXT/WM_GETTEXT carry q.text/q.value (buffer size).
    auto send_route = [&](const char* name, bool wide) {
        r.add("user32.dll", name, Conv::Stdcall, 4, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t token = c.arg(0), msg = c.arg(1);
            auto it = h.window_handles.find(token);
            if (it == h.window_handles.end()) { put_error(c, 6); return 0; }
            Host::UserCall q{}; q.kind = Host::UserCall::Kind::SendMessage; q.wide = wide;
            q.window = it->second; q.value = msg;
            q.result = c.arg(2);                                  // wParam
            if (!pump_thread(c, q)) return 0;
            uint32_t lp = c.arg(3);
            for (int i = 0; i < 4; ++i) q.bytes.push_back((lp >> (8 * i)) & 0xff);
            // Text payloads travel as strings; everything else stays scalar.
            if (msg == 0x000Cu /*WM_SETTEXT*/ && lp) q.text = wide ? read_wstr(c, lp) : read_astr(c, lp);
            if (msg == 0x000Du /*WM_GETTEXT*/) q.width = int32_t(c.arg(2));
            if (!call_user(c, q)) return 0;
            return uint32_t(q.result);
        });
    };
    send_route("SendMessageW", true);
    send_route("SendMessageA", false);
    auto def_route = [&](const char* name, bool wide) {
        r.add("user32.dll", name, Conv::Stdcall, 4, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t token = c.arg(0), msg = c.arg(1);
            auto it = h.window_handles.find(token);
            if (it == h.window_handles.end()) { put_error(c, 6); return 0; }
            Host::UserCall q{}; q.kind = Host::UserCall::Kind::DefWindowProc; q.wide = wide;
            q.window = it->second; q.value = msg;
            q.result = c.arg(2);
            uint32_t lp = c.arg(3);
            for (int i = 0; i < 4; ++i) q.bytes.push_back((lp >> (8 * i)) & 0xff);
            if (!call_user(c, q)) return 0;
            return uint32_t(q.result);
        });
    };
    def_route("DefWindowProcW", true);
    def_route("DefWindowProcA", false);

    // ---- Monitors and system parameters ---------------------------------------
    // MonitorFrom{Window,Rect,Point}: the one record display; NULL/empty inputs
    // with flags&3 fall back to it, otherwise the record answers NULL. The
    // record's stable &monitorToken maps to one shim token, reused after.
    auto monitor_result = [&h, alloc_handle](const Host::UserCall& q) -> uint32_t {
        if (!q.result) return 0;
        for (const auto& item : h.dc_handles) if (item.second == q.result) return item.first;
        return alloc_handle(h.dc_handles, q.result);
    };
    r.add("user32.dll", "MonitorFromWindow", Conv::Stdcall, 2, [=, &h](Cpu& c) -> uint32_t {
        count("MonitorFromWindow"); uint32_t token = c.arg(0);
        auto it = h.window_handles.find(token);
        if (token && it == h.window_handles.end()) { put_error(c, 0); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::MonitorFromWindow;
        q.window = token ? it->second : 0; q.flags = c.arg(1);
        if (!call_user(c, q)) return 0;
        return monitor_result(q);
    });
    r.add("user32.dll", "MonitorFromRect", Conv::Stdcall, 2, [=, &h](Cpu& c) -> uint32_t {
        count("MonitorFromRect"); uint32_t rect = c.arg(0);
        if (!rect) return 0;
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::MonitorFromRect;
        map_rect(q.rect, rect, c); q.flags = c.arg(1);
        if (!call_user(c, q)) return 0;
        return monitor_result(q);
    });
    r.add("user32.dll", "MonitorFromPoint", Conv::Stdcall, 2, [=, &h](Cpu& c) -> uint32_t {
        count("MonitorFromPoint");
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::MonitorFromPoint;
        q.point = read_point(c, c.arg(0)); q.flags = c.arg(1);
        if (!call_user(c, q)) return 0;
        return monitor_result(q);
    });
    // GetMonitorInfo(hmon, info*): cbSize must name MONITORINFO(40) or
    // MONITORINFOEX(A/W: 40+CHAR[32] or 40+WCHAR[32]); fields fill from the
    // record's single display. Monitor tokens come from MonitorFrom*.
    for (bool wide : {false, true}) {
        std::string name = wide ? "GetMonitorInfoW" : "GetMonitorInfoA";
        r.add("user32.dll", name, Conv::Stdcall, 2, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t token = c.arg(0), out = c.arg(1);
            auto it = h.dc_handles.find(token);
            if (!out || it == h.dc_handles.end()) {
                put_error(c, 87); return 0;
            }
            const uint32_t size = c.mem.r32(out);
            const uint32_t base = 40, a_ex = 72, w_ex = 104;          // MONITORINFO, +CHAR[32], +WCHAR[32]
            if (size != base || (!wide && size != a_ex) || (wide && size != w_ex)) { put_error(c, 87); return 0; }
            Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetMonitorInfo; q.wide = wide;
            q.handle = it->second; q.value = size;
            if (!call_user(c, q)) return 0;
            write_rect(c, out + 4, q.rect); write_rect(c, out + 20, q.rect2);
            c.mem.w32(out + 36, q.flags);                             // dwFlags
            if (size > base) for (uint32_t i = 0; i < 64; ++i) c.mem.w8(out + base + i, uint8_t(i < 13 ? "\\\\.\\DISPLAY1"[i] : 0));
            return 1;
        });
    }
    // SystemParametersInfo(A/W)(action, uiParam, pv, fWinIni): SPI_GETWORKAREA
    // fills a RECT; the input_parameter_default table answers UINT queries;
    // everything else is honestly unsupported (ERROR_NOT_SUPPORTED).
    for (bool wide : {false, true}) {
        std::string name = wide ? "SystemParametersInfoW" : "SystemParametersInfoA";
        r.add("user32.dll", name, Conv::Stdcall, 4, [=, &h](Cpu& c) -> uint32_t {
            count(name); uint32_t action = c.arg(0), out = c.arg(2);
            if (action == 0x0030u /*SPI_GETWORKAREA*/ && !out) { put_error(c, 0); return 0; }
            Host::UserCall q{}; q.kind = Host::UserCall::Kind::SystemParametersInfo; q.wide = wide;
            q.action = action; q.value = c.arg(1); q.flags = c.arg(3);
            if (!call_user(c, q)) return 0;
            if (action == 0x0030u && out) write_rect(c, out, q.rect_out);
            else if (out) c.mem.w32(out, q.result);
            return 1;
        });
    }
    // GetWindowPlacement(hwnd, WINDOWPLACEMENT*): 44 bytes on i686 and the
    // record both; length must match or 87.
    r.add("user32.dll", "GetWindowPlacement", Conv::Stdcall, 2, [=, &h](Cpu& c) -> uint32_t {
        count("GetWindowPlacement"); uint32_t token = c.arg(0), out = c.arg(1);
        auto it = h.window_handles.find(token);
        if (!out || it == h.window_handles.end()) { put_error(c, out ? 6 : 87); return 0; }
        if (c.mem.r32(out) != 44) { put_error(c, 87); return 0; }
        Host::UserCall q{}; q.kind = Host::UserCall::Kind::GetWindowPlacement; q.window = it->second;
        if (!call_user(c, q)) return 0;
        // bytes: flags(4) showCmd(4) ptMin(8) ptMax(8) rcNormal(16); length stays.
        const uint8_t* d = q.bytes.data();
        auto r32 = [&d](size_t at) { uint32_t v; std::memcpy(&v, d + at, 4); return v; };
        c.mem.w32(out + 4, r32(0)); c.mem.w32(out + 8, r32(4));
        c.mem.w32(out + 12, r32(8)); c.mem.w32(out + 16, r32(12));
        c.mem.w32(out + 20, r32(16)); c.mem.w32(out + 24, r32(20));
        write_rect(c, out + 28, Host::UserRect{r32(24), r32(28), r32(32), r32(36)});
        return 1;
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
