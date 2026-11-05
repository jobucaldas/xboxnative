#include "route.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static DWORD WINAPI check_thread(void*) {
    using Context = HGLRC (WINAPI*)();
    using DC = HDC (WINAPI*)();
    auto context = reinterpret_cast<Context>(opengl32_core::resolve("OPENGL32.DLL", "wglGetCurrentContext"));
    auto dc = reinterpret_cast<DC>(opengl32_core::resolve("opengl32.dll", "wglGetCurrentDC"));
    HMODULE module = LoadLibraryW(L"opengl32.dll");
    CHECK(context && dc && module);
    auto reference_context = reinterpret_cast<Context>(GetProcAddress(module, "wglGetCurrentContext"));
    auto reference_dc = reinterpret_cast<DC>(GetProcAddress(module, "wglGetCurrentDC"));
    CHECK(reference_context && reference_dc);
    using Lookup = PROC (WINAPI*)(LPCSTR);
    auto lookup = reinterpret_cast<Lookup>(opengl32_core::resolve("opengl32.dll", "wglGetProcAddress"));
    auto reference_lookup = reinterpret_cast<Lookup>(GetProcAddress(module, "wglGetProcAddress"));
    CHECK(lookup && reference_lookup);
    const char* names[] = {nullptr, "", "glGetString", "glCreateShader",
        "wglCreateContextAttribsARB", "wglSwapIntervalEXT", "unknown_extension"};
    for (auto name : names) {
        SetLastError(0xdead);
        CHECK(reference_lookup(name) == nullptr && GetLastError() == 0xdead);
        SetLastError(0xdead);
        CHECK(lookup(name) == nullptr && GetLastError() == 0xdead);
    }
    // Fresh threads have no host context either: compare only this state.
    for (unsigned i = 0; i < 32; ++i) {
        SetLastError(0xdead);
        CHECK(reference_context() == nullptr && GetLastError() == 0xdead);
        CHECK(reference_dc() == nullptr && GetLastError() == 0xdead);
        CHECK(context() == nullptr && GetLastError() == 0xdead);
        CHECK(dc() == nullptr && GetLastError() == 0xdead);
    }
    using Create = HGLRC (WINAPI*)(HDC);
    auto create = reinterpret_cast<Create>(opengl32_core::resolve("opengl32.dll", "wglCreateContext"));
    auto reference_create = reinterpret_cast<Create>(GetProcAddress(module, "wglCreateContext"));
    CHECK(create && reference_create);
    using Delete = BOOL (WINAPI*)(HGLRC);
    using Make = BOOL (WINAPI*)(HDC, HGLRC);
    auto remove = reinterpret_cast<Delete>(opengl32_core::resolve("opengl32.dll", "wglDeleteContext"));
    auto make = reinterpret_cast<Make>(opengl32_core::resolve("opengl32.dll", "wglMakeCurrent"));
    auto reference_remove = reinterpret_cast<Delete>(GetProcAddress(module, "wglDeleteContext"));
    auto reference_make = reinterpret_cast<Make>(GetProcAddress(module, "wglMakeCurrent"));
    CHECK(remove && make && reference_remove && reference_make);
    const uintptr_t handles[] = {0, 1, 0x12345678, UINTPTR_MAX};
    for (auto handle : handles) {
        auto invalid_dc = reinterpret_cast<HDC>(handle);
        SetLastError(0xdead);
        CHECK(reference_create(invalid_dc) == nullptr && GetLastError() == ERROR_INVALID_HANDLE);
        SetLastError(0xdead);
        CHECK(create(invalid_dc) == nullptr && GetLastError() == ERROR_INVALID_HANDLE);
        CHECK(!context() && !dc());
        auto rc = reinterpret_cast<HGLRC>(handle);
        SetLastError(0xdead);
        BOOL expected = reference_remove(rc);
        DWORD error = GetLastError();
        CHECK(!expected && error == ERROR_INVALID_HANDLE);
        SetLastError(0xdead);
        CHECK(remove(rc) == expected && GetLastError() == error);
        for (auto value : handles) {
            auto hdc = reinterpret_cast<HDC>(value);
            SetLastError(0xdead);
            expected = reference_make(hdc, rc);
            error = GetLastError();
            CHECK(expected == (!rc && hdc ? TRUE : FALSE));
            CHECK(error == (expected ? 0xdead : ERROR_INVALID_HANDLE));
            SetLastError(0xdead);
            CHECK(make(hdc, rc) == expected && GetLastError() == error);
            CHECK(!context() && !dc());
        }
    }
    FreeLibrary(module);
    return 0;
}
int main() {
    CHECK(check_thread(nullptr) == 0);
    HANDLE thread = CreateThread(nullptr, 0, check_thread, nullptr, 0, nullptr);
    CHECK(thread && WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0);
    DWORD result = 1;
    CHECK(GetExitCodeThread(thread, &result) && result == 0);
    CloseHandle(thread);
    CHECK(!opengl32_core::resolve(nullptr, "wglGetCurrentDC"));
    CHECK(!opengl32_core::resolve("opengl32.dll", nullptr));
    CHECK(!opengl32_core::resolve("gdi32.dll", "wglGetCurrentDC"));
    CHECK(!opengl32_core::resolve("opengl32.dll", "wglgetcurrentdc"));
    CHECK(!opengl32_core::resolve("opengl32.dll", "#1"));
    CHECK(!opengl32_core::resolve("opengl32.dll", "wglCreateContextAttribsARB"));

    std::puts("OPENGL32 OFFDEVICE PASS no-context queries/extension lookup/creation/binding/deletion, two threads, Wine differential, last error, resolver isolation");
}
