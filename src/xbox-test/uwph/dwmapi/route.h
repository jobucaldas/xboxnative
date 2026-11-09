#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

// Guest HWNDs are user32 record addresses, never desktop DWM handles. The
// borderless record backend stores physical screen coordinates with no DWM
// frame extension. Keep all geometry queries in that backend.
namespace dwmapi_core {
// The SDK hides DWMWINDOWATTRIBUTE from the UWP partition. This is the public
// ABI value of DWMWA_EXTENDED_FRAME_BOUNDS; no desktop DWM API is imported.
inline constexpr DWORD extended_frame_bounds = 9;
using IsWindow = BOOL (WINAPI*)(HWND);
using WindowRect = BOOL (WINAPI*)(HWND, RECT*);
using WindowLong = LONG_PTR (WINAPI*)(HWND, int);
inline std::atomic<IsWindow> is_window{nullptr};
inline std::atomic<WindowRect> window_rect{nullptr};
inline std::atomic<WindowLong> window_long{nullptr};
inline HRESULT validate(HWND window) {
    if (!window) return E_HANDLE;
    auto valid = is_window.load();
    if (!valid) return E_NOTIMPL;
    return valid(window) ? S_OK : E_HANDLE;
}
inline HRESULT WINAPI get_attribute(HWND window, DWORD attribute, void* output, DWORD size) {
    HRESULT result = validate(window);
    if (FAILED(result)) return result;
    if (attribute != extended_frame_bounds) return E_NOTIMPL;
    if (!output) return E_INVALIDARG;
    if (size < sizeof(RECT)) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    auto rect = window_rect.load();
    auto style = window_long.load();
    if (!rect || !style) return E_NOTIMPL;
    if (style(window, GWL_STYLE) & WS_CHILD) return E_HANDLE;
    RECT bounds{};
    if (!rect(window, &bounds)) {
        DWORD error = GetLastError();
        return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    memcpy(output, &bounds, sizeof(bounds));
    return S_OK;
}
inline HRESULT WINAPI set_attribute(HWND window, DWORD, const void*, DWORD) {
    HRESULT result = validate(window);
    // No nonclient renderer, cloak, or DWM policy state exists in this backend.
    // Wine 10's unconditional S_OK is a stub, not a supported behavior to copy.
    return FAILED(result) ? result : E_NOTIMPL;
}
// Composition is always on since Windows 8 (DwmIsCompositionEnabled cannot be
// FALSE there); answered without native DWM.
inline HRESULT WINAPI is_composition_enabled(BOOL* enabled) {
    if (!enabled) return E_INVALIDARG;
    *enabled = TRUE;
    return S_OK;
}
// No blur or frame effect exists for record windows.
inline HRESULT WINAPI enable_blur_behind(HWND window, const void* blur) {
    HRESULT result = validate(window);
    if (FAILED(result)) return result;
    return blur ? E_NOTIMPL : E_INVALIDARG;
}
// DwmFlush waits until the DWM composition queue has consumed the pending
// updates. This backend has no DWM compositor: guest presents go straight to
// the CoreWindow swapchain, which paces itself. There is nothing to flush, so
// reporting S_OK would tell the caller its frame reached a compositor that does
// not exist. Wine 10's unconditional S_OK is the same stub as its setter, not a
// behavior to copy; the console's dwmapi.dll does not export the name at all.
inline HRESULT WINAPI flush() { return E_NOTIMPL; }
// Window-free DWM queries that keep the real system export (no HWND reaches it).
inline bool native_safe(const char* name) {
    return name && !strcmp(name, "DwmGetColorizationColor");
}
inline uintptr_t resolve(const char* dll, const char* name, IsWindow valid = nullptr,
                         WindowRect rect = nullptr, WindowLong style = nullptr) {
    if (!dll || !name || _stricmp(dll, "dwmapi.dll")) return 0;
    if (valid) is_window = valid;
    if (rect) window_rect = rect;
    if (style) window_long = style;
    if (!strcmp(name, "DwmGetWindowAttribute")) return reinterpret_cast<uintptr_t>(&get_attribute);
    if (!strcmp(name, "DwmSetWindowAttribute")) return reinterpret_cast<uintptr_t>(&set_attribute);
    if (!strcmp(name, "DwmIsCompositionEnabled")) return reinterpret_cast<uintptr_t>(&is_composition_enabled);
    if (!strcmp(name, "DwmEnableBlurBehindWindow")) return reinterpret_cast<uintptr_t>(&enable_blur_behind);
    if (!strcmp(name, "DwmFlush")) return reinterpret_cast<uintptr_t>(&flush);
    return 0;
}
}
