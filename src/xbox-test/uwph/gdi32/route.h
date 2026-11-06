#pragma once
#include <windows.h>
#include <cstring>
#include <cstdint>
#include "pixel_format.h"
#include "objects.h"

namespace gdi32_core {
using DeviceCaps = int (WINAPI*)(HDC, int);
inline uintptr_t resolve(const char* dll, const char* name, DeviceCaps device_caps = nullptr) {
    if (!dll || !name || _stricmp(dll, "gdi32.dll")) return 0;
    if (!strcmp(name, "GetObjectA")) return reinterpret_cast<uintptr_t>(&get_object_a);
    if (!strcmp(name, "GetObjectW")) return reinterpret_cast<uintptr_t>(&get_object_w);
    if (!strcmp(name, "ChoosePixelFormat")) return reinterpret_cast<uintptr_t>(&choose_pixel_format);
    if (!strcmp(name, "SetPixelFormat")) return reinterpret_cast<uintptr_t>(&set_pixel_format);
    if (!strcmp(name, "SwapBuffers")) return reinterpret_cast<uintptr_t>(&swap_buffers);
    if (!strcmp(name, "GetDeviceCaps")) return reinterpret_cast<uintptr_t>(device_caps);
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
        {"CreateFontIndirectW", reinterpret_cast<uintptr_t>(&create_font_indirect_w)},
        {"GetTextMetricsW", reinterpret_cast<uintptr_t>(&get_text_metrics_w)},
        {"GetTextExtentPoint32A", reinterpret_cast<uintptr_t>(&get_text_extent_point_32a)},
        {"CreateRectRgn", reinterpret_cast<uintptr_t>(&create_rect_rgn)},
        {"CombineRgn", reinterpret_cast<uintptr_t>(&combine_rgn)},
        {"GetPixelFormat", reinterpret_cast<uintptr_t>(&get_pixel_format)},
        {"DescribePixelFormat", reinterpret_cast<uintptr_t>(&describe_pixel_format)},
        {"GetICMProfileW", reinterpret_cast<uintptr_t>(&get_icm_profile_w)},
    };
    for (auto& api : objectApis) if (!strcmp(name, api.name)) return api.address;
    return 0;
}
}
