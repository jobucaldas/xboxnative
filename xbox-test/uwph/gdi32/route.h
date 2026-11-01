#pragma once
#include <windows.h>
#include <cstring>
#include <cstdint>
#include "pixel_format.h"

namespace gdi32_core {
// This backend creates no GDI objects. Wine 10 gdi32/objects.c GetObjectA
// delegates non-font handles to NtGdiExtGetObjectW (win32u/gdiobj.c), which
// returns zero for an unknown handle without touching output or last error.
// Do not query host GDI handles: guest values can alias unrelated objects.
// Replace this route when a guest GDI object registry is introduced.
inline int WINAPI get_object_a(HGDIOBJ, int, void*) { return 0; }
using DeviceCaps = int (WINAPI*)(HDC, int);
inline uintptr_t resolve(const char* dll, const char* name, DeviceCaps device_caps = nullptr) {
    if (!dll || !name || _stricmp(dll, "gdi32.dll")) return 0;
    if (!strcmp(name, "GetObjectA")) return reinterpret_cast<uintptr_t>(&get_object_a);
    if (!strcmp(name, "ChoosePixelFormat")) return reinterpret_cast<uintptr_t>(&choose_pixel_format);
    if (!strcmp(name, "SetPixelFormat")) return reinterpret_cast<uintptr_t>(&set_pixel_format);
    if (!strcmp(name, "SwapBuffers")) return reinterpret_cast<uintptr_t>(&swap_buffers);
    if (!strcmp(name, "GetDeviceCaps")) return reinterpret_cast<uintptr_t>(device_caps);
    return 0;
}
}
