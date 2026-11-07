#include "route.h"
#include <dwmapi.h>
static_assert(dwmapi_core::extended_frame_bounds == DWMWA_EXTENDED_FRAME_BOUNDS);
#include <cstdio>
#include <array>
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
static bool child = false;
static DWORD rect_error = ERROR_INVALID_WINDOW_HANDLE;
static bool rect_failure = false;
static HWND record = reinterpret_cast<HWND>(0x1234);
static BOOL WINAPI valid(HWND window) { return window == record; }
static BOOL WINAPI rect(HWND window, RECT* out) {
    if (window != record || rect_failure) { SetLastError(rect_error); return FALSE; }
    *out = {-50, 20, 1870, 1100}; return TRUE;
}
static LONG_PTR WINAPI style(HWND, int index) { return index == GWL_STYLE && child ? WS_CHILD : 0; }
static LRESULT CALLBACK record_proc(HWND, UINT message, WPARAM, LPARAM) {
    return message == WM_NCCREATE ? TRUE : 0;
}
int main(int argc, char** argv) {
    CHECK(!dwmapi_core::resolve(nullptr, "DwmGetWindowAttribute"));
    CHECK(!dwmapi_core::resolve("dwmapi.dll", nullptr));
    CHECK(!dwmapi_core::resolve("user32.dll", "DwmGetWindowAttribute"));
    CHECK(!dwmapi_core::resolve("dwmapi.dll", "dwmgetwindowattribute"));
    CHECK(!dwmapi_core::resolve("dwmapi.dll", "DwmFlush"));
    auto get = reinterpret_cast<decltype(&DwmGetWindowAttribute)>(
        dwmapi_core::resolve("DWMAPI.DLL", "DwmGetWindowAttribute"));
    auto set = reinterpret_cast<decltype(&DwmSetWindowAttribute)>(
        dwmapi_core::resolve("dwmapi.dll", "DwmSetWindowAttribute"));
    CHECK(get && set);
    CHECK(get(record, DWMWA_EXTENDED_FRAME_BOUNDS, nullptr, 0) == E_NOTIMPL);
    dwmapi_core::resolve("dwmapi.dll", "DwmGetWindowAttribute", valid, rect, style);
    {
        auto composition = reinterpret_cast<HRESULT (WINAPI*)(BOOL*)>(dwmapi_core::resolve("dwmapi.dll", "DwmIsCompositionEnabled"));
        auto blur = reinterpret_cast<HRESULT (WINAPI*)(HWND, const void*)>(dwmapi_core::resolve("dwmapi.dll", "DwmEnableBlurBehindWindow"));
        CHECK(composition && blur);
        BOOL enabled = FALSE;
        CHECK(composition(&enabled) == S_OK && enabled && composition(nullptr) == E_INVALIDARG);
        DWM_BLURBEHIND blurBehind{};
        CHECK(blur(record, &blurBehind) == E_NOTIMPL && blur(record, nullptr) == E_INVALIDARG);
        CHECK(blur(reinterpret_cast<HWND>(0x9999), &blurBehind) == E_HANDLE);
        CHECK(dwmapi_core::native_safe("DwmGetColorizationColor") && dwmapi_core::native_safe("DwmFlush"));
        CHECK(!dwmapi_core::native_safe("DwmExtendFrameIntoClientArea") && !dwmapi_core::resolve("dwmapi.dll", "DwmExtendFrameIntoClientArea"));
    }
    HMODULE library = LoadLibraryW(L"dwmapi.dll"); CHECK(library);
    auto oracle = reinterpret_cast<decltype(&DwmGetWindowAttribute)>(GetProcAddress(library, "DwmGetWindowAttribute"));
    CHECK(oracle);
    for (HWND bad : {HWND(nullptr), reinterpret_cast<HWND>(0x9999)}) {
        RECT a{1,2,3,4}, b = a;
        SetLastError(0xdead);
        HRESULT result = get(bad, DWMWA_EXTENDED_FRAME_BOUNDS, &a, sizeof(a));
        CHECK(result == E_HANDLE && GetLastError() == 0xdead);
        CHECK(result == oracle(bad, DWMWA_EXTENDED_FRAME_BOUNDS, &b, sizeof(b)));
        CHECK(!memcmp(&a, &b, sizeof(a)));
        CHECK(set(bad, DWMWA_NCRENDERING_POLICY, nullptr, 0) == E_HANDLE);
    }
    CHECK(get(record, DWMWA_EXTENDED_FRAME_BOUNDS, nullptr, sizeof(RECT)) == E_INVALIDARG);
    std::array<unsigned char, 32> buffer; buffer.fill(0xa5);
    for (DWORD size = 0; size < sizeof(RECT); ++size) {
        CHECK(get(record, DWMWA_EXTENDED_FRAME_BOUNDS, buffer.data(), size) == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
        for (auto byte : buffer) CHECK(byte == 0xa5);
    }
    CHECK(get(record, DWMWA_EXTENDED_FRAME_BOUNDS, buffer.data(), buffer.size()) == S_OK);
    RECT expected{-50,20,1870,1100}; CHECK(!memcmp(buffer.data(), &expected, sizeof(expected)));
    for (size_t i = sizeof(RECT); i < buffer.size(); ++i) CHECK(buffer[i] == 0xa5);
    child = true;
    CHECK(get(record, DWMWA_EXTENDED_FRAME_BOUNDS, buffer.data(), buffer.size()) == E_HANDLE);
    child = false; rect_failure = true;
    CHECK(get(record, DWMWA_EXTENDED_FRAME_BOUNDS, buffer.data(), buffer.size()) == HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE));
    rect_error = ERROR_SUCCESS;
    CHECK(get(record, DWMWA_EXTENDED_FRAME_BOUNDS, buffer.data(), buffer.size()) == E_FAIL);
    for (DWORD attribute : {DWORD(0), DWORD(DWMWA_CLOAKED), DWORD(0xffffffff)}) {
        CHECK(get(record, attribute, buffer.data(), buffer.size()) == E_NOTIMPL);
        CHECK(set(record, attribute, buffer.data(), buffer.size()) == E_NOTIMPL);
        CHECK(!memcmp(buffer.data(), &expected, sizeof(expected)));
    }
    CHECK(argc == 2);
    // Load the production packaged record DLL explicitly; native user32 must
    // never satisfy these callbacks or create a desktop window for this test.
    HMODULE records = LoadLibraryA(argv[1]); CHECK(records);
    auto register_class = reinterpret_cast<decltype(&RegisterClassExW)>(GetProcAddress(records, "RegisterClassExW"));
    auto create = reinterpret_cast<decltype(&CreateWindowExW)>(GetProcAddress(records, "CreateWindowExW"));
    auto destroy = reinterpret_cast<decltype(&DestroyWindow)>(GetProcAddress(records, "DestroyWindow"));
    auto unregister_class = reinterpret_cast<decltype(&UnregisterClassW)>(GetProcAddress(records, "UnregisterClassW"));
    auto live_valid = reinterpret_cast<dwmapi_core::IsWindow>(GetProcAddress(records, "IsWindow"));
    auto live_rect = reinterpret_cast<dwmapi_core::WindowRect>(GetProcAddress(records, "GetWindowRect"));
    auto live_style = reinterpret_cast<dwmapi_core::WindowLong>(GetProcAddress(records, "GetWindowLongPtrW"));
    CHECK(register_class && create && destroy && unregister_class && live_valid && live_rect && live_style);
    dwmapi_core::resolve("dwmapi.dll", "DwmGetWindowAttribute", live_valid, live_rect, live_style);
    WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.lpfnWndProc = record_proc;
    cls.lpszClassName = L"sysx-dwm-record"; CHECK(register_class(&cls));
    HWND live = create(0, cls.lpszClassName, L"bounds", WS_POPUP, -50, 20, 1920, 1080,
                       nullptr, nullptr, nullptr, nullptr);
    CHECK(live && live_valid(live));
    RECT actual{};
    CHECK(get(live, DWMWA_EXTENDED_FRAME_BOUNDS, &actual, sizeof(actual)) == S_OK);
    CHECK(!memcmp(&actual, &expected, sizeof(actual)));
    CHECK(set(live, DWMWA_NCRENDERING_POLICY, nullptr, 0) == E_NOTIMPL);
    CHECK(destroy(live) && !live_valid(live));
    CHECK(get(live, DWMWA_EXTENDED_FRAME_BOUNDS, &actual, sizeof(actual)) == E_HANDLE);
    CHECK(unregister_class(cls.lpszClassName, nullptr));
    FreeLibrary(records);
    FreeLibrary(library);
    puts("DWMAPI OFFDEVICE PASS: invalid HWND Wine differential; production record DLL geometry, sizes, child rejection, unsupported policy and untouched outputs");
}
