// SPDX-License-Identifier: GPL-3.0-or-later
// Run the official i686 SDL2 DLL in the x86 interpreter under Wine. SDL's
// Direct3D 9 renderer is connected to the real x64 D3D9 shim for frame readback.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#include "../host/x86/x86_play.h"

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    return file ? std::vector<uint8_t>{std::istreambuf_iterator<char>(file), {}} : std::vector<uint8_t>{};
}
static LRESULT CALLBACK test_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: x86-sdl2 <guest.exe> <SDL2.dll> <d3d9.dll>\n");
        return 2;
    }
    const auto guest = read_file(argv[1]);
    const auto sdl2 = read_file(argv[2]);
    const auto d3d9 = read_file(argv[3]);
    if (guest.empty() || sdl2.empty() || d3d9.empty()) {
        std::fprintf(stderr, "SDL2TEST DRIVER FAIL: guest or SDL2.dll is empty\n");
        return 1;
    }

    HMODULE shim = LoadLibraryA(argv[3]);
    if (!shim) {
        std::fprintf(stderr, "SDL2TEST DRIVER FAIL: D3D9 shim load error=%lu\n", GetLastError());
        return 1;
    }
    using Create9 = void* (WINAPI*)(UINT);
    using Create9Ex = HRESULT (WINAPI*)(UINT, void**);
    auto create9 = reinterpret_cast<Create9>(GetProcAddress(shim, "Direct3DCreate9"));
    auto create9ex = reinterpret_cast<Create9Ex>(GetProcAddress(shim, "Direct3DCreate9Ex"));
    if (!create9 || !create9ex) {
        std::fprintf(stderr, "SDL2TEST DRIVER FAIL: D3D9 factory export missing\n");
        return 1;
    }

    const wchar_t class_name[] = L"X86SDL2TestWindow";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = test_window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = class_name;
    RegisterClassW(&window_class);
    HWND window = CreateWindowW(class_name, L"x86 SDL2 test", WS_OVERLAPPEDWINDOW,
        0, 0, 96, 64, nullptr, nullptr, window_class.hInstance, nullptr);
    if (!window) {
        std::fprintf(stderr, "SDL2TEST DRIVER FAIL: native D3D9 window error=%lu\n", GetLastError());
        return 1;
    }

    const x86::DllSource source = [sdl2, d3d9](const std::string& name) {
        if (name == "sdl2.dll") return sdl2;
        if (name == "d3d9.dll") return d3d9;
        return std::vector<uint8_t>{};
    };
    std::string output;
    std::map<std::pair<uintptr_t, int>, intptr_t> window_longs;
    auto result = x86::run_image(guest, "x86-sdl2.exe", "x86-sdl2.exe",
        [&](const std::string& text) { output += text; }, nullptr,
        2'000'000'000ull, source, true, nullptr, nullptr,
        [&](x86::Host& host) {
            host.tee = [&](const std::string& text) { output += text; };
            host.register_window_class = [](const std::u16string&) { return true; };
            host.unregister_window_class = [](const std::u16string&) { return true; };
            host.create_window = [](const x86::Host::WindowCreate&) -> uintptr_t { return 1; };
            host.get_window_long = [&](uintptr_t hwnd, int index, bool) -> intptr_t {
                auto it = window_longs.find({hwnd, index}); return it == window_longs.end() ? 0 : it->second;
            };
            host.set_window_long = [&](uintptr_t hwnd, int index, intptr_t value, bool) -> intptr_t {
                auto key = std::make_pair(hwnd, index); intptr_t old = window_longs[key]; window_longs[key] = value; return old;
            };
            host.get_client_rect = [](uintptr_t, x86::Host::WindowRect& q) { q = {0, 0, 96, 64}; return true; };
            host.map_window_point = [](uintptr_t, int32_t&, int32_t&, bool) { return true; };
            host.adjust_window_rect = [](x86::Host::WindowRect& q, uint32_t style, bool menu, uint32_t ex) {
                RECT rect{q.left, q.top, q.right, q.bottom};
                if (!::AdjustWindowRectEx(&rect, style, menu, ex)) return false;
                q = {rect.left, rect.top, rect.right, rect.bottom}; return true;
            };
            host.window_call = [&](x86::Host::WindowCall& q) {
                q.supported = true;
                if (q.name == "AdjustWindowRectEx" && q.input.size() == 16) {
                    x86::Host::WindowRect rect{};
                    std::memcpy(&rect, q.input.data(), sizeof(rect));
                    if (!host.adjust_window_rect(rect, uint32_t(q.args[1]), q.args[2] != 0, uint32_t(q.args[3]))) {
                        q.result = 0;
                        return;
                    }
                    q.output.resize(16);
                    std::memcpy(q.output.data(), &rect, sizeof(rect));
                    q.result = 1;
                } else if (q.name == "GetClientRect") {
                    x86::Host::WindowRect rect{0, 0, 96, 64};
                    q.output.resize(16);
                    std::memcpy(q.output.data(), &rect, sizeof(rect));
                    q.result = 1;
                } else if (q.name == "ClientToScreen" || q.name == "ScreenToClient") {
                    q.result = 1;
                } else if (q.name == "GetWindowLongW" || q.name == "GetWindowLongPtrW") {
                    auto it = window_longs.find({q.args[0], int(q.args[1])});
                    q.result = it == window_longs.end() ? 0 : uintptr_t(it->second);
                } else if (q.name == "SetWindowLongW" || q.name == "SetWindowLongPtrW") {
                    auto key = std::make_pair(q.args[0], int(q.args[1]));
                    q.result = uintptr_t(window_longs[key]);
                    window_longs[key] = intptr_t(q.args[2]);
                } else if (q.name == "PeekMessageW") {
                    q.result = 0;
                } else if (q.name == "GetFocus" || q.name == "GetActiveWindow" || q.name == "GetForegroundWindow") {
                    q.result = 0;
                } else if (q.name == "SetWindowPos" || q.name == "SetWindowTextW") {
                    q.result = 1;
                } else {
                    q.supported = false;
                }
            };
            host.d3d9_factory = [=](bool ex, uint32_t version, uintptr_t* out) -> uintptr_t {
                if (!out) return uint32_t(E_POINTER);
                *out = 0;
                if (ex) {
                    void* object = nullptr;
                    HRESULT hr = create9ex(version, &object);
                    *out = reinterpret_cast<uintptr_t>(object);
                    return uint32_t(hr);
                }
                void* object = create9(version);
                *out = reinterpret_cast<uintptr_t>(object);
                return 0;
            };
            host.d3d9_window = [=](uint32_t guest_window) -> uintptr_t {
                return guest_window ? reinterpret_cast<uintptr_t>(window) : 0;
            };
        });

    std::fwrite(output.data(), 1, output.size(), stdout);
    for (const auto& unresolved : result.unresolved)
        std::printf("SDL2TEST UNRESOLVED %s\n", unresolved.c_str());
    if (!result.fault.empty())
        std::printf("SDL2TEST FAULT kind=%s eip=0x%08x %s\n",
            result.fault_kind.c_str(), result.fault_eip, result.fault.c_str());
    std::printf("SDL2TEST RESULT loaded=%d finished=%d exit=%u imports=%u bound=%u\n",
        result.loaded, result.finished, result.exit_code, result.imports, result.bound);

    DestroyWindow(window);
    FreeLibrary(shim);
    return result.loaded && result.finished && result.exit_code == 42 &&
        output.find("SDL2TEST PASS ") != std::string::npos ? 0 : 1;
}
