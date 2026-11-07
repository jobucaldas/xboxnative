// SPDX-License-Identifier: GPL-3.0-or-later
// Guest loading OPENGL32.DLL at run time, as SDL's WGL loader does. The
// console has no opengl32.dll; Play must answer the same way on every host.
#include <windows.h>
extern "C" DWORD WINAPI gl_entry() {
    HMODULE module=LoadLibraryA("OPENGL32.DLL");if(!module)return 1;
    if(GetModuleHandleW(L"opengl32.dll")!=module)return 2;
    using Proc=PROC(WINAPI*)(LPCSTR);
    using Create=HGLRC(WINAPI*)(HDC);
    using Current=HGLRC(WINAPI*)();
    auto getProc=reinterpret_cast<Proc>(GetProcAddress(module,"wglGetProcAddress"));
    auto create=reinterpret_cast<Create>(GetProcAddress(module,"wglCreateContext"));
    auto current=reinterpret_cast<Current>(GetProcAddress(module,"wglGetCurrentContext"));
    if(!getProc || !create || !current || !GetProcAddress(module,"wglDeleteContext") || !GetProcAddress(module,"wglMakeCurrent"))return 3;
    // No driver: no context, no extension entry points, and no host DC reaches GL.
    if(current()!=nullptr || getProc("glGetString")!=nullptr)return 4;
    SetLastError(0);
    if(create(reinterpret_cast<HDC>(0x1234)) || GetLastError()!=ERROR_INVALID_HANDLE)return 5;
    SetLastError(0);
    if(GetProcAddress(module,"glClear") || GetLastError()!=ERROR_PROC_NOT_FOUND)return 6;
    WCHAR name[128];SetLastError(0);
    if(GetModuleFileNameW(module,name,128) || GetLastError()!=ERROR_NOT_SUPPORTED)return 7;
    HMODULE second=nullptr;
    if(!GetModuleHandleExW(0,L"opengl32.dll",&second) || second!=module)return 8;
    if(!FreeLibrary(second) || !GetProcAddress(module,"wglMakeCurrent"))return 9;
    if(!FreeLibrary(module))return 10;
    SetLastError(0);
    if(GetProcAddress(module,"wglMakeCurrent") || GetLastError()!=ERROR_INVALID_HANDLE)return 11;
    return 42;
}
// Mapper requires relocatable images; retain one actual base relocation.
extern "C" void* gl_relocation=reinterpret_cast<void*>(&gl_entry);
