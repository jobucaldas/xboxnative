#include "route.h"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace {
// Stands in for a mapped OpenGL implementation's exports (Mesa's opengl32.dll under Play).
int WINAPI fake_choose(HDC, const PIXELFORMATDESCRIPTOR*) { return 7; }
BOOL WINAPI fake_set(HDC, int format, const PIXELFORMATDESCRIPTOR*) { return format == 7; }
int WINAPI fake_get(HDC) { return 7; }
int WINAPI fake_describe(HDC, int format, UINT, void*) { return format == 1 ? 3 : 0; }
BOOL WINAPI fake_swap(HDC) { return TRUE; }
uintptr_t fake_lookup(const char* name) {
    if (!strcmp(name, "wglChoosePixelFormat")) return reinterpret_cast<uintptr_t>(&fake_choose);
    if (!strcmp(name, "wglSetPixelFormat")) return reinterpret_cast<uintptr_t>(&fake_set);
    if (!strcmp(name, "wglGetPixelFormat")) return reinterpret_cast<uintptr_t>(&fake_get);
    if (!strcmp(name, "wglDescribePixelFormat")) return reinterpret_cast<uintptr_t>(&fake_describe);
    if (!strcmp(name, "wglSwapBuffers")) return reinterpret_cast<uintptr_t>(&fake_swap);
    return 0;
}
}
int main() {
    using Function = int (WINAPI*)(HGDIOBJ, int, void*);
    auto call = reinterpret_cast<Function>(gdi32_core::resolve("GDI32.DLL", "GetObjectA"));
    HMODULE oracle = LoadLibraryW(L"gdi32.dll");
    CHECK(call && oracle);
    auto reference = reinterpret_cast<Function>(GetProcAddress(oracle, "GetObjectA"));
    CHECK(reference);
    unsigned cases = 0;
    for (UINT_PTR value : {UINT_PTR(0), UINT_PTR(1), UINT_PTR(-1), UINT_PTR(0x123456789ULL)})
    for (int size : {-1, 0, 1, 16, 60, 0x7fffffff})
    for (bool null_output : {false, true}) {
        unsigned char buffer[128], expected[128];
        memset(buffer, 0xa5, sizeof(buffer));
        memcpy(expected, buffer, sizeof(buffer));
        auto handle = reinterpret_cast<HGDIOBJ>(value);
        SetLastError(0xdead);
        int result = reference(handle, size, null_output ? nullptr : expected);
        CHECK(result == 0 && GetLastError() == 0xdead);
        CHECK(call(handle, size, null_output ? nullptr : buffer) == result);
        CHECK(GetLastError() == 0xdead);
        CHECK(!memcmp(buffer, expected, sizeof(buffer)));
        for (unsigned char byte : buffer) CHECK(byte == 0xa5);
        ++cases;
    }
    CHECK(!gdi32_core::resolve(nullptr, "GetObjectA"));
    CHECK(!gdi32_core::resolve("gdi32.dll", nullptr));
    CHECK(!gdi32_core::resolve("user32.dll", "GetObjectA"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "getobjecta"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "#1"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "CreatePen"));
    {   // Mesa's software winsys imports StretchDIBits: it resolves and fails cleanly (no GDI surface).
        auto stretch = reinterpret_cast<int (WINAPI*)(HDC,int,int,int,int,int,int,int,int,const void*,const void*,UINT,DWORD)>(
            gdi32_core::resolve("gdi32.dll", "StretchDIBits"));
        CHECK(stretch);
        SetLastError(0);
        CHECK(stretch(nullptr,0,0,1,1,0,0,1,1,nullptr,nullptr,0,SRCCOPY) == 0 && GetLastError() == ERROR_NOT_SUPPORTED);
    }  // unimplemented names stay unresolved
    auto set_format = reinterpret_cast<BOOL (WINAPI*)(HDC, int, const PIXELFORMATDESCRIPTOR*)>(
        gdi32_core::resolve("GDI32.DLL", "SetPixelFormat"));
    auto swap = reinterpret_cast<BOOL (WINAPI*)(HDC)>(
        gdi32_core::resolve("GDI32.DLL", "SwapBuffers"));
    CHECK(set_format && swap);
    auto choose = reinterpret_cast<int (WINAPI*)(HDC, const PIXELFORMATDESCRIPTOR*)>(
        gdi32_core::resolve("GDI32.DLL", "ChoosePixelFormat"));
    CHECK(choose);
    unsigned choose_cases = 0;
    // No formats means no matching format, independent of requested format.
    // Use readable descriptors: Wine tracing may inspect their fields.
    for (UINT_PTR value : {UINT_PTR(0), UINT_PTR(1), UINT_PTR(-1), UINT_PTR(0x123456789ULL)})
    for (DWORD flags : {DWORD(0), DWORD(PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER), DWORD(-1)})
    for (BYTE type : {BYTE(PFD_TYPE_RGBA), BYTE(PFD_TYPE_COLORINDEX), BYTE(255)}) {
        PIXELFORMATDESCRIPTOR request{};
        request.nSize = sizeof(request);
        request.nVersion = 1;
        request.dwFlags = flags;
        request.iPixelType = type;
        request.cColorBits = 32;
        request.cDepthBits = 24;
        request.cStencilBits = 8;
        auto before = request;
        SetLastError(0xdead);
        CHECK(choose(reinterpret_cast<HDC>(value), &request) == 0);
        CHECK(GetLastError() == 0xdead);
        CHECK(!memcmp(&request, &before, sizeof(request)));
        ++choose_cases;
    }
    CHECK(!gdi32_core::resolve("user32.dll", "ChoosePixelFormat"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "choosepixelformat"));
    std::printf("GDI32 OFFDEVICE PASS %u null-driver format selection cases\n", choose_cases);
    // Wine's null OpenGL driver is the oracle for this backend, rather than
    // the host Wine display driver (which may expose real pixel formats).
    PIXELFORMATDESCRIPTOR descriptor{};
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    for (UINT_PTR value : {UINT_PTR(0), UINT_PTR(1), UINT_PTR(-1), UINT_PTR(0x123456789ULL)}) {
        auto dc = reinterpret_cast<HDC>(value);
        for (int format : {-1, 0, 1, 0x7fffffff}) {
            for (auto pfd : {static_cast<const PIXELFORMATDESCRIPTOR*>(nullptr),
                             static_cast<const PIXELFORMATDESCRIPTOR*>(&descriptor),
                             reinterpret_cast<const PIXELFORMATDESCRIPTOR*>(UINT_PTR(1))}) {
                SetLastError(0xdead);
                CHECK(!set_format(dc, format, pfd));
                CHECK(GetLastError() == 0xdead);
            }
        }
        SetLastError(0xdead);
        CHECK(!swap(dc));
        CHECK(GetLastError() == 0xdead);
    }
    CHECK(descriptor.nSize == sizeof(descriptor) && descriptor.nVersion == 1);
    CHECK(!gdi32_core::resolve("user32.dll", "SetPixelFormat"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "swapbuffers"));
    {   // Stock objects are stable, survive DeleteObject, and pens/palettes are not provided.
        auto stock = reinterpret_cast<HGDIOBJ (WINAPI*)(int)>(gdi32_core::resolve("gdi32.dll", "GetStockObject"));
        CHECK(stock);
        HGDIOBJ white = stock(WHITE_BRUSH);
        CHECK(white && stock(WHITE_BRUSH) == white && stock(BLACK_BRUSH) && stock(BLACK_BRUSH) != white);
        CHECK(stock(SYSTEM_FONT) && stock(DEFAULT_GUI_FONT) && stock(NULL_BRUSH));
        CHECK(gdi32_core::delete_object(white) && stock(WHITE_BRUSH) == white);
        CHECK(!stock(BLACK_PEN) && !stock(DEFAULT_PALETTE) && !stock(-1) && !stock(20));
        auto dc = gdi32_core::create_compatible_dc(nullptr);   // a new DC holds WHITE_BRUSH and SYSTEM_FONT
        CHECK(dc && gdi32_core::select_object(dc, stock(BLACK_BRUSH)) == white);
        CHECK(gdi32_core::select_object(dc, stock(DEFAULT_GUI_FONT)) == stock(SYSTEM_FONT) && gdi32_core::delete_dc(dc));
    }
    {
        using Gamma = BOOL (WINAPI*)(HDC, void*);
        auto get = reinterpret_cast<Gamma>(gdi32_core::resolve("gdi32.dll", "GetDeviceGammaRamp"));
        auto set = reinterpret_cast<Gamma>(gdi32_core::resolve("gdi32.dll", "SetDeviceGammaRamp"));
        CHECK(get && set);
        static WORD ramp[3][256];
        HDC record = reinterpret_cast<HDC>(UINT_PTR(0x1234));
        CHECK(get(record, ramp) && ramp[0][0] == 0 && ramp[1][128] == 128 * 257 && ramp[2][255] == 0xffff);
        SetLastError(0); CHECK(!get(nullptr, ramp) && GetLastError() == ERROR_INVALID_PARAMETER);
        SetLastError(0); CHECK(!set(record, ramp) && GetLastError() == ERROR_NOT_SUPPORTED);
        SetLastError(0); CHECK(!set(record, nullptr) && GetLastError() == ERROR_INVALID_PARAMETER);
    }
    {   // With a mapped OpenGL implementation, gdi32 forwards to its wgl* entry points.
        auto describe = reinterpret_cast<int (WINAPI*)(HDC, int, UINT, void*)>(gdi32_core::resolve("gdi32.dll", "DescribePixelFormat"));
        auto get_format = reinterpret_cast<int (WINAPI*)(HDC)>(gdi32_core::resolve("gdi32.dll", "GetPixelFormat"));
        CHECK(describe && get_format);
        gdi32_core::gl_lookup.store(&fake_lookup);
        PIXELFORMATDESCRIPTOR request{};
        HDC dc = reinterpret_cast<HDC>(UINT_PTR(0x1234));
        CHECK(choose(dc, &request) == 7 && set_format(dc, 7, nullptr) && !set_format(dc, 6, nullptr));
        CHECK(swap(dc) && get_format(dc) == 7 && describe(dc, 1, sizeof request, &request) == 3);
        gdi32_core::gl_lookup.store([](const char*) -> uintptr_t { return 0; });   // a module without these exports
        CHECK(choose(dc, &request) == 0 && !set_format(dc, 7, nullptr) && !swap(dc) && describe(dc, 1, sizeof request, &request) == 0);
        SetLastError(0); CHECK(get_format(dc) == 0 && GetLastError() == ERROR_INVALID_PIXEL_FORMAT);
        gdi32_core::gl_lookup.store(nullptr);
        CHECK(choose(dc, &request) == 0 && !swap(dc));
        std::puts("GDI32 OFFDEVICE PASS pixel format/SwapBuffers forward to a mapped OpenGL module and fail cleanly without one");
    }
    FreeLibrary(oracle);
    std::puts("GDI32 OFFDEVICE PASS null-driver pixel format/presentation failures, descriptor guards, last error");
    std::printf("GDI32 OFFDEVICE PASS %u invalid-object cases, Wine differential, output guards, last error, resolver isolation\n", cases);
}
