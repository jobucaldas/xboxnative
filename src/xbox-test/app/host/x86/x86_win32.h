// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit allowlist of Win32/CRT imports the x86 core can serve today, with
// hand-written checked marshallers (every guest pointer goes through Memory, so a
// bad pointer is a Fault, never a host access). Milestone 1 surface only: a
// handful of kernel32 calls and two cdecl msvcrt calls, one of which calls back
// into guest code. Generated thunks for the wider surface come later (see plan).
#pragma once
#include <chrono>
#include <functional>
#include <thread>
#include "x86_pe.h"
#include "x86_gl_generated.h"
#include "x86com.h"
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
        uint32_t last_error = 0;  // host Winsock error on failure; written to the guest TEB like other host routes
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
        uint32_t last_error = 0;
        bool gap = false;
    };
    struct WindowCreate {
        std::u16string class_name, title;
        uint32_t ex_style = 0, style = 0, x = 0, y = 0, width = 0, height = 0;
        uintptr_t parent = 0, menu = 0, instance = 0;
    };
    struct WindowCall {
        std::string name;
        uintptr_t args[8]{};
        std::vector<uint8_t> input, output;
        uintptr_t result = 0;
        bool supported = false;
    };
    struct XInputCall {
        enum class Kind { GetState, GetStateEx, SetState, GetCapabilities, GetCapabilitiesEx, Enable, GetBatteryInformation, GetKeystroke } kind;
        uint32_t index = 0, flags = 0, device_type = 0, reserved = 0;
        std::vector<uint8_t> input, output;
        uint32_t result = 1167; // ERROR_DEVICE_NOT_CONNECTED until a host explicitly supplies the slot
    };
    struct WindowRect { int32_t left = 0, top = 0, right = 0, bottom = 0; };
    struct Timer { uint32_t hwnd = 0, id = 0, interval = 1, proc = 0; uint64_t due = 0; bool pending = false; };
    struct RegistryCall {
        enum class Kind { Open, Create, Query, Set, DeleteValue, DeleteKey, EnumKey, EnumValue, Close } kind;
        uintptr_t handle = 0, result_handle = 0;
        uint32_t root = 0, index = 0, options = 0, access = 0, type = 0, disposition = 0;
        bool ansi = false;
        std::u16string subkey, name, class_name;
        std::vector<uint8_t> data;
        uint32_t status = 120;
    };
    struct GlMap { uint32_t address = 0, size = 0, target = 0; uintptr_t offset = 0; bool writable = false; };
    std::string out, err;                         // captured stdout/stderr bytes
    std::function<void(const std::string&)> tee;  // optional live sink
    std::function<NlsResponse(const NlsRequest&)> nls_call;
    std::function<void(FileCall&)> file_call;     // Play's native kernel32 file layer
    std::function<void(RegistryCall&)> registry_call; // Play's persistent guest registry
    std::function<bool(uint8_t*, uint32_t)> random_bytes;
    std::function<bool(const char*, const std::u16string&)> destructive_file_guard;
    std::function<void(SocketCall&)> socket_call;
    std::function<uint64_t()> tick_count;          // same monotonic millisecond source for the guest tick APIs
    std::function<void(LocaleCall&)> locale_call; // Play's native kernel32 locale formatter
    std::function<void(GlCall&)> gl_call;
    std::function<bool(const std::u16string&)> register_window_class;
    std::function<bool(const std::u16string&)> unregister_window_class;
    std::function<bool(int, int&)> system_metrics;
    std::function<uintptr_t(const WindowCreate&)> create_window;
    std::function<bool(WindowRect&, uint32_t, bool, uint32_t)> adjust_window_rect;
    std::function<bool(uintptr_t, WindowRect&)> get_client_rect;
    std::function<bool(uintptr_t, int32_t&, int32_t&, bool)> map_window_point;
    std::function<bool(uintptr_t)> destroy_window;
    std::function<intptr_t(uintptr_t, int, bool)> get_window_long;
    std::function<intptr_t(uintptr_t, int, intptr_t, bool)> set_window_long;
    std::function<uintptr_t(uintptr_t)> get_dc;
    std::function<bool(uintptr_t, uintptr_t)> release_dc;
    std::function<void(WindowCall&)> window_call;
    std::function<void(XInputCall&)> xinput_call;
    std::function<void(const std::string&)> route_trace;
    std::map<uint32_t, uintptr_t> window_handles, dc_handles, gl_contexts, user_handles;
    std::map<std::u16string, uint32_t> window_class_procs;
    std::map<uint32_t, uint32_t> window_procs;
    std::map<uint32_t, std::map<int32_t, uint32_t>> window_longs;
    std::map<uint32_t, std::u16string> window_class_atoms;
    std::map<std::pair<uint32_t, std::u16string>, uint32_t> window_properties;
    std::map<uint32_t, GlMap> gl_maps;
    std::vector<Timer> timers;
    uint32_t next_timer_id = 1;
    uint32_t window_class_serial = 0xC000u, gl_handle_serial = 0xE0000000u, window_handle_serial = 0xD0000000u, user_handle_serial = 0xC0000000u, gl_map_next = 0x70000000u;
    uint32_t vertex_array = 0, gl_array_buffer = 0, gl_element_array_buffer = 0;
    std::function<uintptr_t(bool, uint32_t, uintptr_t*)> d3d9_factory;
    std::function<uintptr_t(const void*, uintptr_t*)> dsound_factory;
    std::function<uintptr_t(const char*)> dsound_export;
    std::function<uintptr_t(uint32_t)> dsound_event;
    static constexpr uint32_t DSOUND_WAIT_UNHANDLED = 0xfffffffeu;
    std::function<uint32_t(Cpu&, uint32_t, uint32_t)> dsound_wait;
    std::function<uintptr_t(uint32_t)> d3d9_window;
    ComSpace com;
    uint32_t d3d9_guest_top = 0x7D000000u;
    std::map<std::pair<uint32_t, uint32_t>, std::tuple<void*, uint32_t, uint32_t>> d3d9_locks;
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
    r.add("user32.dll", "EnumDisplayMonitors", Conv::Stdcall, 4, [&h, count](Cpu& c) -> uint32_t {
        count("EnumDisplayMonitors");
        constexpr uint32_t monitor = 0xCE000001u;
        int width = 1280, height = 720;
        if (h.system_metrics) { h.system_metrics(0, width); h.system_metrics(1, height); }
#if defined(_WIN32)
        else {
            using Fn = int (WINAPI*)(int);
            static HMODULE user = LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto fn = user ? reinterpret_cast<Fn>(GetProcAddress(user, "GetSystemMetrics")) : nullptr;
            if (fn) { width = fn(0); height = fn(1); }
        }
#endif
        uint32_t clip = c.arg(1), callback = c.arg(2), data = c.arg(3);
        if (clip && (int32_t(c.mem.r32(clip + 8)) <= 0 || int32_t(c.mem.r32(clip + 12)) <= 0)) return 1;
        c.reg[ESP] -= 64; const uint32_t rect = c.reg[ESP];  // the RECT stays above the callback frame, as in Win32
        c.mem.w32(rect, 0); c.mem.w32(rect + 4, 0);
        c.mem.w32(rect + 8, uint32_t(width)); c.mem.w32(rect + 12, uint32_t(height));
        const bool ok = !callback || c.call(callback, {monitor, 0, rect, data});
        c.reg[ESP] += 64;
        return ok ? 1 : 0;
    });
    auto enum_display_settings = [&h](Cpu& c) -> uint32_t {
        if (c.arg(0)) {
            const char16_t name[] = u"\\\\.\\DISPLAY1";
            for (size_t i = 0; i < sizeof(name) / sizeof(name[0]); ++i)
                if (c.mem.r(c.arg(0) + uint32_t(i * 2), 2) != name[i]) return 0;
        }
        if (c.arg(1) != 0xFFFFFFFFu || !c.arg(2) || c.mem.r(c.arg(2) + 68, 2) < 220) return 0;
        int width = 1280, height = 720;
        if (h.system_metrics) { h.system_metrics(0, width); h.system_metrics(1, height); }
#if defined(_WIN32)
        else {
            using Fn = int (WINAPI*)(int);
            static HMODULE user = LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto fn = user ? reinterpret_cast<Fn>(GetProcAddress(user, "GetSystemMetrics")) : nullptr;
            if (fn) { width = fn(0); height = fn(1); }
        }
#endif
        uint8_t mode[220]{};
        c.mem.write_bytes(c.arg(2), mode, sizeof(mode));
        c.mem.w(c.arg(2) + 68, 220, 2); c.mem.w32(c.arg(2) + 72, 0x005c0000u);
        c.mem.w32(c.arg(2) + 168, 32); c.mem.w32(c.arg(2) + 172, uint32_t(width));
        c.mem.w32(c.arg(2) + 176, uint32_t(height)); c.mem.w32(c.arg(2) + 184, 60); return 1;
    };
    r.add("user32.dll", "EnumDisplaySettingsW", Conv::Stdcall, 3, [count, enum_display_settings](Cpu& c) -> uint32_t {
        count("EnumDisplaySettingsW"); return enum_display_settings(c);
    });
    r.add("user32.dll", "EnumDisplaySettingsExW", Conv::Stdcall, 4, [count, enum_display_settings](Cpu& c) -> uint32_t {
        count("EnumDisplaySettingsExW"); return enum_display_settings(c);
    });
    r.add("gdi32.dll", "CreateDCW", Conv::Stdcall, 4, [count](Cpu& c) -> uint32_t {
        count("CreateDCW"); c.mem.w32(c.fs_base + 0x34, 120); return 0;
    });
    r.add("user32.dll", "EnumDisplayDevicesW", Conv::Stdcall, 4, [count](Cpu& c) -> uint32_t {
        count("EnumDisplayDevicesW");
        const char16_t device[] = u"\\\\.\\DISPLAY1";
        if (c.arg(0)) for (size_t i = 0; i < sizeof(device) / sizeof(device[0]); ++i)
            if (c.mem.r(c.arg(0) + uint32_t(i * 2), 2) != device[i]) return 0;
        uint32_t out = c.arg(2);
        if (c.arg(1) || !out || c.mem.r32(out) < 840) return 0;
        const char16_t name[] = u"\\\\.\\DISPLAY1", description[] = u"xboxnative virtual display";
        for (size_t i = 0; i < sizeof(name) / sizeof(name[0]); ++i) c.mem.w(out + 4 + uint32_t(i * 2), name[i], 2);
        for (size_t i = 0; i < sizeof(description) / sizeof(description[0]); ++i) c.mem.w(out + 68 + uint32_t(i * 2), description[i], 2);
        c.mem.w32(out + 324, 1); return 1;
    });
    r.add("user32.dll", "GetMonitorInfoW", Conv::Stdcall, 2, [&h, count](Cpu& c) -> uint32_t {
        count("GetMonitorInfoW");
        uint32_t monitor = c.arg(0), info = c.arg(1);
        if (monitor != 0xCE000001u || !info || c.mem.r32(info) < 40) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }
        uint32_t info_size = c.mem.r32(info);
        int width = 1280, height = 720;
        if (h.system_metrics) { h.system_metrics(0, width); h.system_metrics(1, height); }
#if defined(_WIN32)
        else {
            using Fn = int (WINAPI*)(int);
            static HMODULE user = LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto fn = user ? reinterpret_cast<Fn>(GetProcAddress(user, "GetSystemMetrics")) : nullptr;
            if (fn) { width = fn(0); height = fn(1); }
        }
#endif
        c.mem.w32(info + 4, 0); c.mem.w32(info + 8, 0);
        c.mem.w32(info + 12, uint32_t(width)); c.mem.w32(info + 16, uint32_t(height));
        c.mem.w32(info + 20, 0); c.mem.w32(info + 24, 0);
        c.mem.w32(info + 28, uint32_t(width)); c.mem.w32(info + 32, uint32_t(height));
        c.mem.w32(info + 36, 1);
        if (info_size >= 104) {
            const char16_t name[] = u"\\\\.\\DISPLAY1";
            for (size_t i = 0; i < sizeof(name) / sizeof(name[0]); ++i) c.mem.w(info + 40 + uint32_t(i * 2), name[i], 2);
        }
        return 1;
    });
    r.add("user32.dll", "MonitorFromWindow", Conv::Stdcall, 2, [count](Cpu&) -> uint32_t {
        count("MonitorFromWindow"); return 0xCE000001u;
    });
    r.add("user32.dll", "MonitorFromPoint", Conv::Stdcall, 3, [count](Cpu&) -> uint32_t {
        count("MonitorFromPoint"); return 0xCE000001u;
    });
    r.add("user32.dll", "MonitorFromRect", Conv::Stdcall, 2, [count](Cpu&) -> uint32_t {
        count("MonitorFromRect"); return 0xCE000001u;
    });
    r.add("user32.dll", "MapVirtualKeyW", Conv::Stdcall, 2, [count](Cpu& c) -> uint32_t {
        count("MapVirtualKeyW"); uint32_t code = c.arg(0), type = c.arg(1);
#if defined(_WIN32)
        using Fn = UINT (WINAPI*)(UINT, UINT);
        static HMODULE user = LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto fn = user ? reinterpret_cast<Fn>(GetProcAddress(user, "MapVirtualKeyW")) : nullptr;
        if (fn) return fn(code, type);
#endif
        if (type == 0 || type == 4) {
            static const uint8_t letters[] = {0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};
            if (code >= 'A' && code <= 'Z') return letters[code - 'A'];
            if (code >= '1' && code <= '9') return code - '1' + 2;
            if (code == '0') return 0x0b;
            switch (code) { case 0x08: return 0x0e; case 0x09: return 0x0f; case 0x0d: return 0x1c; case 0x1b: return 0x01; case 0x20: return 0x39; default: return 0; }
        }
        if (type == 2) {
            if (code >= 'A' && code <= 'Z') return code;
            if (code >= '0' && code <= '9') return code;
            if (code == 0x20) return code;
        }
        return 0;
    });
    r.add("user32.dll", "GetKeyState", Conv::Stdcall, 1, [count](Cpu& c) -> uint32_t {
        count("GetKeyState"); (void)c;
#if defined(_WIN32)
        using Fn = SHORT (WINAPI*)(int);
        static HMODULE user = LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto fn = user ? reinterpret_cast<Fn>(GetProcAddress(user, "GetKeyState")) : nullptr;
        if (fn) return uint32_t(int32_t(fn(int(c.arg(0)))));
#endif
        return 0;
    });
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
    // Routes registered earlier with a real implementation (display, keyboard and
    // monitor routes) win over this batch's fail-closed stubs.
    auto registered = [&r](const char* name) {
        std::string key(name); for (auto& ch : key) ch = char(tolower((unsigned char)ch));
        return r.map.count({"user32.dll", key}) != 0;
    };
    auto unavailable = [&r, &h, count, registered](const char* name, unsigned argc, uint32_t result = 0) {
        if (registered(name)) return;
        r.add("user32.dll", name, Conv::Stdcall, argc, [&h, count, name, result](Cpu& c) -> uint32_t {
            count(name);
            c.mem.w32(c.fs_base + 0x34, 120);  // ERROR_CALL_NOT_IMPLEMENTED: no host record operation
            auto& logged=h.calls[std::string("unsupported:")+name];if(!logged++){const std::string line=std::string("x86 user32 unsupported: ")+name+"\n";h.err+=line;if(h.tee)h.tee(line);}
            return result;
        });
    };
    auto unavailable_cdecl = [&r, &h, count](const char* dll, const char* name, unsigned argc, uint32_t result = 0) {
        if (r.map.count({dll, Registry::key_sym(name)})) return;
        r.add(dll, name, Conv::Cdecl, argc, [&h, count, name, result](Cpu& c) -> uint32_t {
            count(name);
            c.mem.w32(c.fs_base + 0x34, 120);  // ERROR_CALL_NOT_IMPLEMENTED: no host record operation
            auto& logged = h.calls[std::string("unsupported:") + name];
            if (!logged++) { const std::string line = std::string("x86 unsupported: ") + name + "\n"; h.err += line; if (h.tee) h.tee(line); }
            return result;
        });
    };
    auto unavailable_stdcall = [&r, &h, count](const char* dll, const char* name, unsigned argc, uint32_t result = 0) {
        if (r.map.count({dll, Registry::key_sym(name)})) return;
        r.add(dll, name, Conv::Stdcall, argc, [&h, count, name, result](Cpu& c) -> uint32_t {
            count(name);
            c.mem.w32(c.fs_base + 0x34, 120);  // ERROR_CALL_NOT_IMPLEMENTED: no host record operation
            auto& logged = h.calls[std::string("unsupported:") + name];
            if (!logged++) { const std::string line = std::string("x86 unsupported: ") + name + "\n"; h.err += line; if (h.tee) h.tee(line); }
            return result;
        });
    };
    auto native_window = [&h](uint32_t token) -> uintptr_t {
        if (!token) return 0;
        auto it = h.window_handles.find(token); return it == h.window_handles.end() ? 0 : it->second;
    };
    auto guest_window = [&h](uintptr_t native) -> uint32_t { if(!native)return 0;for(const auto& item:h.window_handles)if(item.second==native)return item.first;return 0; };
    auto user_handle = [&h](uintptr_t native) -> uint32_t { if(!native)return 0;for(const auto& item:h.user_handles)if(item.second==native)return item.first;if(++h.user_handle_serial<0xC0000000u)h.user_handle_serial=0xC0000000u;h.user_handles[h.user_handle_serial]=native;return h.user_handle_serial; };
    auto call_window = [&h](Cpu& c, const char* name, std::initializer_list<uintptr_t> args,
                                   const std::vector<uint8_t>& input = {}, size_t output_size = 0) {
        Host::WindowCall q{}; q.name = name; size_t i = 0; for (auto a : args) q.args[i++] = a;
        q.input = input; q.output.resize(output_size); if (h.window_call) h.window_call(q);
        if (!q.supported) {
            c.mem.w32(c.fs_base + 0x34, 120);
            auto& logged = h.calls[std::string("unsupported:") + name];
            if (!logged++) { const std::string line = std::string("x86 user32 unsupported: ") + name + "\n"; h.err += line; if (h.tee) h.tee(line); }
        }
        return q;
    };
    auto timer_now = [&h]() -> uint64_t {
        if (h.tick_count) return h.tick_count();
        return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    };
    auto post_window_message = [&h, native_window](uint32_t guest_hwnd, uint32_t message, uint32_t wp, uint32_t lp) {
        Host::WindowCall q{}; q.name = "PostMessageW"; q.args[0] = native_window(guest_hwnd);
        q.args[1] = message; q.args[2] = wp; q.args[3] = lp;
        if (h.window_call) h.window_call(q);
        return q.supported && q.result;
    };
    auto pump_timers = [&h, timer_now, native_window, post_window_message]() {
        const uint64_t now = timer_now();
        for (auto& timer : h.timers) {
            if (timer.hwnd && !native_window(timer.hwnd)) { timer.pending = false; continue; }
            // A NULL-hwnd (thread) timer has no window to post to: it is pending until
            // GetMessage/PeekMessage hands it out (thread_timer_message).
            if (!timer.pending && now >= timer.due && (!timer.hwnd || post_window_message(timer.hwnd, 0x0113u, timer.id, timer.proc))) {
                timer.pending = true;
                timer.due = now + timer.interval;
            }
        }
    };
    // Due thread timers reach the calling thread's queue as WM_TIMER with hwnd NULL, wParam the
    // timer id and lParam the TimerProc. GetMessage/PeekMessage use it only when the native queue
    // is empty, so posted messages keep their order. Filters follow GetMessage: a NULL or (HWND)-1
    // hwnd admits thread messages, any window excludes them, and a range admits WM_TIMER only if
    // it contains 0x113 (both bounds 0 means no range).
    auto thread_timer_message = [&h, timer_now](Host::WindowCall& q, uint32_t filter, uint32_t min_msg, uint32_t max_msg) {
        if (filter && filter != 0xFFFFFFFFu) return false;
        if ((min_msg || max_msg) && (0x0113u < min_msg || 0x0113u > max_msg)) return false;
        for (const auto& timer : h.timers) {
            if (timer.hwnd || !timer.pending) continue;
            q.output.assign(32, 0);
            const uint32_t fields[6] = {0x0113u, timer.id, timer.proc, uint32_t(timer_now()), 0, 0};
            memcpy(q.output.data() + 8, fields, sizeof(fields));
            q.supported = true; q.result = 1;
            return true;
        }
        return false;
    };
    auto set_timer = [&h, native_window, timer_now](Cpu& c) -> uint32_t {
        const uint32_t hwnd = c.arg(0); uint32_t id = c.arg(1);
        if (hwnd && !native_window(hwnd)) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        if (!hwnd || !id) {
            do { id = h.next_timer_id++; if (!h.next_timer_id) h.next_timer_id = 1; }
            while (!id || std::any_of(h.timers.begin(), h.timers.end(), [hwnd, id](const auto& timer) { return timer.hwnd == hwnd && timer.id == id; }));
        }
        const uint32_t interval = std::max(1u, c.arg(2));
        auto it = std::find_if(h.timers.begin(), h.timers.end(), [hwnd, id](const auto& timer) { return timer.hwnd == hwnd && timer.id == id; });
        if (it == h.timers.end()) h.timers.push_back({hwnd, id, interval, c.arg(3), timer_now() + interval, false});
        else { it->interval = interval; it->proc = c.arg(3); it->due = timer_now() + interval; it->pending = false; }
        return id;
    };
    auto kill_timer = [&h, native_window](Cpu& c) -> uint32_t {
        const uint32_t hwnd = c.arg(0); const uint32_t id = c.arg(1);
        if (hwnd && !native_window(hwnd)) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        const auto old = h.timers.size();
        h.timers.erase(std::remove_if(h.timers.begin(), h.timers.end(), [hwnd, id](const auto& timer) { return timer.hwnd == hwnd && timer.id == id; }), h.timers.end());
        return h.timers.size() != old;
    };
    auto guest_rect = [](Cpu& c, uint32_t address) { return c.mem.read_bytes(address, 16); };
    auto guest_point = [](Cpu& c, uint32_t address) { return c.mem.read_bytes(address, 8); };
    auto read_window_wstr = [](Cpu& c, uint32_t address) { std::u16string s; for(uint32_t i=0;i<32768;++i){uint16_t ch=uint16_t(c.mem.r(address+i*2,2));if(!ch)return s;s.push_back(char16_t(ch));}throw Fault(FaultKind::AccessViolation,c.eip,address,"unterminated guest UTF-16 string"); };
    auto call_wndproc = [&h](Cpu& c, uint32_t token, uint32_t message, uint32_t wp, uint32_t lp) -> uint32_t {
        auto p = h.window_procs.find(token); return p == h.window_procs.end() || !p->second ? 0 : c.call(p->second, {token, message, wp, lp});
    };
    auto window_route = [&](const char* name, unsigned argc, const char* native_name = nullptr) {
        if (registered(name)) return;
        r.add("user32.dll", name, Conv::Stdcall, argc, [&h, count, native_window, guest_window, user_handle, call_window, guest_rect, guest_point, call_wndproc, read_window_wstr, pump_timers, thread_timer_message, timer_now, name, native_name, argc](Cpu& c) -> uint32_t {
            count(name);
            const bool first_hwnd = !strcmp(name,"ShowWindow")||!strcmp(name,"UpdateWindow")||!strcmp(name,"SetWindowPos")||!strcmp(name,"MoveWindow")||!strcmp(name,"GetClientRect")||!strcmp(name,"GetWindowRect")||!strcmp(name,"ClientToScreen")||!strcmp(name,"ScreenToClient")||!strcmp(name,"SetWindowTextA")||!strcmp(name,"SetWindowTextW")||!strcmp(name,"GetWindowLongA")||!strcmp(name,"GetWindowLongW")||!strcmp(name,"SetWindowLongA")||!strcmp(name,"SetWindowLongW")||!strcmp(name,"IsWindowVisible")||!strcmp(name,"IsIconic")||!strcmp(name,"GetParent")||!strcmp(name,"SetFocus")||!strcmp(name,"SetForegroundWindow")||!strcmp(name,"PostMessageA")||!strcmp(name,"PostMessageW")||!strcmp(name,"SendMessageA")||!strcmp(name,"SendMessageW")||!strcmp(name,"DefWindowProcA")||!strcmp(name,"DefWindowProcW");
            const uint32_t token = argc&&first_hwnd ? c.arg(0) : 0; uintptr_t hwnd = native_window(token);
            auto bad_window = [&]() { c.mem.w32(c.fs_base + 0x34, 6); return uint32_t(0); };
            if (token && !hwnd) return bad_window();
            if (!strcmp(name,"GetWindowLongA") || !strcmp(name,"GetWindowLongW")) {
                int32_t index=int32_t(c.arg(1));auto& values=h.window_longs[token];if(index==-4||index==-21){auto it=values.find(index);return it==values.end()?0u:it->second;}
                auto q=call_window(c,name,{hwnd,uintptr_t(index)});return uint32_t(q.result);
            }
            if (!strcmp(name,"GetParent")) {
                auto q=call_window(c,name,{hwnd});return q.supported?guest_window(q.result):0;
            }
            if (!strcmp(name,"SetWindowLongA") || !strcmp(name,"SetWindowLongW")) {
                int32_t index=int32_t(c.arg(1));auto& values=h.window_longs[token];if(index==-4||index==-21){uint32_t old=values[index];values[index]=c.arg(2);if(index==-4)h.window_procs[token]=c.arg(2);return old;}
                auto q=call_window(c,name,{hwnd,uintptr_t(index),c.arg(2)});values[index]=c.arg(2);return uint32_t(q.result);
            }
            if (!strcmp(name,"GetForegroundWindow") || !strcmp(name,"GetFocus") || !strcmp(name,"GetActiveWindow")) {
                auto q=call_window(c,name,{});return q.supported?guest_window(q.result):0;
            }
            if (!strcmp(name,"SetFocus")) { auto q=call_window(c,name,{hwnd});return guest_window(q.result); }
            if (!strcmp(name,"SetForegroundWindow")) { auto q=call_window(c,name,{hwnd});return uint32_t(q.result); }
            if (!strcmp(name,"LoadCursorA") || !strcmp(name,"LoadCursorW") || !strcmp(name,"LoadIconA") || !strcmp(name,"LoadIconW")) {
                const bool wide=name[strlen(name)-1]=='W';uint32_t resource=c.arg(1);std::vector<uint8_t> input;
                if(resource>0xffffu){if(wide){auto s=read_window_wstr(c,resource);input.resize((s.size()+1)*2);memcpy(input.data(),s.data(),s.size()*2);}else{auto s=c.mem.read_cstr(resource,32768);input.assign(s.begin(),s.end());input.push_back(0);}}
                const char* op=native_name?native_name:name;auto q=call_window(c,op,{c.arg(0),resource>0xffffu?0u:resource},input);return q.supported?user_handle(q.result):0;
            }
            if (!strcmp(name,"SetCursor")) { uint32_t t=c.arg(0);uintptr_t native=0;if(t){auto it=h.user_handles.find(t);if(it==h.user_handles.end()){c.mem.w32(c.fs_base+0x34,6);return 0;}native=it->second;}auto q=call_window(c,name,{native});return q.supported?user_handle(q.result):0; }
            if (!strcmp(name,"PostQuitMessage")) { auto q = call_window(c,name,{c.arg(0)}); return uint32_t(q.result); }
            if (!strcmp(name,"SendMessageA") || !strcmp(name,"SendMessageW")) return call_wndproc(c,token,c.arg(1),c.arg(2),c.arg(3));
            if (!strcmp(name,"DefWindowProcA") || !strcmp(name,"DefWindowProcW")) {
                auto q=call_window(c,name,{hwnd,c.arg(1),c.arg(2),c.arg(3)}); return uint32_t(q.result);
            }
            if (!strcmp(name,"PeekMessageA") || !strcmp(name,"PeekMessageW") || !strcmp(name,"GetMessageA") || !strcmp(name,"GetMessageW")) {
                const bool get = name[0]=='G'; const uint32_t out=c.arg(0), filter=c.arg(1); uintptr_t native_filter=filter==0xFFFFFFFFu?static_cast<uintptr_t>(-1):native_window(filter);
                if(filter&&!native_filter)return uint32_t(-1);
                const size_t size=32; Host::WindowCall q{};
                if (get && h.window_call) {
                    const char* peek_name = !strcmp(name,"GetMessageA") ? "PeekMessageA" : "PeekMessageW";
                    for (;;) {
                        pump_timers();
                        q = Host::WindowCall{}; q.name = peek_name; q.args[0] = out; q.args[1] = native_filter;
                        q.args[2] = c.arg(2); q.args[3] = c.arg(3); q.args[4] = 0; q.output.resize(size);
                        h.window_call(q);
                        if (!q.supported) break;
                        if (!q.result && thread_timer_message(q, filter, c.arg(2), c.arg(3))) break;
                        if (q.result) {
                            q = call_window(c,name,{uintptr_t(out),native_filter,c.arg(2),c.arg(3),1u},{},size);
                            break;
                        }
                        if (h.timers.empty()) { q = call_window(c,name,{uintptr_t(out),native_filter,c.arg(2),c.arg(3),0u},{},size); break; }
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    if (!q.supported) q = call_window(c,name,{uintptr_t(out),native_filter,c.arg(2),c.arg(3),0u},{},size);
                } else {
                    pump_timers();
                    q = call_window(c,name,{uintptr_t(out),native_filter,c.arg(2),c.arg(3),get?0u:c.arg(4)}, {},size);
                    if (q.supported && !q.result) thread_timer_message(q, filter, c.arg(2), c.arg(3));
                }
                const bool quit=get&&q.supported&&!q.result&&q.output.size()>=size&&[&]{uint32_t m=0;memcpy(&m,q.output.data()+8,4);return m==0x0012u;}();
                if(q.supported && (q.result || quit) && q.output.size()>=size) {
                    uintptr_t native_hwnd=0;uint32_t guest_hwnd=0;memcpy(&native_hwnd,q.output.data(),sizeof(native_hwnd));
                    for(const auto& item:h.window_handles)if(item.second==native_hwnd){guest_hwnd=item.first;break;}
                    std::vector<uint8_t> guest(28);memcpy(guest.data(),&guest_hwnd,4);memcpy(guest.data()+4,q.output.data()+8,24);c.mem.write_bytes(out,guest.data(),uint32_t(guest.size()));
                    const uint32_t message=c.mem.r32(out+4), id=c.mem.r32(out+8), proc=c.mem.r32(out+12);
                    if ((get || (c.arg(4)&1)) && message==0x0113u)
                        for(auto& timer:h.timers)if(timer.hwnd==guest_hwnd&&timer.id==id&&timer.proc==proc){timer.pending=false;break;}
                    if (get && message==0x0012u) return 0;
                }
                return uint32_t(q.result);
            }
            if (!strcmp(name,"DispatchMessageA") || !strcmp(name,"DispatchMessageW")) {
                const uint32_t address=c.arg(0), w=c.mem.r32(address), msg=c.mem.r32(address+4), wp=c.mem.r32(address+8), lp=c.mem.r32(address+12);
                if(msg==0x0012u)return 0;
                if(msg==0x0113u && lp) { c.call(lp,{w,msg,wp,uint32_t(timer_now())}); return 0; }
                return call_wndproc(c,w,msg,wp,lp);
            }
            if (!strcmp(name,"TranslateMessage")) {
                const uint32_t a=c.arg(0); auto q=call_window(c,name,{native_window(c.mem.r32(a)),c.mem.r32(a+4),c.mem.r32(a+8),c.mem.r32(a+12)}); return uint32_t(q.result);
            }
            uintptr_t a[8]{}; for(unsigned i=0;i<argc;++i) a[i]=c.arg(i);
            if(first_hwnd) a[0]=hwnd;
            if(!strcmp(name,"SetWindowPos")&&a[1]){const uint32_t after_token=uint32_t(a[1]);if(after_token==1u||after_token==0xFFFFFFFFu||after_token==0xFFFFFFFEu)a[1]=uintptr_t(int32_t(after_token));else{uintptr_t after=native_window(after_token);if(!after)return bad_window();a[1]=after;}}
            std::vector<uint8_t> bytes; size_t out_size=0; uint32_t output_address=0;
            if(!strcmp(name,"GetClientRect")||!strcmp(name,"GetWindowRect")){out_size=16;output_address=uint32_t(a[1]);}
            else if(!strcmp(name,"ClientToScreen")||!strcmp(name,"ScreenToClient")){out_size=8;output_address=uint32_t(a[1]);bytes=guest_point(c,output_address);}
            else if(!strcmp(name,"AdjustWindowRect")||!strcmp(name,"AdjustWindowRectEx")){out_size=16;output_address=uint32_t(a[0]);bytes=guest_rect(c,output_address);a[0]=0;}
            else if(!strcmp(name,"GetCursorPos")){out_size=8;output_address=uint32_t(a[0]);a[0]=0;}
            else if(!strcmp(name,"GetClipCursor")){out_size=16;output_address=uint32_t(a[0]);a[0]=0;}
            else if(!strcmp(name,"ClipCursor")){if(a[0])bytes=guest_rect(c,uint32_t(a[0]));a[0]=0;}
            else if(!strcmp(name,"SetWindowTextA")||!strcmp(name,"SetWindowTextW")) {
                if(a[1]) { if(!strcmp(name,"SetWindowTextW")){auto s=read_window_wstr(c,uint32_t(a[1]));bytes.resize((s.size()+1)*2);memcpy(bytes.data(),s.data(),s.size()*2);}else{auto s=c.mem.read_cstr(uint32_t(a[1]),32768);bytes.assign(s.begin(),s.end());bytes.push_back(0);} }
                a[1]=0;
            }
            const char* operation=native_name?native_name:name;
            if(!strcmp(name,"GetClientRect")||!strcmp(name,"GetWindowRect")||!strcmp(name,"ClientToScreen")||!strcmp(name,"ScreenToClient")) a[1]=0;
            auto q=call_window(c,operation,{a[0],a[1],a[2],a[3],a[4],a[5],a[6]},bytes,out_size);
            if(q.supported && out_size && q.output.size()>=out_size) c.mem.write_bytes(output_address,q.output.data(),uint32_t(out_size));
            return uint32_t(q.result);
        });
    };
    window_route("ShowWindow",2); window_route("UpdateWindow",1); window_route("SetWindowPos",7); window_route("MoveWindow",6);
    window_route("GetClientRect",2); window_route("GetWindowRect",2); window_route("AdjustWindowRect",3); window_route("AdjustWindowRectEx",4);
    window_route("ClientToScreen",2); window_route("ScreenToClient",2); window_route("SetWindowTextA",2); window_route("SetWindowTextW",2);
    window_route("GetWindowLongA",2); window_route("GetWindowLongW",2); window_route("GetWindowLongPtrW",2); window_route("GetParent",1); window_route("SetWindowLongA",3); window_route("SetWindowLongW",3);
    window_route("LoadCursorA",2); window_route("LoadCursorW",2); window_route("SetCursor",1); window_route("ShowCursor",1); window_route("ClipCursor",1); window_route("GetCursorPos",1); window_route("GetClipCursor",1);
    window_route("LoadIconA",2); window_route("LoadIconW",2); window_route("GetForegroundWindow",0); window_route("SetForegroundWindow",1);
    window_route("SetFocus",1); window_route("GetFocus",0); window_route("GetActiveWindow",0); window_route("IsWindowVisible",1); window_route("IsIconic",1);
    r.add("user32.dll", "PtInRect", Conv::Stdcall, 3, [count](Cpu& c) -> uint32_t {
        const uint32_t rect = c.arg(0);
        if (!rect) return 0;
        const int32_t left = int32_t(c.mem.r32(rect)), top = int32_t(c.mem.r32(rect + 4));
        const int32_t right = int32_t(c.mem.r32(rect + 8)), bottom = int32_t(c.mem.r32(rect + 12));
        count("PtInRect");
        const int32_t x = int32_t(c.arg(1)), y = int32_t(c.arg(2));
        return x >= left && x < right && y >= top && y < bottom;
    });
    window_route("PeekMessageA",5); window_route("PeekMessageW",5); window_route("GetMessageA",4); window_route("GetMessageW",4);
    window_route("TranslateMessage",1); window_route("DispatchMessageA",1); window_route("DispatchMessageW",1); window_route("PostQuitMessage",1);
    window_route("PostMessageA",4); window_route("PostMessageW",4); window_route("SendMessageA",4); window_route("SendMessageW",4); window_route("DefWindowProcA",4); window_route("DefWindowProcW",4);
    r.add("user32.dll", "SetTimer", Conv::Stdcall, 4, [count, set_timer](Cpu& c) -> uint32_t { count("SetTimer"); return set_timer(c); });
    r.add("user32.dll", "SetCoalescableTimer", Conv::Stdcall, 5, [count, set_timer](Cpu& c) -> uint32_t { count("SetCoalescableTimer"); return set_timer(c); });
    r.add("user32.dll", "KillTimer", Conv::Stdcall, 2, [count, kill_timer](Cpu& c) -> uint32_t { count("KillTimer"); return kill_timer(c); });
    // Other frequent user32 imports in the ranked gap reports. These remain
    // explicit unsupported results until their input/output contracts can be
    // backed by the UWP record layer.
    unavailable("GetWindowTextA", 3);
    unavailable("GetWindowTextW", 3);
    unavailable("GetWindowTextLengthA", 1);
    unavailable("GetWindowTextLengthW", 1);
    unavailable("GetKeyState", 1);
    unavailable("GetAsyncKeyState", 1);
    unavailable("GetKeyboardState", 1);
    unavailable("GetKeyNameTextA", 3);
    unavailable("GetKeyNameTextW", 3);
    unavailable("GetKeyboardType", 1);
    unavailable("MapVirtualKeyA", 2);
    unavailable("MapVirtualKeyW", 2);
    unavailable("GetWindowThreadProcessId", 2);
    unavailable("GetDesktopWindow", 0);
    unavailable("GetDlgItem", 2);
    unavailable("IsWindow", 1);
    r.add("user32.dll", "AllowSetForegroundWindow", Conv::Stdcall, 1, [&h, count](Cpu& c) -> uint32_t {
        count("AllowSetForegroundWindow"); Host::WindowCall q{}; q.name="AllowSetForegroundWindow"; q.args[0]=c.arg(0);
        if (h.window_call) h.window_call(q);
        if (!q.supported) c.mem.w32(c.fs_base + 0x34, 120);
        return q.supported ? uint32_t(q.result) : 0;
    });
    unavailable("CallNextHookEx", 4);
    unavailable("DialogBoxParamW", 5, uint32_t(-1));
    unavailable("EndDialog", 2);
    unavailable("GetMonitorInfoA", 2);
    unavailable("GetMonitorInfoW", 2);
    r.add("user32.dll", "GetRawInputDeviceList", Conv::Stdcall, 3, [count](Cpu& c) -> uint32_t {
        count("GetRawInputDeviceList");
        if (!c.arg(1)) { c.mem.w32(c.fs_base + 0x34, 87); return uint32_t(-1); }
        c.mem.w32(c.arg(1), 0);  // The console has no Raw Input devices.
        return 0;
    });
    unavailable("GetRawInputData", 5, uint32_t(-1));
    unavailable("GetWindowLongPtrW", 2);
    unavailable("LoadStringW", 4);
    unavailable("MapWindowPoints", 4);
    unavailable("MonitorFromRect", 2);
    unavailable("MonitorFromWindow", 2);
    unavailable("MsgWaitForMultipleObjectsEx", 5, uint32_t(-1));
    unavailable("OffsetRect", 3);
    unavailable("BeginPaint", 2);
    unavailable("CheckDlgButton", 3);
    unavailable("CheckMenuItem", 3);
    unavailable("CheckRadioButton", 4);
    unavailable("CreateDialogParamA", 5);
    unavailable("DestroyMenu", 1);
    unavailable("DialogBoxParamA", 5, uint32_t(-1));
    unavailable("EnableWindow", 2);
    unavailable("EndPaint", 2);
    unavailable("FindWindowA", 2);
    unavailable("GetDlgCtrlID", 1);
    unavailable("GetDlgItemTextA", 4);
    unavailable("GetLastActivePopup", 1);
    unavailable("GetMenuState", 3);
    unavailable("GetSubMenu", 2);
    unavailable("IsDlgButtonChecked", 2);
    unavailable("IsWindowEnabled", 1);
    unavailable("LoadMenuA", 2);
    unavailable("PostThreadMessageA", 4);
    unavailable("SendDlgItemMessageA", 5);
    unavailable("SetDlgItemTextA", 3);
    r.add("user32.dll", "SetRect", Conv::Stdcall, 5, [count](Cpu& c) -> uint32_t {
        count("SetRect"); const uint32_t rect = c.arg(0); if (!rect) return 0;
        c.mem.w32(rect, c.arg(1)); c.mem.w32(rect + 4, c.arg(2));
        c.mem.w32(rect + 8, c.arg(3)); c.mem.w32(rect + 12, c.arg(4)); return 1;
    });
    unavailable_cdecl("user32.dll", "wsprintfA", 2);
    auto message_box = [&r, &h, count](const char* name, bool wide) {
        r.add("user32.dll", name, Conv::Stdcall, 4, [&h, count, name, wide](Cpu& c) -> uint32_t {
            count(name);
            auto read_wide = [&c](uint32_t address) {
                std::u16string text;
                if (!address) return text;
                for (uint32_t i = 0; i < 32768; ++i) {
                    uint16_t ch = uint16_t(c.mem.r(address + i * 2, 2));
                    if (!ch) return text;
                    text.push_back(char16_t(ch));
                }
                throw Fault(FaultKind::AccessViolation, c.eip, address, "unterminated guest UTF-16 string");
            };
            const uint32_t text_address = c.arg(1), title_address = c.arg(2);
            std::u16string text, title;
            if (wide) { text = read_wide(text_address); title = read_wide(title_address); }
            else {
                const std::string a = text_address ? c.mem.read_cstr(text_address, 32768) : std::string();
                const std::string b = title_address ? c.mem.read_cstr(title_address, 32768) : std::string();
                text.assign(a.begin(), a.end()); title.assign(b.begin(), b.end());
            }
            std::string line = "x86 MessageBox" + std::string(wide ? "W: " : "A: ");
            line.append(title.begin(), title.end()); line += ": "; line.append(text.begin(), text.end()); line += "\n";
            h.out += line;
            if (h.tee) h.tee(line);
            return 1;  // IDOK: Play logs rather than opening a desktop dialog.
        });
    };
    message_box("MessageBoxA", false);
    message_box("MessageBoxW", true);
    r.add("user32.dll", "GetDoubleClickTime", Conv::Stdcall, 0, [count](Cpu&) -> uint32_t {
        count("GetDoubleClickTime");
#if defined(_WIN32)
        using Fn = UINT (WINAPI*)();
        static HMODULE user = LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto fn = user ? reinterpret_cast<Fn>(GetProcAddress(user, "GetDoubleClickTime")) : nullptr;
        if (fn) return fn();
#endif
        return 500;  // Windows default double-click interval.
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
    unavailable_stdcall("gdi32.dll", "GetGlyphOutlineA", 7, uint32_t(-1));
    unavailable_stdcall("imm32.dll", "ImmAssociateContext", 2);
    unavailable_stdcall("kernel32.dll", "GetDiskFreeSpaceA", 5);
    unavailable_stdcall("kernel32.dll", "GetOverlappedResult", 4);
    unavailable_stdcall("kernel32.dll", "OpenProcess", 3);
    unavailable_stdcall("kernel32.dll", "ReadConsoleW", 5);
    unavailable_stdcall("kernel32.dll", "UnmapViewOfFile", 1);
    unavailable_stdcall("sensapi.dll", "IsNetworkAlive", 1);
}
inline void register_xinput(Registry& r, Host& h) {
    constexpr uint32_t error_bad_arguments = 160;
    const char* dlls[] = {"xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll"};
    auto call = [&h](Host::XInputCall& request) {
        if (h.xinput_call) h.xinput_call(request);
    };
    for (const char* dll : dlls) {
        auto state = [call](Host::XInputCall::Kind kind) {
            return [kind, call](Cpu& c) -> uint32_t {
                const uint32_t output = c.arg(1);
                if (!output) return 160;
                Host::XInputCall request{}; request.kind = kind; request.index = c.arg(0);
                call(request);
                if (!request.result && request.output.size() == 16) c.mem.write_bytes(output, request.output.data(), 16);
                return request.result;
            };
        };
        r.add(dll, "XInputGetState", Conv::Stdcall, 2, state(Host::XInputCall::Kind::GetState));
        r.add_ordinal(dll, 2, "XInputGetState");
        r.add(dll, "XInputGetStateEx", Conv::Stdcall, 2, state(Host::XInputCall::Kind::GetStateEx));
        if (dll != std::string("xinput9_1_0.dll")) r.add_ordinal(dll, 100, "XInputGetStateEx");
        r.add(dll, "XInputSetState", Conv::Stdcall, 2, [call](Cpu& c) -> uint32_t {
            if (!c.arg(1)) return error_bad_arguments;
            Host::XInputCall request{}; request.kind = Host::XInputCall::Kind::SetState; request.index = c.arg(0);
            request.input = c.mem.read_bytes(c.arg(1), 4); call(request); return request.result;
        });
        r.add(dll, "XInputGetCapabilities", Conv::Stdcall, 3, [call](Cpu& c) -> uint32_t {
            if (!c.arg(2)) return error_bad_arguments;
            Host::XInputCall request{}; request.kind = Host::XInputCall::Kind::GetCapabilities;
            request.index = c.arg(0); request.flags = c.arg(1); call(request);
            if (!request.result && request.output.size() == 20) c.mem.write_bytes(c.arg(2), request.output.data(), 20);
            return request.result;
        });
        r.add(dll, "XInputGetCapabilitiesEx", Conv::Stdcall, 4, [call](Cpu& c) -> uint32_t {
            if (!c.arg(3)) return error_bad_arguments;
            Host::XInputCall request{}; request.kind = Host::XInputCall::Kind::GetCapabilitiesEx;
            request.index = c.arg(1); request.flags = c.arg(2); call(request);
            if (!request.result && request.output.size() == 20) c.mem.write_bytes(c.arg(3), request.output.data(), 20);
            return request.result;
        });
        if (dll != std::string("xinput9_1_0.dll")) r.add_ordinal(dll, 108, "XInputGetCapabilitiesEx");
        r.add(dll, "XInputEnable", Conv::Stdcall, 1, [call](Cpu& c) -> uint32_t {
            Host::XInputCall request{}; request.kind = Host::XInputCall::Kind::Enable; request.flags = c.arg(0);
            call(request); return request.result;
        });
        r.add(dll, "XInputGetBatteryInformation", Conv::Stdcall, 3, [call](Cpu& c) -> uint32_t {
            if (!c.arg(2)) return error_bad_arguments;
            Host::XInputCall request{}; request.kind = Host::XInputCall::Kind::GetBatteryInformation;
            request.index = c.arg(0); request.device_type = c.arg(1); call(request);
            if (!request.result && request.output.size() == 2) c.mem.write_bytes(c.arg(2), request.output.data(), 2);
            return request.result;
        });
        r.add(dll, "XInputGetKeystroke", Conv::Stdcall, 3, [call](Cpu& c) -> uint32_t {
            if (!c.arg(2)) return error_bad_arguments;
            Host::XInputCall request{}; request.kind = Host::XInputCall::Kind::GetKeystroke;
            request.index = c.arg(0); request.flags = c.arg(1); call(request);
            if (!request.result && request.output.size() == 8) c.mem.write_bytes(c.arg(2), request.output.data(), 8);
            return request.result;
        });
        if (dll != std::string("xinput9_1_0.dll")) {
            r.add_ordinal(dll, 3, "XInputSetState"); r.add_ordinal(dll, 4, "XInputGetCapabilities"); r.add_ordinal(dll, 5, "XInputEnable");
            r.add_ordinal(dll, 7, "XInputGetBatteryInformation"); r.add_ordinal(dll, 8, "XInputGetKeystroke");
        }
    }
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
        else {
            h.gl_call(q);
            if (q.last_error) c.mem.w32(c.fs_base + 0x34, q.last_error);
            else if (q.gap) c.mem.w32(c.fs_base + 0x34, 120);
        }
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
    auto dialog_route = [&r, &h, count, read_astr](const char* name, uint32_t result) {
        r.add("user32.dll", name, Conv::Stdcall, 5, [&h, count, read_astr, name, result](Cpu& c) -> uint32_t {
            count(name); c.mem.w32(c.fs_base + 0x34, 120);
            auto& logged = h.calls[std::string("unsupported:") + name];
            if (!logged++) {
                const uint32_t template_arg = c.arg(1);
                std::string template_name;
                if (!template_arg) template_name = "<null>";
                else if (template_arg <= 0xffffu) template_name = "#" + std::to_string(template_arg);
                else { const auto value = read_astr(c, template_arg); template_name.assign(value.begin(), value.end()); }
                const std::string line = std::string("x86 user32 dialog unsupported: ") + name + " template=" + template_name + "\n";
                h.err += line; if (h.tee) h.tee(line);
            }
            return result;
        });
    };
    dialog_route("CreateDialogParamA", 0);
    dialog_route("DialogBoxParamA", uint32_t(-1));
    auto map_window = [&h](uintptr_t native) {
        if (!native) return uint32_t(0);
        if (++h.window_handle_serial < 0xD0000000u) h.window_handle_serial = 0xD0000000u;
        h.window_handles[h.window_handle_serial] = native;
        return h.window_handle_serial;
    };
    auto register_class = [&h](std::u16string name) -> uint32_t {
        if (!h.register_window_class || !h.register_window_class(name)) return 0;
        if (++h.window_class_serial > 0xffffu) h.window_class_serial = 0xc001u;
        h.window_class_atoms[h.window_class_serial] = std::move(name);
        return h.window_class_serial;
    };
    auto read_class_w = [&h, read_wstr](Cpu& c, uint32_t name) {
        if (name > 0xffffu) return read_wstr(c, name);
        auto it = h.window_class_atoms.find(name);
        return it == h.window_class_atoms.end() ? std::u16string{} : it->second;
    };
    auto read_class_a = [&h, read_astr](Cpu& c, uint32_t name) {
        if (name > 0xffffu) { auto value = read_astr(c, name); return std::u16string(value.begin(), value.end()); }
        auto it = h.window_class_atoms.find(name);
        return it == h.window_class_atoms.end() ? std::u16string{} : it->second;
    };
    r.add("user32.dll", "RegisterClassExA", Conv::Stdcall, 1, [&h, count, read_astr, register_class](Cpu& c) -> uint32_t {
        count("RegisterClassExA"); uint32_t wc = c.arg(0);
        if (c.mem.r32(wc) < 48) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }
        const uint32_t name = c.mem.r32(wc + 40);
        if (!name) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }
        auto cls = read_astr(c, name);
        uint32_t atom = register_class(cls);
        if (atom) h.window_class_procs[cls] = c.mem.r32(wc + 8);
        return atom;
    });
    r.add("user32.dll", "CreateWindowExA", Conv::Stdcall, 12, [&h, count, read_class_a, read_astr, map_window, window](Cpu& c) -> uint32_t {
        count("CreateWindowExA");
        if (!h.create_window) { c.mem.w32(c.fs_base + 0x34, 120); return 0; }
        Host::WindowCreate q{};
        q.ex_style = c.arg(0); q.class_name = read_class_a(c, c.arg(1)); if (c.arg(2)) { auto title = read_astr(c, c.arg(2)); q.title.assign(title.begin(), title.end()); }
        q.style = c.arg(3); q.x = c.arg(4); q.y = c.arg(5); q.width = c.arg(6); q.height = c.arg(7);
        q.parent = c.arg(8) == 0xfffffffdu ? uintptr_t(int32_t(c.arg(8))) : c.arg(8) ? window(c.arg(8)) : 0; q.menu = c.arg(9); q.instance = c.arg(10);
        if ((c.arg(8) && c.arg(8) != 0xfffffffdu && !q.parent) || !q.class_name.size()) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        uint32_t token = map_window(h.create_window(q));
        if (token) { h.window_procs[token] = h.window_class_procs[q.class_name]; h.window_longs[token][-4] = h.window_procs[token]; }
        return token;
    });
    r.add("user32.dll", "RegisterClassW", Conv::Stdcall, 1, [&h, count, read_wstr, register_class](Cpu& c) -> uint32_t {
        count("RegisterClassW"); uint32_t wc = c.arg(0);
        if (!c.mem.r32(wc + 36)) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }
        auto cls = read_wstr(c, c.mem.r32(wc + 36));
        uint32_t atom = register_class(cls);
        if (atom) h.window_class_procs[cls] = c.mem.r32(wc + 4);
        return atom;
    });
    r.add("user32.dll", "GetClassInfoExW", Conv::Stdcall, 3, [count, read_wstr](Cpu& c) -> uint32_t {
        count("GetClassInfoExW");
        if (!c.arg(1) || !c.arg(2) || c.mem.r32(c.arg(2)) < 48) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }
        (void)read_wstr(c, c.arg(1));
        c.mem.w32(c.fs_base + 0x34, 1411);
        return 0;
    });
    r.add("user32.dll", "UnregisterClassW", Conv::Stdcall, 2, [&h, count, read_class_w](Cpu& c) -> uint32_t {
        count("UnregisterClassW");
        if (!h.unregister_window_class || !c.arg(0)) { c.mem.w32(c.fs_base + 0x34, 120); return 0; }
        std::u16string name = read_class_w(c, c.arg(0));
        if (name.empty() || !h.unregister_window_class(name)) { c.mem.w32(c.fs_base + 0x34, 1411); return 0; }
        for (auto it = h.window_class_atoms.begin(); it != h.window_class_atoms.end(); )
            if (it->second == name) it = h.window_class_atoms.erase(it); else ++it;
        return 1;
    });
    r.add("user32.dll", "RegisterClassExW", Conv::Stdcall, 1, [&h, count, read_wstr, register_class](Cpu& c) -> uint32_t {
        count("RegisterClassExW"); uint32_t wc = c.arg(0);
        if (c.mem.r32(wc) < 48) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }
        const uint32_t name = c.mem.r32(wc + 40);
        if (!name) { c.mem.w32(c.fs_base + 0x34, 87); return 0; }
        auto cls = read_wstr(c, name);
        uint32_t atom = register_class(cls);
        if (atom) h.window_class_procs[cls] = c.mem.r32(wc + 8);
        return atom;
    });
    r.add("user32.dll", "CreateWindowExW", Conv::Stdcall, 12, [&h, count, read_class_w, read_wstr, map_window, window](Cpu& c) -> uint32_t {
        count("CreateWindowExW");
        if (!h.create_window) { c.mem.w32(c.fs_base + 0x34, 120); return 0; }
        Host::WindowCreate q{};
        q.ex_style = c.arg(0); q.class_name = read_class_w(c, c.arg(1)); if (c.arg(2)) q.title = read_wstr(c, c.arg(2));
        q.style = c.arg(3); q.x = c.arg(4); q.y = c.arg(5); q.width = c.arg(6); q.height = c.arg(7);
        q.parent = c.arg(8) == 0xfffffffdu ? uintptr_t(int32_t(c.arg(8))) : c.arg(8) ? window(c.arg(8)) : 0; q.menu = c.arg(9); q.instance = c.arg(10);
        if ((c.arg(8) && c.arg(8) != 0xfffffffdu && !q.parent) || !q.class_name.size()) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        uint32_t token = map_window(h.create_window(q));
        if (token) { h.window_procs[token] = h.window_class_procs[q.class_name]; h.window_longs[token][-4] = h.window_procs[token]; }
        return token;
    });
    r.add("user32.dll", "RegisterDeviceNotificationW", Conv::Stdcall, 3, [count](Cpu& c) -> uint32_t {
        count("RegisterDeviceNotificationW");
        return c.arg(0) && c.arg(1) ? 0xD1000001u : 0;
    });
    r.add("user32.dll", "UnregisterDeviceNotification", Conv::Stdcall, 1, [count](Cpu& c) -> uint32_t {
        count("UnregisterDeviceNotification");
        return c.arg(0) == 0xD1000001u;
    });
    r.add("user32.dll", "DestroyWindow", Conv::Stdcall, 1, [&h, count](Cpu& c) -> uint32_t {
        count("DestroyWindow"); uint32_t token = c.arg(0); auto it = h.window_handles.find(token);
        if (it == h.window_handles.end() || !h.destroy_window || !h.destroy_window(it->second)) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        h.window_handles.erase(it); h.window_procs.erase(token); h.window_longs.erase(token);
        h.timers.erase(std::remove_if(h.timers.begin(), h.timers.end(), [token](const auto& timer) { return timer.hwnd == token; }), h.timers.end());
        return 1;
    });
    auto get_window_long = [&h, count, window](Cpu& c, bool pointer) -> uint32_t {
        count(pointer ? "GetWindowLongPtrW" : "GetWindowLongW"); uint32_t token = c.arg(0); uintptr_t native = window(token);
        if (native && (int32_t(c.arg(1)) == -4 || int32_t(c.arg(1)) == -21)) { auto& values = h.window_longs[token]; auto it = values.find(int32_t(c.arg(1))); return it == values.end() ? 0u : it->second; }
        if (!native || !h.get_window_long) { c.mem.w32(c.fs_base + 0x34, native ? 120 : 6); return 0; }
        return uint32_t(h.get_window_long(native, int32_t(c.arg(1)), pointer));
    };
    auto set_window_long = [&h, count, window](Cpu& c, bool pointer) -> uint32_t {
        count("SetWindowLongPtrW"); uint32_t token = c.arg(0); uintptr_t native = window(token);
        if (native && (int32_t(c.arg(1)) == -4 || int32_t(c.arg(1)) == -21)) { const int32_t index = int32_t(c.arg(1)); auto& values = h.window_longs[token]; uint32_t old = values[index]; values[index] = c.arg(2); if (index == -4) h.window_procs[token] = c.arg(2); return old; }
        if (!native || !h.set_window_long) { c.mem.w32(c.fs_base + 0x34, native ? 120 : 6); return 0; }
        return uint32_t(h.set_window_long(native, int32_t(c.arg(1)), int32_t(c.arg(2)), pointer));
    };
    r.add("user32.dll", "GetWindowLongPtrW", Conv::Stdcall, 2, [get_window_long](Cpu& c) { return get_window_long(c, true); });
    r.add("user32.dll", "SetWindowLongPtrW", Conv::Stdcall, 3, [set_window_long](Cpu& c) { return set_window_long(c, true); });
    r.add("user32.dll", "SetPropW", Conv::Stdcall, 3, [&h, count, read_wstr, window](Cpu& c) -> uint32_t {
        count("SetPropW"); uint32_t token = c.arg(0);
        if (!window(token) || !c.arg(1)) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        h.window_properties[{token, read_wstr(c, c.arg(1))}] = c.arg(2); return 1;
    });
    r.add("user32.dll", "GetPropW", Conv::Stdcall, 2, [&h, count, read_wstr, window](Cpu& c) -> uint32_t {
        count("GetPropW"); uint32_t token = c.arg(0);
        if (!window(token) || !c.arg(1)) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        auto it = h.window_properties.find({token, read_wstr(c, c.arg(1))});
        return it == h.window_properties.end() ? 0 : it->second;
    });
    r.add("user32.dll", "RemovePropW", Conv::Stdcall, 2, [&h, count, read_wstr, window](Cpu& c) -> uint32_t {
        count("RemovePropW"); uint32_t token = c.arg(0);
        if (!window(token) || !c.arg(1)) { c.mem.w32(c.fs_base + 0x34, 6); return 0; }
        auto it = h.window_properties.find({token, read_wstr(c, c.arg(1))});
        if (it == h.window_properties.end()) return 0;
        uint32_t value = it->second; h.window_properties.erase(it); return value;
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
    r.add("opengl32.dll", "wglGetCurrentContext", Conv::Stdcall, 0, [&h, count](Cpu& c) -> uint32_t {
        count("wglGetCurrentContext"); if (!h.gl_call) { c.mem.w32(c.fs_base + 0x34, 120); return 0; }
        Host::GlCall q{}; q.name = "wglGetCurrentContext"; h.gl_call(q);
        if (q.last_error) c.mem.w32(c.fs_base + 0x34, q.last_error);
        else if (q.gap) c.mem.w32(c.fs_base + 0x34, 120);
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
                if (width > 16384 || height > 16384 || !components || (!packed_size && !component_size) || row_pixels < width ||
                    (alignment != 1 && alignment != 2 && alignment != 4 && alignment != 8)) {
                    q.gap = true; call(c, q); return 0;
                }
                const uint64_t row_stride = (raw_row + alignment - 1) & ~(uint64_t(alignment) - 1);
                const uint64_t bytes = height ? uint64_t(h.unpack_skip_rows) * row_stride + uint64_t(h.unpack_skip_pixels) * pixel_bytes + uint64_t(height - 1) * row_stride + uint64_t(width) * pixel_bytes : 0;
                if (bytes > (1u << 26)) { q.gap = true; call(c, q); return 0; }
                if (dst) q.input = c.mem.read_bytes(dst, uint32_t(bytes));
                q.args[8] = 0;
            }
            // Vendor aliases (glUniform4fvARB, glUniform4uivEXT) take the core name's arguments.
            const std::string core = name.size() > 3 && (name.compare(name.size() - 3, 3, "ARB") == 0 || name.compare(name.size() - 3, 3, "EXT") == 0) ? name.substr(0, name.size() - 3) : name;
            if (core.size() >= 10 && core.compare(0, 9, "glUniform") == 0 && core.find("Matrix") == std::string::npos && core.back() == 'v') {
                // The component count is the digit right after "glUniform", not the last digit ("glUniform4uiv").
                const uint32_t components = core[9] >= '1' && core[9] <= '4' ? uint32_t(core[9] - '0') : 0u;
                const uint32_t count = uint32_t(q.args[1]), source = uint32_t(q.args[2]);
                const uint32_t scalar_size = core.find("dv") != std::string::npos || core.find("i64") != std::string::npos ? 8u : 4u;
                uint64_t bytes = uint64_t(count) * components * scalar_size;
                if (!components || (count && !source) || count > (1u << 20) || bytes > (1u << 26)) { q.gap = true; call(c, q); return 0; }
                if (bytes) q.input = c.mem.read_bytes(source, uint32_t(bytes));
                q.args[2] = 0;
            }
            if (name.size() >= 16 && name.compare(0, 9, "glUniform") == 0 && name.find("Matrix") != std::string::npos && name.back() == 'v') {
                const size_t matrix = name.find("Matrix") + 6;
                const uint32_t rows = matrix < name.size() && name[matrix] >= '2' && name[matrix] <= '4' ? uint32_t(name[matrix] - '0') : 0u;
                const bool rectangular = matrix + 1 < name.size() && name[matrix + 1] == 'x';
                const uint32_t columns = rectangular && matrix + 2 < name.size() && name[matrix + 2] >= '2' && name[matrix + 2] <= '4' ? uint32_t(name[matrix + 2] - '0') : rows;
                const uint32_t count = uint32_t(q.args[1]), source = uint32_t(q.args[3]);
                const uint32_t scalar_size = name.find("dv") != std::string::npos ? 8u : 4u;
                uint64_t bytes = uint64_t(count) * rows * columns * scalar_size;
                if (!rows || !columns || (count && !source) || count > (1u << 20) || bytes > (1u << 26)) { q.gap = true; call(c, q); return 0; }
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
    r.add("opengl32.dll", "wglGetCurrentDC", Conv::Stdcall, 0, [&h, count](Cpu& c) -> uint32_t {
        count("wglGetCurrentDC"); if (!h.gl_call) { c.mem.w32(c.fs_base + 0x34, 120); return 0; }
        Host::GlCall q{}; q.name = "wglGetCurrentDC"; h.gl_call(q);
        if (q.last_error) c.mem.w32(c.fs_base + 0x34, q.last_error);
        else if (q.gap) c.mem.w32(c.fs_base + 0x34, 120);
        for (const auto& item : h.dc_handles) if (item.second == q.result) return item.first;
        return 0;
    });
}
inline void register_ws2(Registry& r, Host& h) {
    auto startup=[&h](Cpu& c)->uint32_t {
        Host::SocketCall q{};q.kind=Host::SocketCall::Kind::Startup;q.version=uint16_t(c.arg(0));
        if(!h.socket_call){c.mem.w32(c.fs_base+0x34,10093);return 10093;}h.socket_call(q);
        if(q.result==0){if(q.data.size()!=400)throw Fault(FaultKind::AccessViolation,c.eip,c.arg(1),"WSAStartup host returned malformed WSADATA");c.mem.write_bytes(c.arg(1),q.data.data(),400);}
        if(q.result&&q.last_error)c.mem.w32(c.fs_base+0x34,q.last_error);
        return uint32_t(q.result);
    };
    auto cleanup=[&h](Cpu& c)->uint32_t{Host::SocketCall q{};q.kind=Host::SocketCall::Kind::Cleanup;if(h.socket_call)h.socket_call(q);if(q.result&&q.last_error)c.mem.w32(c.fs_base+0x34,q.last_error);return uint32_t(q.result);};
    // Winsock errors live in the thread's last-error slot, the same one the failing routes write (unsupported stubs too).
    auto last_error=[](Cpu& c)->uint32_t{return c.mem.r32(c.fs_base+0x34);};
    r.add("ws2_32.dll","WSAStartup",Conv::Stdcall,2,startup);r.add("ws2_32.dll","#115",Conv::Stdcall,2,startup);
    r.add("ws2_32.dll","WSACleanup",Conv::Stdcall,0,cleanup);r.add("ws2_32.dll","#116",Conv::Stdcall,0,cleanup);
    r.add("ws2_32.dll","WSAGetLastError",Conv::Stdcall,0,last_error);r.add("ws2_32.dll","#111",Conv::Stdcall,0,last_error);
    auto unavailable=[&r](const char* name,unsigned argc,uint32_t result){
        r.add("ws2_32.dll",name,Conv::Stdcall,argc, [result](Cpu& c)->uint32_t { c.mem.w32(c.fs_base+0x34,10045); return result; });
    };
    unavailable("accept",3,uint32_t(-1));r.add_ordinal("ws2_32.dll",1,"accept");
    unavailable("bind",3,uint32_t(-1));r.add_ordinal("ws2_32.dll",2,"bind");
    unavailable("closesocket",1,uint32_t(-1));r.add_ordinal("ws2_32.dll",3,"closesocket");
    unavailable("connect",3,uint32_t(-1));r.add_ordinal("ws2_32.dll",4,"connect");
    unavailable("inet_addr",1,uint32_t(-1));r.add_ordinal("ws2_32.dll",11,"inet_addr");
    unavailable("inet_ntoa",1,0);r.add_ordinal("ws2_32.dll",12,"inet_ntoa");
    unavailable("listen",2,uint32_t(-1));r.add_ordinal("ws2_32.dll",13,"listen");
    unavailable("recv",4,uint32_t(-1));r.add_ordinal("ws2_32.dll",16,"recv");
    unavailable("recvfrom",6,uint32_t(-1));r.add_ordinal("ws2_32.dll",17,"recvfrom");
    unavailable("send",4,uint32_t(-1));r.add_ordinal("ws2_32.dll",19,"send");
    unavailable("sendto",6,uint32_t(-1));r.add_ordinal("ws2_32.dll",20,"sendto");
    unavailable("shutdown",2,uint32_t(-1));r.add_ordinal("ws2_32.dll",22,"shutdown");
    unavailable("socket",3,uint32_t(-1));r.add_ordinal("ws2_32.dll",23,"socket");
    unavailable("gethostbyaddr",3,0);r.add_ordinal("ws2_32.dll",51,"gethostbyaddr");
    unavailable("gethostbyname",1,0);r.add_ordinal("ws2_32.dll",52,"gethostbyname");
    unavailable("WSAAsyncSelect",4,uint32_t(-1));r.add_ordinal("ws2_32.dll",101,"WSAAsyncSelect");
    r.add("ws2_32.dll","htons",Conv::Stdcall,1,[](Cpu& c)->uint32_t { const uint16_t v=uint16_t(c.arg(0)); return uint32_t(uint16_t((v<<8)|(v>>8))); });
    r.add_ordinal("ws2_32.dll",9,"htons");
    r.add("ws2_32.dll","ntohs",Conv::Stdcall,1,[](Cpu& c)->uint32_t { const uint16_t v=uint16_t(c.arg(0)); return uint32_t(uint16_t((v<<8)|(v>>8))); });
    r.add_ordinal("ws2_32.dll",15,"ntohs");
}
}
