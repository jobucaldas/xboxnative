#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>

namespace gdi32_core {
// OpenGL driver hook. With no driver the pixel-format and presentation entry points fail as Wine 10 opengl32
// does without display-driver formats (wgl.c wglChoosePixelFormat returns zero before scoring; unix_thunks.c
// null_wglSetPixelFormat/null_wglSwapBuffers fail without reading the descriptor, changing last error, or
// touching host DCs). When Play has mapped an OpenGL implementation (Mesa's opengl32.dll, from the runtime
// directory or shipped by the game) the loader installs a lookup for that module's exports, and gdi32 forwards
// to its wgl* entry points as Windows' gdi32 forwards to the installed OpenGL driver. Host DCs never reach it.
using GlLookup = uintptr_t (*)(const char* name);
inline std::atomic<GlLookup> gl_lookup{nullptr};
template <class Fn> inline Fn gl_export(const char* name) {
    auto lookup = gl_lookup.load();
    return lookup ? reinterpret_cast<Fn>(lookup(name)) : nullptr;
}
inline int WINAPI choose_pixel_format(HDC dc, const PIXELFORMATDESCRIPTOR* descriptor) {
    if (auto forward = gl_export<int (WINAPI*)(HDC, const PIXELFORMATDESCRIPTOR*)>("wglChoosePixelFormat"))
        return forward(dc, descriptor);
    return 0;
}
inline BOOL WINAPI set_pixel_format(HDC dc, int format, const PIXELFORMATDESCRIPTOR* descriptor) {
    if (auto forward = gl_export<BOOL (WINAPI*)(HDC, int, const PIXELFORMATDESCRIPTOR*)>("wglSetPixelFormat"))
        return forward(dc, format, descriptor);
    return FALSE;
}
inline BOOL WINAPI swap_buffers(HDC dc) {
    if (auto forward = gl_export<BOOL (WINAPI*)(HDC)>("wglSwapBuffers")) return forward(dc);
    return FALSE;
}
inline int WINAPI get_pixel_format(HDC dc) {
    if (auto forward = gl_export<int (WINAPI*)(HDC)>("wglGetPixelFormat")) return forward(dc);
    SetLastError(ERROR_INVALID_PIXEL_FORMAT);
    return 0;
}
inline int WINAPI describe_pixel_format(HDC dc, int format, UINT bytes, void* descriptor) {
    if (auto forward = gl_export<int (WINAPI*)(HDC, int, UINT, void*)>("wglDescribePixelFormat"))
        return forward(dc, format, bytes, descriptor);
    return 0;
}
}
