// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that runs the OpenGL startup path an SDL3 GL game
// takes (issue #176): LoadLibrary("opengl32.dll") — served by Play
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
// On a 3.0+-capable driver the fixture also runs the core 3.3 pipeline a modern
// GL game takes (issue #176): a wglCreateContextAttribsARB
// core-profile context, a VBO/VAO triangle through a linked GLSL program and an
// FBO render-to-texture, each with a readback pixel as the verdict. GL 1.1
// entry points (glDrawArrays, glGenTextures, glBindTexture, glTexImage2D,
// glTexParameteri, glDeleteTextures) resolve from opengl32.dll's exports;
// everything 2.0+ comes from wglGetProcAddress with the current context. When
// the driver caps below 3.0 the core stages are reported skipped and the GL 1.1
// verdict above still decides the run.
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
// No CRT: a substring test for the extension string.
static bool contains(const char* hay, const char* needle) {
    if (!hay || !needle) return false;
    for (; *hay; ++hay) {
        const char* h = hay; const char* nd = needle;
        while (*h && *nd && *h == *nd) { ++h; ++nd; }
        if (!*nd) return true;
    }
    return false;
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
typedef HANDLE_GL (WINAPI* PFN_wglCreateContextAttribsARB)(HDC, HANDLE_GL, const int*);
typedef const char* (WINAPI* PFN_glGetString)(uint32_t);
typedef void (WINAPI* PFN_glClearColor)(float, float, float, float);
typedef void (WINAPI* PFN_glClear)(uint32_t);
typedef void (WINAPI* PFN_glViewport)(int, int, int, int);
typedef void (WINAPI* PFN_glReadPixels)(int, int, int, int, uint32_t, uint32_t, void*);
typedef void (WINAPI* PFN_glFinish)(void);
typedef BOOL (WINAPI* PFN_wglSwapIntervalEXT)(int);
typedef int (WINAPI* PFN_wglGetSwapIntervalEXT)(void);
// WGL_ARB_pixel_format / WGL_ARB_extensions_string.
typedef const char* (WINAPI* PFN_wglGetExtensionsStringARB)(HDC);
typedef BOOL (WINAPI* PFN_wglGetPixelFormatAttribivARB)(HDC, int, int, uint32_t, const int*, int*);
typedef BOOL (WINAPI* PFN_wglChoosePixelFormatARB)(HDC, const int*, const FLOAT*, uint32_t, int*, uint32_t*);
// Core 3.x pipeline (only reachable through wglGetProcAddress on a 3.0+ context).
typedef void (WINAPI* PFN_glGenBuffers)(int, uint32_t*);
typedef void (WINAPI* PFN_glBindBuffer)(uint32_t, uint32_t);
typedef void (WINAPI* PFN_glBufferData)(uint32_t, intptr_t, const void*, uint32_t);
typedef void (WINAPI* PFN_glGenVertexArrays)(int, uint32_t*);
typedef void (WINAPI* PFN_glBindVertexArray)(uint32_t);
typedef void (WINAPI* PFN_glVertexAttribPointer)(uint32_t, int, uint32_t, uint8_t, int, const void*);
typedef void (WINAPI* PFN_glEnableVertexAttribArray)(uint32_t);
typedef uint32_t (WINAPI* PFN_glCreateShader)(uint32_t);
typedef void (WINAPI* PFN_glShaderSource)(uint32_t, int, const char* const*, const int*);
typedef void (WINAPI* PFN_glCompileShader)(uint32_t);
typedef void (WINAPI* PFN_glGetShaderiv)(uint32_t, uint32_t, int*);
typedef void (WINAPI* PFN_glGetShaderInfoLog)(uint32_t, int, int*, char*);
typedef uint32_t (WINAPI* PFN_glCreateProgram)(void);
typedef void (WINAPI* PFN_glAttachShader)(uint32_t, uint32_t);
typedef void (WINAPI* PFN_glLinkProgram)(uint32_t);
typedef void (WINAPI* PFN_glGetProgramiv)(uint32_t, uint32_t, int*);
typedef void (WINAPI* PFN_glGetProgramInfoLog)(uint32_t, int, int*, char*);
typedef void (WINAPI* PFN_glDeleteShader)(uint32_t);
typedef void (WINAPI* PFN_glUseProgram)(uint32_t);
typedef int (WINAPI* PFN_glGetUniformLocation)(uint32_t, const char*);
typedef void (WINAPI* PFN_glUniform4f)(int, float, float, float, float);
typedef void (WINAPI* PFN_glDrawArrays)(uint32_t, int, int);
typedef void (WINAPI* PFN_glGenFramebuffers)(int, uint32_t*);
typedef void (WINAPI* PFN_glBindFramebuffer)(uint32_t, uint32_t);
typedef void (WINAPI* PFN_glFramebufferTexture)(uint32_t, uint32_t, uint32_t, int);
typedef uint32_t (WINAPI* PFN_glCheckFramebufferStatus)(uint32_t);
typedef void (WINAPI* PFN_glGenTextures)(int, uint32_t*);
typedef void (WINAPI* PFN_glBindTexture)(uint32_t, uint32_t);
typedef void (WINAPI* PFN_glTexImage2D)(uint32_t, int, int, int, int, int, uint32_t, uint32_t, const void*);
typedef void (WINAPI* PFN_glTexParameteri)(uint32_t, uint32_t, int);
typedef void (WINAPI* PFN_glDeleteFramebuffers)(int, const uint32_t*);
typedef void (WINAPI* PFN_glDeleteBuffers)(int, const uint32_t*);
typedef void (WINAPI* PFN_glDeleteVertexArrays)(int, const uint32_t*);
typedef void (WINAPI* PFN_glDeleteTextures)(int, const uint32_t*);
typedef void (WINAPI* PFN_glDeleteProgram)(uint32_t);
typedef void* (WINAPI* PFN_glMapBuffer)(uint32_t, uint32_t);
typedef BOOL (WINAPI* PFN_glUnmapBuffer)(uint32_t);

enum : uint32_t { GL_VENDOR = 0x1F00, GL_RENDERER = 0x1F01, GL_VERSION = 0x1F02,
                  GL_COLOR_BUFFER_BIT = 0x00004000 };
enum : uint32_t { GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401 };
// Core pipeline constants (GL 3.x).
enum : uint32_t { GL_ARRAY_BUFFER = 0x8892, GL_STATIC_DRAW = 0x88E4,
                  GL_FLOAT = 0x1406, GL_TRIANGLES = 0x0004,
                  GL_VERTEX_SHADER = 0x8B31, GL_FRAGMENT_SHADER = 0x8B30,
                  GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82,
                  GL_FRAMEBUFFER = 0x8D40, GL_COLOR_ATTACHMENT0 = 0x8CE0,
                  GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE_MIN_FILTER = 0x2801,
                  GL_NEAREST = 0x2600, GL_FRAMEBUFFER_COMPLETE = 0x8CD5,
                  GL_RGBA8 = 0x8058, GL_TEXTURE_WRAP_S = 0x2802, GL_CLAMP = 0x2900 };
// WGL_ARB_pixel_format attribute codes (WGL_use_* prefixes renamed: these are
// the values SDL3/GLFW pass in the IAttributes array).
enum : int { WGL_NUMBER_PIXEL_FORMATS_ARB = 0x2000,
             WGL_SUPPORT_OPENGL_ARB = 0x2010,
             WGL_DOUBLE_BUFFER_ARB = 0x2011,
             WGL_COLOR_BITS_ARB = 0x2014,
             WGL_PIXEL_TYPE_ARB = 0x2013,
             WGL_SAMPLE_BUFFERS_ARB = 0x2041,
             WGL_SAMPLES_ARB = 0x2042 };
// WGL_TYPE_RGBA_ARB: the pixel type a desktop GL game asks for.
enum : int { WGL_TYPE_RGBA_ARB = 0x202B };
// PBO readback (GL 2.1+): GL_PIXEL_PACK_BUFFER_BINDING target for glReadPixels.
enum : uint32_t { GL_PIXEL_PACK_BUFFER = 0x88EB,
                  GL_READ_ONLY = 0x88B8,
                  GL_BUFFER_SIZE = 0x8764 };

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

    // Version refusal (the Fields of Mistria report): a request the driver
    // cannot satisfy must fail with a real error. A host whose D3D12 refuses
    // R9G9B9E5_SHAREDEXP caps Mesa at GL 2.1, so when the reported version is
    // below 3.0 a compat 3.0 request is refused by the state tracker;
    // wglCreateContextAttribsARB must then return NULL with GetLastError() set
    // — patches/mesa-src/0010 maps the state tracker's
    // ST_CONTEXT_ERROR_BAD_VERSION to ERROR_INVALID_VERSION_ARB, and without it
    // the error is 0, which sent SDL3's "No matching GL pixel format available"
    // investigation down the wrong path. The stage always reports: err=N when
    // the refusal ran, "skipped version=X.Y" when the driver provides 3.0+
    // (nothing to refuse — the console's d3d12 reports GL 4.1).
    if (version && version[0] >= '0' && version[0] <= '9') {
        int major = version[0] - '0';
        if (major < 3) {
            auto createAttribs = (PFN_wglCreateContextAttribsARB)(uintptr_t)
                wglGetProcAddress("wglCreateContextAttribsARB");
            if (!createAttribs) return fail(17, "wglCreateContextAttribsARB", 0);
            int attrs[] = {0x2091, 3, 0x2092, 0, 0};   // WGL_CONTEXT_MAJOR_VERSION_ARB/minor
            SetLastError(0);
            HANDLE_GL refused = createAttribs(dc, nullptr, attrs);
            DWORD err = GetLastError();
            {char line[160]; char* p = append(line, "PLAY-OPENGL STAGE version-refusal err=");
             p = appendInt(p, (int32_t)err); p = append(p, "\n"); *p = 0; emit(line);}
            if (refused) return fail(17, "version-refusal: 3.0 succeeded under a 2.1 driver", 0);
            if (!err) return fail(17, "version-refusal: NULL with error 0 (SetLastError lost)", 0);
        } else {
            {char line[160]; char* p = append(line, "PLAY-OPENGL STAGE version-refusal skipped version=");
             p = append(p, version); p = append(p, "\n"); *p = 0; emit(line);}
        }
    }

    // Core 3.3 pipeline (the profile a modern GL game creates): a
    // wglCreateContextAttribsARB request with a core profile, then a VBO/VAO
    // draw through a linked GLSL program whose verdict is a readback pixel,
    // then an FBO render-to-texture readback. On a host whose driver caps
    // below 3.0 (the d3d12 driver on Wine 11's vkd3d caps at 2.1 for lack of
    // R9G9B9E5_SHAREDEXP) every stage below reports "skipped" and the fixture
    // still passes on the GL 1.1 verdict above; on a 3.0+-capable driver each
    // stage must succeed or the run is BLOCKED, never a pass.
    bool blocked = false;
    if (version && version[0] >= '0' && version[0] <= '9' && version[0] - '0' >= 3) {
        auto createAttribs = (PFN_wglCreateContextAttribsARB)(uintptr_t)
            wglGetProcAddress("wglCreateContextAttribsARB");
        if (!createAttribs) return fail(18, "wglCreateContextAttribsARB (3.0+ driver)", 0);
        // WGL_CONTEXT_MAJOR_VERSION_ARB=3, MINOR=3,
        // WGL_CONTEXT_PROFILE_MASK_ARB=0x9126, WGL_CONTEXT_CORE_PROFILE_BIT_ARB=1
        int coreAttrs[] = {0x2091, 3, 0x2092, 3, 0x9126, 1, 0};
        SetLastError(0);
        HANDLE_GL core = createAttribs(dc, nullptr, coreAttrs);
        if (!core) return fail(18, "core 3.3 context", GetLastError());
        if (!wglMakeCurrent(dc, core)) return fail(18, "core 3.3 make-current", GetLastError());
        {char line[96]; char* p = append(line, "PLAY-OPENGL STAGE core-context\n"); *p = 0; emit(line);}

        auto coreLookup = [&wglGetProcAddress](const char* name) -> uintptr_t {
            return (uintptr_t)(void*)wglGetProcAddress(name);
        };
        // GL 1.1 entry points are exported from opengl32.dll itself;
        // wglGetProcAddress legitimately answers NULL for them.
        auto modLookup = [&gl](const char* name) -> uintptr_t {
            return (uintptr_t)(void*)GetProcAddress(gl, name);
        };
        auto glGenBuffers = (PFN_glGenBuffers)coreLookup("glGenBuffers");
        auto glBindBuffer = (PFN_glBindBuffer)coreLookup("glBindBuffer");
        auto glBufferData = (PFN_glBufferData)coreLookup("glBufferData");
        auto glGenVertexArrays = (PFN_glGenVertexArrays)coreLookup("glGenVertexArrays");
        auto glBindVertexArray = (PFN_glBindVertexArray)coreLookup("glBindVertexArray");
        auto glVertexAttribPointer = (PFN_glVertexAttribPointer)coreLookup("glVertexAttribPointer");
        auto glEnableVertexAttribArray = (PFN_glEnableVertexAttribArray)coreLookup("glEnableVertexAttribArray");
        auto glCreateShader = (PFN_glCreateShader)coreLookup("glCreateShader");
        auto glShaderSource = (PFN_glShaderSource)coreLookup("glShaderSource");
        auto glCompileShader = (PFN_glCompileShader)coreLookup("glCompileShader");
        auto glGetShaderiv = (PFN_glGetShaderiv)coreLookup("glGetShaderiv");
        auto glGetShaderInfoLog = (PFN_glGetShaderInfoLog)coreLookup("glGetShaderInfoLog");
        auto glCreateProgram = (PFN_glCreateProgram)coreLookup("glCreateProgram");
        auto glAttachShader = (PFN_glAttachShader)coreLookup("glAttachShader");
        auto glLinkProgram = (PFN_glLinkProgram)coreLookup("glLinkProgram");
        auto glGetProgramiv = (PFN_glGetProgramiv)coreLookup("glGetProgramiv");
        auto glGetProgramInfoLog = (PFN_glGetProgramInfoLog)coreLookup("glGetProgramInfoLog");
        auto glDeleteShader = (PFN_glDeleteShader)coreLookup("glDeleteShader");
        auto glUseProgram = (PFN_glUseProgram)coreLookup("glUseProgram");
        auto glGetUniformLocation = (PFN_glGetUniformLocation)coreLookup("glGetUniformLocation");
        auto glUniform4f = (PFN_glUniform4f)coreLookup("glUniform4f");
        auto glDrawArrays = (PFN_glDrawArrays)modLookup("glDrawArrays");
        auto glGenFramebuffers = (PFN_glGenFramebuffers)coreLookup("glGenFramebuffers");
        auto glBindFramebuffer = (PFN_glBindFramebuffer)coreLookup("glBindFramebuffer");
        auto glFramebufferTexture = (PFN_glFramebufferTexture)coreLookup("glFramebufferTexture");
        auto glCheckFramebufferStatus = (PFN_glCheckFramebufferStatus)coreLookup("glCheckFramebufferStatus");
        auto glGenTextures = (PFN_glGenTextures)modLookup("glGenTextures");
        auto glBindTexture = (PFN_glBindTexture)modLookup("glBindTexture");
        auto glTexImage2D = (PFN_glTexImage2D)modLookup("glTexImage2D");
        auto glTexParameteri = (PFN_glTexParameteri)modLookup("glTexParameteri");
        auto glDeleteFramebuffers = (PFN_glDeleteFramebuffers)coreLookup("glDeleteFramebuffers");
        auto glDeleteBuffers = (PFN_glDeleteBuffers)coreLookup("glDeleteBuffers");
        auto glDeleteVertexArrays = (PFN_glDeleteVertexArrays)coreLookup("glDeleteVertexArrays");
        auto glDeleteTextures = (PFN_glDeleteTextures)modLookup("glDeleteTextures");
        auto glDeleteProgram = (PFN_glDeleteProgram)coreLookup("glDeleteProgram");
        if (!glGenBuffers || !glBindBuffer || !glBufferData || !glGenVertexArrays ||
            !glBindVertexArray || !glVertexAttribPointer || !glEnableVertexAttribArray ||
            !glCreateShader || !glShaderSource || !glCompileShader || !glGetShaderiv ||
            !glGetShaderInfoLog || !glCreateProgram || !glAttachShader || !glLinkProgram ||
            !glGetProgramiv || !glGetProgramInfoLog || !glDeleteShader || !glUseProgram ||
            !glGetUniformLocation || !glUniform4f || !glGenFramebuffers ||
            !glBindFramebuffer || !glFramebufferTexture || !glCheckFramebufferStatus ||
            !glDeleteFramebuffers || !glDeleteBuffers || !glDeleteVertexArrays ||
            !glDeleteProgram || !glDrawArrays || !glGenTextures || !glBindTexture ||
            !glTexImage2D || !glTexParameteri || !glDeleteTextures)
            return fail(18, "core 3.x entry points", 0);

        // The version the core context itself reports (a 3.3 request must not
        // silently get a lesser core context).
        auto coreGetString = (PFN_glGetString)(uintptr_t)GetProcAddress(gl, "glGetString");
        const char* coreVersion = coreGetString ? coreGetString(GL_VERSION) : "";
        {char line[160]; char* p = append(line, "PLAY-OPENGL STAGE core-version \"");
         p = append(p, coreVersion ? coreVersion : "?"); p = append(p, "\"\n"); *p = 0; emit(line);}

        // A triangle through the whole core pipeline; the single uniform tints
        // the three vertices differently so a wrong draw cannot read back as the
        // clear colour.
        const char* vsSrc =
            "#version 330\n"
            "in vec2 pos;\n"
            "uniform vec4 tint;\n"
            "out vec4 color;\n"
            "void main() {\n"
            "  color = tint;\n"
            "  gl_Position = vec4(pos, 0.0, 1.0);\n"
            "}\n";
        const char* fsSrc =
            "#version 330\n"
            "in vec4 color;\n"
            "out vec4 frag;\n"
            "void main() { frag = color; }\n";
        auto compileStage = [&](uint32_t type, const char* src, const char* what) -> uint32_t {
            uint32_t sh = glCreateShader(type);
            glShaderSource(sh, 1, &src, nullptr);
            glCompileShader(sh);
            int ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[256] = {0}; glGetShaderInfoLog(sh, 255, nullptr, log);
                char line[320]; char* p = append(line, "PLAY-OPENGL BLOCKED shader-compile ");
                p = append(p, what); p = append(p, ": "); p = append(p, log); p = append(p, "\n"); *p = 0;
                emit(line);
                return 0;
            }
            return sh;
        };
        uint32_t vs = compileStage(GL_VERTEX_SHADER, vsSrc, "vertex");
        emit("PLAY-OPENGL STAGE core-vs-created\n");
        uint32_t fs = vs ? compileStage(GL_FRAGMENT_SHADER, fsSrc, "fragment") : 0;
        emit("PLAY-OPENGL STAGE core-fs-created\n");
        uint32_t prog = 0;
        int linked = 0;
        if (vs && fs) {
            prog = glCreateProgram();
            glAttachShader(prog, vs); glAttachShader(prog, fs);
            glLinkProgram(prog);
            glGetProgramiv(prog, GL_LINK_STATUS, &linked);
            emit("PLAY-OPENGL STAGE core-link-called\n");
            if (!linked) {
                char log[256] = {0}; glGetProgramInfoLog(prog, 255, nullptr, log);
                char line[320]; char* p = append(line, "PLAY-OPENGL BLOCKED program-link: ");
                p = append(p, log); p = append(p, "\n"); *p = 0; emit(line);
            }
        }
        if (!vs || !fs || !prog || !linked) {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            if (prog) glDeleteProgram(prog);
            blocked = true;
        } else {
        glDeleteShader(vs); glDeleteShader(fs);

        {
            float verts[6] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f};
            uint32_t vbo = 0, vao = 0;
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, (intptr_t)sizeof verts, verts, GL_STATIC_DRAW);
            glVertexAttribPointer(0, 2, GL_FLOAT, 0, 2 * sizeof(float), nullptr);
            glEnableVertexAttribArray(0);
            glUseProgram(prog);
            emit("PLAY-OPENGL STAGE core-useprogram\n");
            int tintLoc = glGetUniformLocation(prog, "tint");
            if (tintLoc < 0) {
                emit("PLAY-OPENGL BLOCKED uniform-location tint\n");
                blocked = true;
            } else {
            // Purple triangle on the default framebuffer; read the centre.
            glUniform4f(tintLoc, 0.5f, 0.0f, 0.25f, 1.0f);
            glViewport(0, 0, 320, 240);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            emit("PLAY-OPENGL STAGE core-clear-done\n");
            glDrawArrays(GL_TRIANGLES, 0, 3);
            emit("PLAY-OPENGL STAGE core-draw-issued\n");
            glFinish();
            unsigned char tri[4] = {0, 0, 0, 0};
            glReadPixels(160, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, tri);
            {char line[192]; char* p = append(line, "PLAY-OPENGL STAGE core-draw got=");
             p = appendInt(p, tri[0]); *p++ = ','; p = appendInt(p, tri[1]); *p++ = ',';
             p = appendInt(p, tri[2]); *p++ = ','; p = appendInt(p, tri[3]);
             p = append(p, " want=128,0,64,255\n"); *p = 0; emit(line);}
            // 0.5f -> 128 exact, 0.25f -> 64 exact in the fixed-point conversion
            // every GL implementation uses for 8-bit targets.
            if (tri[0] != 128 || tri[1] != 0 || tri[2] != 64 || tri[3] != 255) {
                emit("PLAY-OPENGL BLOCKED core-draw-readback\n");
                blocked = true;
            }

            // FBO render-to-texture: a green clear inside a 64x64 FBO, read
            // back from the texture's pixels. Proves framebuffer objects, the
            // completeness check and a readback from a non-default framebuffer.
            uint32_t fbo = 0, tex = 0;
            glGenFramebuffers(1, &fbo);
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                emit("PLAY-OPENGL BLOCKED fbo-incomplete\n");
                blocked = true;
            } else {
                glViewport(0, 0, 64, 64);
                glClearColor(0.0f, 0.5f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                glFinish();
                unsigned char fboPix[4] = {0, 0, 0, 0};
                glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, fboPix);
                {char line[192]; char* p = append(line, "PLAY-OPENGL STAGE fbo-readback got=");
                 p = appendInt(p, fboPix[0]); *p++ = ','; p = appendInt(p, fboPix[1]); *p++ = ',';
                 p = appendInt(p, fboPix[2]); *p++ = ','; p = appendInt(p, fboPix[3]);
                 p = append(p, " want=0,128,0,255\n"); *p = 0; emit(line);}
                if (fboPix[0] != 0 || fboPix[1] != 128 || fboPix[2] != 0 || fboPix[3] != 255) {
                    emit("PLAY-OPENGL BLOCKED fbo-readback\n");
                    blocked = true;
                }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &tex);

            // PBO readback (GL 2.1+, issue #176): games use a
            // PIXEL_PACK buffer for async readbacks. Clear the default
            // framebuffer to the same blue, read into a PBO at offset 0, map
            // it and check the pixel. Proves buffer targets beyond ARRAY_BUFFER
            // and a mapped readback, not just a call that returned.
            auto glMapBuffer = (PFN_glMapBuffer)coreLookup("glMapBuffer");
            auto glUnmapBuffer = (PFN_glUnmapBuffer)coreLookup("glUnmapBuffer");
            if (!glMapBuffer || !glUnmapBuffer) {
                emit("PLAY-OPENGL BLOCKED pbo-map entry points\n");
                blocked = true;
            } else {
                uint32_t pbo = 0;
                glGenBuffers(1, &pbo);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
                glBufferData(GL_PIXEL_PACK_BUFFER, 4, nullptr, GL_STATIC_DRAW);
                glViewport(0, 0, 320, 240);
                glClearColor(0.0f, 0.25f, 0.5f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                glFinish();
                // (void)0 offset: the last argument of glReadPixels becomes a
                // byte offset into the bound PIXEL_PACK buffer.
                glReadPixels(160, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, (void*)0);
                void* map = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
                if (!map) {
                    emit("PLAY-OPENGL BLOCKED pbo-map null\n");
                    blocked = true;
                } else {
                    const unsigned char* m = (const unsigned char*)map;
                    {char line[192]; char* p = append(line, "PLAY-OPENGL STAGE pbo-readback got=");
                     p = appendInt(p, m[0]); *p++ = ','; p = appendInt(p, m[1]); *p++ = ',';
                     p = appendInt(p, m[2]); *p++ = ','; p = appendInt(p, m[3]);
                     p = append(p, " want=0,64,128,255\n"); *p = 0; emit(line);}
                    if (m[0] != 0 || m[1] != 64 || m[2] != 128 || m[3] != 255) {
                        emit("PLAY-OPENGL BLOCKED pbo-readback\n");
                        blocked = true;
                    }
                    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                }
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                glDeleteBuffers(1, &pbo);
            }

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
            glDeleteBuffers(1, &vbo);
            glDeleteVertexArrays(1, &vao);
            }   // tintLoc else
        }   // vbo/vao block
        }   // program-linked else
        glDeleteProgram(prog);

        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(core);
        // Back on the compatibility context for the present loop below.
        if (!wglMakeCurrent(dc, context)) return fail(18, "re-make compat current", GetLastError());
        emit("PLAY-OPENGL STAGE core-pipeline-done\n");
    }

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
    if (pixel[0] != 0 || pixel[1] != 64 || pixel[2] != 128) {
        blocked = true;
        char line[192]; char* p = append(line, "PLAY-OPENGL BLOCKED pixel-readback got=");
        p = appendInt(p, pixel[0]); *p++ = ','; p = appendInt(p, pixel[1]); *p++ = ',';
        p = appendInt(p, pixel[2]); *p++ = ','; p = appendInt(p, pixel[3]);
        p = append(p, " want=0,64,128,255\n"); *p = 0; emit(line);
    }

    // Swap interval (WGL_EXT_swap_control, issue #176): a
    // game's vsync call. The extension functions come from wglGetProcAddress
    // with the context current; Mesa's frontend serves them and patch 0001
    // clamps the Present interval on a CoreWindow chain. The set must succeed
    // (or the extension is absent, reported as skipped — a host without the
    // extension is a platform limit, not a pass), and the interval read back
    // through wglGetSwapIntervalEXT must equal what was set.
    {
        auto swapInterval = (PFN_wglSwapIntervalEXT)(uintptr_t)
            wglGetProcAddress("wglSwapIntervalEXT");
        auto getSwapInterval = (PFN_wglGetSwapIntervalEXT)(uintptr_t)
            wglGetProcAddress("wglGetSwapIntervalEXT");
        if (swapInterval && getSwapInterval) {
            SetLastError(0);
            if (!swapInterval(1)) return fail(19, "wglSwapIntervalEXT(1)", GetLastError());
            int got = getSwapInterval();
            {char line[160]; char* p = append(line, "PLAY-OPENGL STAGE swap-interval set=1 got=");
             p = appendInt(p, got); p = append(p, "\n"); *p = 0; emit(line);}
            if (got != 1) {
                emit("PLAY-OPENGL BLOCKED swap-interval-readback\n");
                blocked = true;
            }
        } else {
            emit("PLAY-OPENGL STAGE swap-interval skipped (WGL_EXT_swap_control absent)\n");
        }
    }

    // WGL_ARB_pixel_format (issue #176): the call SDL3 and
    // GLFW actually make. Disjoint cases, each with its own verdict:
    //   (a) wglGetExtensionsStringARB does not resolve: skipped (platform
    //       limit) with a log line — SDL3/GLFW fall back to the GDI path;
    //   (b) the string does not advertise WGL_ARB_pixel_format: skipped with
    //       that exact message ("not advertised"), same platform limit;
    //   (c) the string advertises the extension but a function pointer is
    //       NULL: a real driver bug (the string is a contract), BLOCKED
    //       (exit 43, the file's blocked verdict) naming the missing
    //       function;
    //   (d) everything resolves: query attributes and choose the format.
    {
        auto extString = (PFN_wglGetExtensionsStringARB)(uintptr_t)
            wglGetProcAddress("wglGetExtensionsStringARB");
        auto attribiv = (PFN_wglGetPixelFormatAttribivARB)(uintptr_t)
            wglGetProcAddress("wglGetPixelFormatAttribivARB");
        auto chooseARB = (PFN_wglChoosePixelFormatARB)(uintptr_t)
            wglGetProcAddress("wglChoosePixelFormatARB");
        if (!extString) {
            // No extension-string entry point at all: the ARB path cannot be
            // probed, so SDL3/GLFW fall back to the GDI pixel-format path the
            // fixture already proved above. A platform limit, skipped with a
            // log line — never a pass for the ARB stages themselves.
            emit("PLAY-OPENGL STAGE wgl-extensions skipped (WGL_ARB_extensions_string absent)\n");
        } else {
        const char* exts = extString(dc);
        if (!exts || !contains(exts, "WGL_ARB_pixel_format")) {
            emit("PLAY-OPENGL STAGE wgl-extensions skipped (WGL_ARB_pixel_format not advertised)\n");
        } else if (!attribiv || !chooseARB) {
            // The string is a contract: advertising the extension and then
            // handing back a NULL entry point is a driver bug, not a limit.
            char line[192];
            char* p = append(line, "PLAY-OPENGL BLOCKED wgl-extensions: WGL_ARB_pixel_format advertised but ");
            p = append(p, !attribiv ? "wglGetPixelFormatAttribivARB" : "wglChoosePixelFormatARB");
            p = append(p, " is NULL\n"); *p = 0; emit(line);
            blocked = true;
        } else {
                // Query the GDI-chosen format's attributes (hdc iLayerPlane 0).
                int fmt0 = getPixelFormat(dc);
                int query[] = {WGL_NUMBER_PIXEL_FORMATS_ARB, WGL_SUPPORT_OPENGL_ARB,
                               WGL_DOUBLE_BUFFER_ARB, WGL_COLOR_BITS_ARB,
                               WGL_PIXEL_TYPE_ARB, WGL_SAMPLE_BUFFERS_ARB, WGL_SAMPLES_ARB};
                int vals[7] = {0};
                if (!attribiv(dc, fmt0, 0, 7, query, vals)) {
                    emit("PLAY-OPENGL BLOCKED pixel-format-attribiv query\n");
                    blocked = true;
                } else {
                    {char line[224]; char* p = append(line, "PLAY-OPENGL STAGE pixel-format-attribs count=");
                     p = appendInt(p, vals[0]); p = append(p, " supportGL="); p = appendInt(p, vals[1]);
                     p = append(p, " doubleBuffer="); p = appendInt(p, vals[2]);
                     p = append(p, " colorBits="); p = appendInt(p, vals[3]);
                     p = append(p, " pixelType=0x"); {uint32_t v=(uint32_t)vals[4]; char hex[9]; int i=0; while(v){int d=v&15; hex[i++]=d<10?'0'+d:'a'+d-10; v>>=4;} while(i)*p++=hex[--i];}
                     p = append(p, " sampleBuffers="); p = appendInt(p, vals[5]);
                     p = append(p, " samples="); p = appendInt(p, vals[6]);
                     p = append(p, "\n"); *p = 0; emit(line);}
                    if (vals[0] < 1 || !vals[1] || !vals[2] || vals[3] < 24 || vals[4] != WGL_TYPE_RGBA_ARB) {
                        emit("PLAY-OPENGL BLOCKED pixel-format-attribs wrong\n");
                        blocked = true;
                    }
                }
                // SDL3's attribute list (SDL_windowsgl.c): double buffer,
                // support GL, RGBA pixel type, 24 colour bits. MSAA attributes
                // are only added when the game asks; report what a 4x request
                // gets so the MSAA row of the matrix has evidence.
                int iattrs[] = {WGL_SUPPORT_OPENGL_ARB, 1, WGL_DOUBLE_BUFFER_ARB, 1,
                                WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB, WGL_COLOR_BITS_ARB, 24,
                                WGL_SAMPLE_BUFFERS_ARB, 1, WGL_SAMPLES_ARB, 4, 0};
                int chosen = 0; uint32_t num = 0;
                SetLastError(0);
                if (!chooseARB(dc, iattrs, nullptr, 1, &chosen, &num) || !chosen || !num) {
                    // A 4x MSAA request may legitimately find nothing; retry
                    // without the MSAA pair, which every GL game's baseline
                    // must match.
                    int base[] = {WGL_SUPPORT_OPENGL_ARB, 1, WGL_DOUBLE_BUFFER_ARB, 1,
                                  WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB, WGL_COLOR_BITS_ARB, 24, 0};
                    if (!chooseARB(dc, base, nullptr, 1, &chosen, &num) || !chosen || !num) {
                        emit("PLAY-OPENGL BLOCKED choose-pixel-format-arb\n");
                        blocked = true;
                    } else {
                        emit("PLAY-OPENGL STAGE choose-pixel-format-arb msaa=unavailable base\n");
                    }
                } else {
                    {char line[160]; char* p = append(line, "PLAY-OPENGL STAGE choose-pixel-format-arb msaa4x format=");
                     p = appendInt(p, chosen); p = append(p, " count="); p = appendInt(p, (int32_t)num);
                     p = append(p, "\n"); *p = 0; emit(line);}
                }
            }
        }
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
}   // extern "C"
