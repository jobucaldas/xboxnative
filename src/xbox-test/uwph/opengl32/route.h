#pragma once
#include <windows.h>
#include <cstring>
#include <cstdint>

namespace opengl32_core {
// No guest OpenGL context can be created or bound by this backend. Wine 10
// opengl32/wgl.c returns NULL for both queries when glCurrentRC is NULL.
// Never inspect the host TEB: a host graphics context is not a guest context.
// Replace these queries with guest thread state when context support is added.
inline HGLRC WINAPI get_current_context() { return nullptr; }
inline HDC WINAPI get_current_dc() { return nullptr; }
// Wine 10 wgl.c/unix_wgl.c: null names and lookup without an active
// context return NULL without changing last error. No driver is available
// for guest extension dispatch, so do not expose host OpenGL entry points.
inline PROC WINAPI get_proc_address(LPCSTR) { return nullptr; }
// No OpenGL-capable guest DC is registered. Opaque user32 DC tokens have no
// GDI surface and must never be forwarded to host OpenGL. Wine 10
// unix_private.h get_dc_funcs rejects an unrecognized DC with this error.
inline HGLRC WINAPI create_context(HDC) {
    SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
}
// Wine 10 unix_wgl.c: no context handles exist in this backend.
inline BOOL WINAPI delete_context(HGLRC) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}
inline BOOL WINAPI make_current(HDC dc, HGLRC context) {
    // With no previous context, Wine accepts a non-null DC and null context
    // without inspecting the DC. This does not create or bind a context.
    if (!context && dc) return TRUE;
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}
inline uintptr_t resolve(const char* dll, const char* name) {
    if (!dll || !name || _stricmp(dll, "opengl32.dll")) return 0;
    if (!strcmp(name, "wglGetCurrentContext")) return reinterpret_cast<uintptr_t>(&get_current_context);
    if (!strcmp(name, "wglGetCurrentDC")) return reinterpret_cast<uintptr_t>(&get_current_dc);
    if (!strcmp(name, "wglGetProcAddress")) return reinterpret_cast<uintptr_t>(&get_proc_address);
    if (!strcmp(name, "wglCreateContext")) return reinterpret_cast<uintptr_t>(&create_context);
    if (!strcmp(name, "wglDeleteContext")) return reinterpret_cast<uintptr_t>(&delete_context);
    if (!strcmp(name, "wglMakeCurrent")) return reinterpret_cast<uintptr_t>(&make_current);
    return 0;
}
}
