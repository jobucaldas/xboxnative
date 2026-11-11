// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that runs the OpenGL startup path an SDL3 GL game
// takes (research/OPENGL-PLAN.md): LoadLibrary("opengl32.dll") — served by Play
// from the runtime directory with Mesa's d3d12 WGL driver — a guest window whose
// DC gets a pixel format through gdi32's forwarded ChoosePixelFormat/
// SetPixelFormat, wglCreateContext, wglMakeCurrent, a cleared frame presented
// with SwapBuffers, and a glReadPixels readback so the verdict is a rendered
// pixel, not just a call that returned.
//
// Stages are reported so a failure names the first missing piece:
//   exit 42  the cleared frame was read back correctly ("PLAY-OPENGL PASS pixel-readback")
//   exit 43  library, window, pixel format and context worked but the readback
//            did not match: BLOCKED with the observed pixels, never a pass
//   other    a stage before that failed
//
// The WGL/GL ABI is hand-declared: the fixture links /nodefaultlib (no CRT) and
// imports only what Play routes (kernel32, user32; gdi32 comes through
// LoadLibrary, as SDL3 loads it).
#include <windows.h>
#include <stdint.h>

// /nodefaultlib: memset for zero-initialised structs, stage lines through
// WriteFile and OutputDebugStringA (the console has no stdout handle; Play
// routes OutputDebugStringA to the launch probe log).
extern "C" void* memset(void* dest, int value, size_t count) {
    volatile unsigned char* p = static_cast<volatile unsigned char*>(dest);
    while (count--) *p++ = static_cast<unsigned char>(value);
    return dest;
}
static void emit(const char* text) {
    DWORD written = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, text, (DWORD)lstrlenA(text), &written, nullptr);
    OutputDebugStringA(text);
}
static char* append(char* p, const char* text) {
    while (*text) *p++ = *text++;
    return p;
}
static char* appendInt(char* p, int32_t value) {
    char digits[12]; int n = 0;
    uint32_t v = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    if (value < 0) *p++ = '-';
    do { digits[n++] = char('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = digits[--n];
    return p;
}

extern "C" {
// WGL and GL types, declared by hand (no GL headers: the fixture is a guest).
typedef void* HANDLE_GL;   // HGLRC
typedef int (WINAPI* PFN_ChoosePixelFormat)(HDC, const PIXELFORMATDESCRIPTOR*);
typedef BOOL (WINAPI* PFN_SetPixelFormat)(HDC, int, const PIXELFORMATDESCRIPTOR*);
typedef int (WINAPI* PFN_GetPixelFormat)(HDC);
typedef int (WINAPI* PFN_DescribePixelFormat)(HDC, int, UINT, PIXELFORMATDESCRIPTOR*);
typedef BOOL (WINAPI* PFN_SwapBuffers)(HDC);
typedef HANDLE_GL (WINAPI* PFN_wglCreateContext)(HDC);
typedef BOOL (WINAPI* PFN_wglDeleteContext)(HANDLE_GL);
typedef BOOL (WINAPI* PFN_wglMakeCurrent)(HDC, HANDLE_GL);
typedef PROC (WINAPI* PFN_wglGetProcAddress)(LPCSTR);
typedef const char* (WINAPI* PFN_glGetString)(uint32_t);
typedef void (WINAPI* PFN_glClearColor)(float, float, float, float);
typedef void (WINAPI* PFN_glClear)(uint32_t);
typedef void (WINAPI* PFN_glViewport)(int, int, int, int);
typedef void (WINAPI* PFN_glReadPixels)(int, int, int, int, uint32_t, uint32_t, void*);
typedef void (WINAPI* PFN_glFinish)(void);

enum : uint32_t { GL_VENDOR = 0x1F00, GL_RENDERER = 0x1F01, GL_VERSION = 0x1F02,
                  GL_COLOR_BUFFER_BIT = 0x00004000 };
enum : uint32_t { GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401 };

int opengl_anchor;
void* opengl_relocation = &opengl_anchor;

static int fail(int code, const char* stage, DWORD error) {
    char line[192]; char* p = append(line, "PLAY-OPENGL FAIL ");
    p = append(p, stage); p = append(p, " error="); p = appendInt(p, (int32_t)error);
    p = append(p, "\n"); *p = 0;
    emit(line);
    return code;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

int opengl_entry() {
    // A guest window, the way an SDL3 GL game creates one; the launcher's DXGI
    // adapter maps it to its CoreWindow for Mesa's swap chain.
    WNDCLASSEXW cls{sizeof cls};
    cls.lpfnWndProc = wndproc; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"play-opengl";
    if (!RegisterClassExW(&cls)) return fail(10, "RegisterClassExW", GetLastError());
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"play-opengl", WS_OVERLAPPEDWINDOW,
                                  0, 0, 320, 240, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window) return fail(10, "CreateWindowExW", GetLastError());

    // SDL3's SDL_GL_LoadLibrary: by name, so Play serves the runtime copy (Mesa).
    HMODULE gl = LoadLibraryW(L"opengl32.dll");
    if (!gl) return fail(11, "LoadLibraryW(opengl32.dll)", GetLastError());
    auto choosePixelFormat = (PFN_ChoosePixelFormat)(uintptr_t)GetProcAddress(gl, "wglChoosePixelFormat");
    auto setPixelFormat = (PFN_SetPixelFormat)(uintptr_t)GetProcAddress(gl, "wglSetPixelFormat");
    auto getPixelFormat = (PFN_GetPixelFormat)(uintptr_t)GetProcAddress(gl, "wglGetPixelFormat");
    auto describePixelFormat = (PFN_DescribePixelFormat)(uintptr_t)GetProcAddress(gl, "wglDescribePixelFormat");
    auto swapBuffers = (PFN_SwapBuffers)(uintptr_t)GetProcAddress(gl, "wglSwapBuffers");
    auto wglCreateContext = (PFN_wglCreateContext)(uintptr_t)GetProcAddress(gl, "wglCreateContext");
    auto wglDeleteContext = (PFN_wglDeleteContext)(uintptr_t)GetProcAddress(gl, "wglDeleteContext");
    auto wglMakeCurrent = (PFN_wglMakeCurrent)(uintptr_t)GetProcAddress(gl, "wglMakeCurrent");
    auto wglGetProcAddress = (PFN_wglGetProcAddress)(uintptr_t)GetProcAddress(gl, "wglGetProcAddress");
    if (!choosePixelFormat || !setPixelFormat || !getPixelFormat || !describePixelFormat ||
        !swapBuffers || !wglCreateContext || !wglDeleteContext || !wglMakeCurrent || !wglGetProcAddress)
        return fail(11, "wgl entry points", 0);

    // The pixel format: through the module's wgl* exports, the way SDL3 calls
    // them after its GDI fallback (SDL_windowsgl.c uses wglChoosePixelFormat).
    HDC dc = GetDC(window);
    if (!dc) return fail(12, "GetDC", GetLastError());
    PIXELFORMATDESCRIPTOR want{};
    want.nSize = sizeof want; want.nVersion = 1;
    want.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    want.iPixelType = PFD_TYPE_RGBA;
    want.cColorBits = 24; want.cDepthBits = 24; want.cStencilBits = 8;
    want.iLayerType = PFD_MAIN_PLANE;
    int format = choosePixelFormat(dc, &want);
    if (!format) return fail(13, "wglChoosePixelFormat", GetLastError());
    PIXELFORMATDESCRIPTOR got{};
    if (!describePixelFormat(dc, format, sizeof got, &got)) return fail(13, "wglDescribePixelFormat", GetLastError());
    {char line[160]; char* p = append(line, "PLAY-OPENGL STAGE pixel-format index=");
     p = appendInt(p, format); p = append(p, " color="); p = appendInt(p, got.cColorBits);
     p = append(p, " depth="); p = appendInt(p, got.cDepthBits); p = append(p, "\n"); *p = 0; emit(line);}
    if (!setPixelFormat(dc, format, &want)) return fail(13, "wglSetPixelFormat", GetLastError());
    if (getPixelFormat(dc) != format) return fail(13, "wglGetPixelFormat", GetLastError());

    HANDLE_GL context = wglCreateContext(dc);
    if (!context) return fail(14, "wglCreateContext", GetLastError());
    if (!wglMakeCurrent(dc, context)) return fail(14, "wglMakeCurrent", GetLastError());
    emit("PLAY-OPENGL STAGE context-current\n");

    // GL 1.1 entry points come from the module's exports; extensions would come
    // from wglGetProcAddress. glGetString names the driver that answered.
    auto glGetString = (PFN_glGetString)(uintptr_t)GetProcAddress(gl, "glGetString");
    auto glClearColor = (PFN_glClearColor)(uintptr_t)GetProcAddress(gl, "glClearColor");
    auto glClear = (PFN_glClear)(uintptr_t)GetProcAddress(gl, "glClear");
    auto glViewport = (PFN_glViewport)(uintptr_t)GetProcAddress(gl, "glViewport");
    auto glReadPixels = (PFN_glReadPixels)(uintptr_t)GetProcAddress(gl, "glReadPixels");
    auto glFinish = (PFN_glFinish)(uintptr_t)GetProcAddress(gl, "glFinish");
    if (!glGetString || !glClearColor || !glClear || !glViewport || !glReadPixels || !glFinish)
        return fail(15, "gl entry points", 0);
    const char* renderer = glGetString(GL_RENDERER);
    const char* version = glGetString(GL_VERSION);
    {char line[320]; char* p = append(line, "PLAY-OPENGL STAGE context-current renderer=\"");
     p = append(p, renderer ? renderer : "?"); p = append(p, "\" version=\"");
     p = append(p, version ? version : "?"); p = append(p, "\"\n"); *p = 0; emit(line);}

    // The verdict is a pixel, not a call: read the centre of the cleared frame
    // BEFORE any present — after SwapBuffers the back-buffer contents are
    // undefined (the swapchain rotates/discards buffers), so the readback must
    // happen while the cleared buffer is still bound.
    glViewport(0, 0, 320, 240);
    glClearColor(0.0f, 0.25f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    unsigned char pixel[4] = {0, 0, 0, 0};
    glReadPixels(160, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool blocked = false;
    if (pixel[0] != 0 || pixel[1] != 64 || pixel[2] != 128) {
        blocked = true;
        char line[192]; char* p = append(line, "PLAY-OPENGL BLOCKED pixel-readback got=");
        p = appendInt(p, pixel[0]); *p++ = ','; p = appendInt(p, pixel[1]); *p++ = ',';
        p = appendInt(p, pixel[2]); *p++ = ','; p = appendInt(p, pixel[3]);
        p = append(p, " want=0,64,128,255\n"); *p = 0; emit(line);
    }

    // Three presented frames, as a game's first frames: present is proven by
    // the stage line and the swap chain's hr in the log; the pixel verdict has
    // already been read back above, before any present invalidated the buffer.
    glViewport(0, 0, 320, 240);
    for (int frame = 0; frame < 3; ++frame) {
        glClearColor(0.0f, 0.25f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!swapBuffers(dc)) return fail(16, "wglSwapBuffers", GetLastError());
    }
    glFinish();
    emit("PLAY-OPENGL STAGE frame-presented\n");
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(context);
    ReleaseDC(window, dc);
    DestroyWindow(window);
    if (blocked) return 43;
    emit("PLAY-OPENGL PASS pixel-readback\n");
    return 42;
}
}
