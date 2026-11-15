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
typedef BOOL (WINAPI* PFN_wglShareLists)(HANDLE_GL, HANDLE_GL);
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
typedef void (WINAPI* PFN_glCompressedTexImage2D)(uint32_t, int, uint32_t, int, int, int, int, const void*);
typedef void (WINAPI* PFN_glUniform1i)(int, int);
typedef void (WINAPI* PFN_glGenRenderbuffers)(int, uint32_t*);
typedef void (WINAPI* PFN_glBindRenderbuffer)(uint32_t, uint32_t);
typedef void (WINAPI* PFN_glRenderbufferStorageMultisample)(uint32_t, int, uint32_t, int, int);
typedef void (WINAPI* PFN_glFramebufferRenderbuffer)(uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (WINAPI* PFN_glBlitFramebuffer)(int, int, int, int, int, int, int, int, uint32_t, uint32_t);
typedef void (WINAPI* PFN_glDeleteRenderbuffers)(int, const uint32_t*);

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
enum : uint32_t { GL_COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1,
                  GL_RENDERBUFFER = 0x8D41 };

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

    // Extended issue #176 fixture stages intentionally follow every legacy
    // stage above. Each failure has a distinct guest exit code and never reaches
    // the existing pixel-readback PASS verdict.
    auto stageFailure = [&](const char* name, int code, const char* reason) -> int {
        char line[224]; char* p = append(line, "PLAY-OPENGL STAGE ");
        p = append(p, name); p = append(p, " exit=43 stage-code="); p = appendInt(p, code);
        p = append(p, " ");
        if (reason[0] == 'w' && reason[1] == 'i' && reason[2] == 'n' && reason[3] == 'e' && reason[4] == '-') {
            p = append(p, "SKIP (wine): "); p = append(p, reason + 5);
        } else {
            p = append(p, "BLOCKED "); p = append(p, reason);
        }
        p = append(p, "\n"); *p = 0;
        emit(line);
        return 43;
    };
    auto stagePass = [&](const char* name, const char* result) {
        char line[256]; char* p = append(line, "PLAY-OPENGL STAGE ");
        p = append(p, name); p = append(p, " exit=0 "); p = append(p, result);
        p = append(p, "\n"); *p = 0; emit(line);
    };
    const char* compatExtensions = glGetString(0x1F03);
    bool hasS3tc = contains(compatExtensions, "GL_EXT_texture_compression_s3tc") ||
                   contains(compatExtensions, "GL_EXT_texture_compression_dxt1");
    auto createAttribs41 = (PFN_wglCreateContextAttribsARB)(uintptr_t)
        wglGetProcAddress("wglCreateContextAttribsARB");
    if (!createAttribs41) return stageFailure("core41-draw", 23, "wine-unavailable:missing-create-context-attribs");
    int attrs41[] = {0x2091, 4, 0x2092, 1, 0x9126, 1, 0};
    HANDLE_GL core41 = createAttribs41(dc, nullptr, attrs41);
    if (!core41) return stageFailure("core41-draw", 23, "wine-unavailable:request-4.1-core-failed");
    HANDLE_GL shared = wglCreateContext(dc);
    auto shareLists = (PFN_wglShareLists)(uintptr_t)GetProcAddress(gl, "wglShareLists");
    if (!shared || !shareLists || !shareLists(core41, shared))
        return stageFailure("share-lists-draw", 20, "context-share-failed");
    if (!wglMakeCurrent(dc, core41)) return stageFailure("share-lists-draw", 20, "make-core41-current-failed");
    const char* version41 = glGetString(GL_VERSION);
    if (!version41 || version41[0] < '4' || (version41[0] == '4' && version41[2] < '1'))
        return stageFailure("core41-draw", 23, "requested-4.1-core-version-not-reported");

    auto lookup = [&](const char* name) -> uintptr_t { return (uintptr_t)(void*)wglGetProcAddress(name); };
    auto genBuffers = (PFN_glGenBuffers)lookup("glGenBuffers");
    auto bindBuffer = (PFN_glBindBuffer)lookup("glBindBuffer");
    auto bufferData = (PFN_glBufferData)lookup("glBufferData");
    auto genVAOs = (PFN_glGenVertexArrays)lookup("glGenVertexArrays");
    auto bindVAO = (PFN_glBindVertexArray)lookup("glBindVertexArray");
    auto attribPointer = (PFN_glVertexAttribPointer)lookup("glVertexAttribPointer");
    auto enableAttrib = (PFN_glEnableVertexAttribArray)lookup("glEnableVertexAttribArray");
    auto createShader = (PFN_glCreateShader)lookup("glCreateShader");
    auto shaderSource = (PFN_glShaderSource)lookup("glShaderSource");
    auto compileShader = (PFN_glCompileShader)lookup("glCompileShader");
    auto getShaderiv = (PFN_glGetShaderiv)lookup("glGetShaderiv");
    auto createProgram = (PFN_glCreateProgram)lookup("glCreateProgram");
    auto attachShader = (PFN_glAttachShader)lookup("glAttachShader");
    auto linkProgram = (PFN_glLinkProgram)lookup("glLinkProgram");
    auto getProgramiv = (PFN_glGetProgramiv)lookup("glGetProgramiv");
    auto deleteShader = (PFN_glDeleteShader)lookup("glDeleteShader");
    auto useProgram = (PFN_glUseProgram)lookup("glUseProgram");
    auto getUniform = (PFN_glGetUniformLocation)lookup("glGetUniformLocation");
    auto uniform1i = (PFN_glUniform1i)lookup("glUniform1i");
    auto drawArrays = (PFN_glDrawArrays)(uintptr_t)GetProcAddress(gl, "glDrawArrays");
    auto genTextures = (PFN_glGenTextures)(uintptr_t)GetProcAddress(gl, "glGenTextures");
    auto bindTexture = (PFN_glBindTexture)(uintptr_t)GetProcAddress(gl, "glBindTexture");
    auto texImage = (PFN_glTexImage2D)(uintptr_t)GetProcAddress(gl, "glTexImage2D");
    auto compressedTexImage = (PFN_glCompressedTexImage2D)lookup("glCompressedTexImage2D");
    auto texParameteri = (PFN_glTexParameteri)(uintptr_t)GetProcAddress(gl, "glTexParameteri");
    auto genFbos = (PFN_glGenFramebuffers)lookup("glGenFramebuffers");
    auto bindFbo = (PFN_glBindFramebuffer)lookup("glBindFramebuffer");
    auto checkFbo = (PFN_glCheckFramebufferStatus)lookup("glCheckFramebufferStatus");
    auto genRbos = (PFN_glGenRenderbuffers)lookup("glGenRenderbuffers");
    auto bindRbo = (PFN_glBindRenderbuffer)lookup("glBindRenderbuffer");
    auto msaaStorage = (PFN_glRenderbufferStorageMultisample)lookup("glRenderbufferStorageMultisample");
    auto attachRbo = (PFN_glFramebufferRenderbuffer)lookup("glFramebufferRenderbuffer");
    auto blitFbo = (PFN_glBlitFramebuffer)lookup("glBlitFramebuffer");
    auto deleteRbos = (PFN_glDeleteRenderbuffers)lookup("glDeleteRenderbuffers");
    if (!genBuffers || !bindBuffer || !bufferData || !genVAOs || !bindVAO || !attribPointer ||
        !enableAttrib || !createShader || !shaderSource || !compileShader || !getShaderiv ||
        !createProgram || !attachShader || !linkProgram || !getProgramiv || !deleteShader ||
        !useProgram || !getUniform || !uniform1i || !drawArrays || !genTextures || !bindTexture ||
        !texImage || !compressedTexImage || !texParameteri || !genFbos || !bindFbo || !checkFbo ||
        !genRbos || !bindRbo || !msaaStorage || !attachRbo || !blitFbo || !deleteRbos)
        return stageFailure("core41-draw", 23, "required-core-entry-point-missing");

    // wglShareLists is called before either context creates objects. The buffer
    // and texture are then created in the first context and consumed by a draw
    // in the second; the VAO remains local to that second context.
    float sharedVerts[] = {-1.0f,-1.0f, 3.0f,-1.0f, -1.0f,3.0f};
    uint32_t sharedVbo = 0, sharedTex = 0;
    genBuffers(1, &sharedVbo); bindBuffer(GL_ARRAY_BUFFER, sharedVbo);
    bufferData(GL_ARRAY_BUFFER, (intptr_t)sizeof sharedVerts, sharedVerts, GL_STATIC_DRAW);
    genTextures(1, &sharedTex); bindTexture(GL_TEXTURE_2D, sharedTex);
    unsigned char sharedPixel[4] = {64,128,192,255};
    texImage(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, sharedPixel);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    if (!wglMakeCurrent(dc, shared)) return stageFailure("share-lists-draw", 20, "make-shared-current-failed");
    const char* sharedVs = "#version 410 core\nlayout(location=0) in vec2 pos; out vec2 uv; void main(){uv=pos*0.5+0.5; gl_Position=vec4(pos,0,1);}";
    const char* sampleFs = "#version 410 core\nin vec2 uv; uniform sampler2D image; out vec4 frag; void main(){frag=texture(image,uv);} ";
    auto makeProgram = [&](const char* vsText, const char* fsText) -> uint32_t {
        uint32_t vs = createShader(GL_VERTEX_SHADER), fs = createShader(GL_FRAGMENT_SHADER);
        shaderSource(vs, 1, &vsText, nullptr); compileShader(vs);
        shaderSource(fs, 1, &fsText, nullptr); compileShader(fs);
        int vok = 0, fok = 0; getShaderiv(vs, GL_COMPILE_STATUS, &vok); getShaderiv(fs, GL_COMPILE_STATUS, &fok);
        if (!vok || !fok) { deleteShader(vs); deleteShader(fs); return 0; }
        uint32_t program = createProgram(); attachShader(program, vs); attachShader(program, fs); linkProgram(program);
        int linkedNow = 0; getProgramiv(program, GL_LINK_STATUS, &linkedNow);
        deleteShader(vs); deleteShader(fs); return linkedNow ? program : 0;
    };
    auto drawSample = [&](uint32_t program, uint32_t vbo, uint32_t texture, unsigned char* pixel) -> bool {
        uint32_t vao = 0; genVAOs(1, &vao); bindVAO(vao);
        bindBuffer(GL_ARRAY_BUFFER, vbo); attribPointer(0, 2, GL_FLOAT, 0, 2 * (int)sizeof(float), nullptr);
        enableAttrib(0); bindTexture(GL_TEXTURE_2D, texture); useProgram(program);
        int sampler = getUniform(program, "image"); if (sampler < 0) return false; uniform1i(sampler, 0);
        glViewport(0, 0, 320, 240); drawArrays(GL_TRIANGLES, 0, 3); glFinish();
        glReadPixels(160, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        return true;
    };
    uint32_t sampleProgram = makeProgram(sharedVs, sampleFs);
    unsigned char sharedGot[4] = {0,0,0,0};
    if (!sampleProgram || !drawSample(sampleProgram, sharedVbo, sharedTex, sharedGot) ||
        sharedGot[0] != 64 || sharedGot[1] != 128 || sharedGot[2] != 192 || sharedGot[3] != 255)
        return stageFailure("share-lists-draw", 20, "shared-buffer-texture-readback-mismatch");
    stagePass("share-lists-draw", "shared-buffer-and-texture readback=64,128,192,255");

    // BC1/DXT1 block: solid red (RGB565 endpoint 0xf800, all selectors 0).
    if (!hasS3tc) return stageFailure("bc1-sampled-readback", 21, "wine-unavailable:S3TC-extension-absent");
    uint32_t bc1 = 0; genTextures(1, &bc1); bindTexture(GL_TEXTURE_2D, bc1);
    const unsigned char redBc1[8] = {0x00,0xf8,0x00,0x00,0,0,0,0};
    compressedTexImage(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, 4, 0, 8, redBc1);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    unsigned char bc1Got[4] = {0,0,0,0};
    if (!drawSample(sampleProgram, sharedVbo, bc1, bc1Got) || bc1Got[0] < 250 || bc1Got[1] > 2 || bc1Got[2] > 2 || bc1Got[3] != 255)
        return stageFailure("bc1-sampled-readback", 21, "compressed-texture-sample-mismatch");
    stagePass("bc1-sampled-readback", "format=BC1 got=255,0,0,255");

    // Resize the guest window before a fresh default framebuffer clear/readback.
    if (!SetWindowPos(window, nullptr, 0, 0, 400, 300, SWP_NOZORDER | SWP_NOACTIVATE))
        return stageFailure("resize-framebuffer", 22, "SetWindowPos-failed");
    RECT resized{};
    if (!GetClientRect(window, &resized) || resized.right <= 0 || resized.bottom <= 0)
        return stageFailure("resize-framebuffer", 22, "empty-client-rect");
    glViewport(0, 0, resized.right, resized.bottom);
    glClearColor(0.0f, 0.5f, 0.25f, 1.0f); glClear(GL_COLOR_BUFFER_BIT); glFinish();
    unsigned char resizedGot[4] = {0,0,0,0};
    glReadPixels(resized.right/2, resized.bottom/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, resizedGot);
    if (resizedGot[0] != 0 || resizedGot[1] != 128 || resizedGot[2] != 64 || resizedGot[3] != 255)
        return stageFailure("resize-framebuffer", 22, "fresh-framebuffer-readback-mismatch");
    stagePass("resize-framebuffer", "client-size-and-readback=0,128,64,255");

    // The 4.1 core context was requested above; prove it with a shader draw and
    // sampled readback rather than treating context creation alone as coverage.
    if (!wglMakeCurrent(dc, core41)) return stageFailure("core41-draw", 23, "make-current-failed");
    unsigned char core41Got[4] = {0,0,0,0};
    if (!drawSample(sampleProgram, sharedVbo, sharedTex, core41Got) ||
        core41Got[0] != 64 || core41Got[1] != 128 || core41Got[2] != 192 || core41Got[3] != 255)
        return stageFailure("core41-draw", 23, "4.1-core-draw-readback-mismatch");
    {char line[192]; char* p = append(line, "PLAY-OPENGL STAGE core41-draw exit=0 requested=4.1 version=\"");
     p = append(p, version41); p = append(p, "\" got=64,128,192,255\n"); *p = 0; emit(line);}

    // Render to a 4x multisample renderbuffer, resolve into a single-sample
    // texture-backed FBO, then verify the resolved pixel.
    uint32_t msaaFbo = 0, resolveFbo = 0, msaaRbo = 0, resolveTex = 0;
    genFbos(1, &msaaFbo); genFbos(1, &resolveFbo); genRbos(1, &msaaRbo);
    bindFbo(GL_FRAMEBUFFER, msaaFbo); bindRbo(GL_RENDERBUFFER, msaaRbo);
    msaaStorage(GL_RENDERBUFFER, 4, GL_RGBA8, 32, 32);
    attachRbo(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaRbo);
    if (checkFbo(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return stageFailure("msaa-resolve-readback", 24, "wine-unavailable:4x-framebuffer-incomplete");
    genTextures(1, &resolveTex); bindTexture(GL_TEXTURE_2D, resolveTex);
    texImage(GL_TEXTURE_2D, 0, GL_RGBA8, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    bindFbo(GL_FRAMEBUFFER, resolveFbo);
    auto attachTexture = (PFN_glFramebufferTexture)(uintptr_t)lookup("glFramebufferTexture");
    if (!attachTexture) return stageFailure("msaa-resolve-readback", 24, "framebuffer-texture-entry-point-missing");
    attachTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, resolveTex, 0);
    if (checkFbo(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return stageFailure("msaa-resolve-readback", 24, "resolve-framebuffer-incomplete");
    bindFbo(0x8CA8, msaaFbo); bindFbo(0x8CA9, resolveFbo);
    glViewport(0, 0, 32, 32); glClearColor(0.25f, 0.5f, 0.75f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
    blitFbo(0, 0, 32, 32, 0, 0, 32, 32, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    bindFbo(GL_FRAMEBUFFER, resolveFbo); glFinish();
    unsigned char resolved[4] = {0,0,0,0}; glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, resolved);
    if (resolved[0] != 64 || resolved[1] != 128 || resolved[2] != 191 || resolved[3] != 255)
        return stageFailure("msaa-resolve-readback", 24, "resolved-pixel-mismatch");
    stagePass("msaa-resolve-readback", "samples=4 got=64,128,191,255");
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(shared); wglDeleteContext(core41);
    if (!wglMakeCurrent(dc, context)) return fail(18, "restore compat context", GetLastError());

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(context);
    ReleaseDC(window, dc);
    DestroyWindow(window);
    if (blocked) return 43;
    emit("PLAY-OPENGL PASS pixel-readback\n");
    return 42;
}
}   // extern "C"
