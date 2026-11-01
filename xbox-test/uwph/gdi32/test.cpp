#include "route.h"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
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
    CHECK(!gdi32_core::resolve("gdi32.dll", "CreateBitmap"));
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
    FreeLibrary(oracle);
    std::puts("GDI32 OFFDEVICE PASS null-driver pixel format/presentation failures, descriptor guards, last error");
    std::printf("GDI32 OFFDEVICE PASS %u invalid-object cases, Wine differential, output guards, last error, resolver isolation\n", cases);
}
