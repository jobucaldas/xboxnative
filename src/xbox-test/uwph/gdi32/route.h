#pragma once
#include <windows.h>
#include <cstring>
#include <cstdint>
#include <atomic>
#include "pixel_format.h"
#include "objects.h"
#include "device_caps.h"

namespace gdi32_core {
// Display gamma: nothing adjusts the guest display's gamma, so the current ramp
// is the identity (Windows' default, i * 257 per 8-bit step). The presenter
// cannot apply a ramp, so setting one fails as on a display without gamma
// control; SDL treats both as optional. Guest DC values are not passed to host GDI.
inline BOOL WINAPI get_device_gamma_ramp(HDC dc, void* ramp) {
    if (!dc || !ramp) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    auto words = static_cast<WORD*>(ramp);
    for (int channel = 0; channel < 3; ++channel)
        for (int i = 0; i < 256; ++i) words[channel * 256 + i] = WORD(i * 257);
    return TRUE;
}
inline BOOL WINAPI set_device_gamma_ramp(HDC dc, void* ramp) {
    SetLastError(!dc || !ramp ? ERROR_INVALID_PARAMETER : ERROR_NOT_SUPPORTED);
    return FALSE;
}
using DeviceCaps = int (WINAPI*)(HDC, int);
using Metrics = int (WINAPI*)(int);
// GetDeviceCaps: this registry's display and memory DCs (CreateDCW "DISPLAY",
// CreateCompatibleDC) describe the same virtual display as user32 window DCs,
// sized by user32's screen metrics; SDL2 reads its desktop mode this way. Any
// other handle goes to the user32 record callback, which validates window DCs.
inline std::atomic<DeviceCaps> window_device_caps{nullptr};
inline std::atomic<Metrics> screen_metrics{nullptr};
inline int WINAPI get_device_caps(HDC dc, int cap) {
    // Classify under the GDI lock, but call user32 only after releasing it:
    // user32 runs window procedures under its own lock, which may call GDI.
    enum { None, Ours, Other } owner = None;
    {
        std::lock_guard<std::recursive_mutex> l(objectsM);
        if (auto o = any(dc)) owner = (o->kind == Kind::DisplayDc || o->kind == Kind::MemoryDc) ? Ours : Other;
    }
    if (owner != None) {
        auto metrics = screen_metrics.load();
        if (owner == Other || !metrics) { SetLastError(ERROR_INVALID_HANDLE); return 0; }
        return display_cap(cap, metrics(0), metrics(1));  // SM_CXSCREEN, SM_CYSCREEN
    }
    auto window = window_device_caps.load();
    return window ? window(dc, cap) : 0;
}
inline uintptr_t resolve(const char* dll, const char* name, DeviceCaps device_caps = nullptr, Metrics metrics = nullptr) {
    if (!dll || !name || _stricmp(dll, "gdi32.dll")) return 0;
    if (!strcmp(name, "GetObjectA")) return reinterpret_cast<uintptr_t>(&get_object_a);
    if (!strcmp(name, "GetObjectW")) return reinterpret_cast<uintptr_t>(&get_object_w);
    if (!strcmp(name, "ChoosePixelFormat")) return reinterpret_cast<uintptr_t>(&choose_pixel_format);
    if (!strcmp(name, "SetPixelFormat")) return reinterpret_cast<uintptr_t>(&set_pixel_format);
    if (!strcmp(name, "SwapBuffers")) return reinterpret_cast<uintptr_t>(&swap_buffers);
    if (!strcmp(name, "GetDeviceCaps")) {
        if (!device_caps) return 0;
        window_device_caps.store(device_caps);
        if (metrics) screen_metrics.store(metrics);
        return reinterpret_cast<uintptr_t>(&get_device_caps);
    }
    static const struct { const char* name; uintptr_t address; } objectApis[] = {
        {"CreateBitmap", reinterpret_cast<uintptr_t>(&create_bitmap)},
        {"CreateCompatibleBitmap", reinterpret_cast<uintptr_t>(&create_compatible_bitmap)},
        {"CreateDIBSection", reinterpret_cast<uintptr_t>(&create_dib_section)},
        {"GetDIBits", reinterpret_cast<uintptr_t>(&get_dibits)},
        {"CreateCompatibleDC", reinterpret_cast<uintptr_t>(&create_compatible_dc)},
        {"CreateDCW", reinterpret_cast<uintptr_t>(&create_dc_w)},
        {"DeleteDC", reinterpret_cast<uintptr_t>(&delete_dc)},
        {"SelectObject", reinterpret_cast<uintptr_t>(&select_object)},
        {"DeleteObject", reinterpret_cast<uintptr_t>(&delete_object)},
        {"SetPixel", reinterpret_cast<uintptr_t>(&set_pixel)},
        {"BitBlt", reinterpret_cast<uintptr_t>(&bit_blt)},
        {"CreateSolidBrush", reinterpret_cast<uintptr_t>(&create_solid_brush)},
        {"GetStockObject", reinterpret_cast<uintptr_t>(&get_stock_object)},
        {"CreateFontIndirectW", reinterpret_cast<uintptr_t>(&create_font_indirect_w)},
        {"GetTextMetricsW", reinterpret_cast<uintptr_t>(&get_text_metrics_w)},
        {"GetTextExtentPoint32A", reinterpret_cast<uintptr_t>(&get_text_extent_point_32a)},
        {"GetTextExtentPoint32W", reinterpret_cast<uintptr_t>(&get_text_extent_point_32w)},
        {"CreateRectRgn", reinterpret_cast<uintptr_t>(&create_rect_rgn)},
        {"CombineRgn", reinterpret_cast<uintptr_t>(&combine_rgn)},
        {"GetPixelFormat", reinterpret_cast<uintptr_t>(&get_pixel_format)},
        {"DescribePixelFormat", reinterpret_cast<uintptr_t>(&describe_pixel_format)},
        {"StretchDIBits", reinterpret_cast<uintptr_t>(&stretch_dibits)},
        {"GetICMProfileW", reinterpret_cast<uintptr_t>(&get_icm_profile_w)},
        {"GetDeviceGammaRamp", reinterpret_cast<uintptr_t>(&get_device_gamma_ramp)},
        {"SetDeviceGammaRamp", reinterpret_cast<uintptr_t>(&set_device_gamma_ramp)},
    };
    for (auto& api : objectApis) if (!strcmp(name, api.name)) return api.address;
    return 0;
}
}
