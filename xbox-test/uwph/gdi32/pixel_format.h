#pragma once
#include <windows.h>

namespace gdi32_core {
// Wine 10 opengl32/wgl.c wglChoosePixelFormat returns zero before scoring
// the descriptor when wglDescribePixelFormat reports no driver formats.
// Preserve last error, as get_pixel_formats does on its failure path.
inline int WINAPI choose_pixel_format(HDC, const PIXELFORMATDESCRIPTOR*) {
    return 0;
}
// No OpenGL display driver is installed in this backend. Match Wine 10
// opengl32/unix_thunks.c null_wglSetPixelFormat/null_wglSwapBuffers: fail
// without reading the descriptor, changing last error, or touching host DCs.
// Do not claim a configured pixel format or a presented frame.
inline BOOL WINAPI set_pixel_format(HDC, int, const PIXELFORMATDESCRIPTOR*) {
    return FALSE;
}
inline BOOL WINAPI swap_buffers(HDC) { return FALSE; }
}
