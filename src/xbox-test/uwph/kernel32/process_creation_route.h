#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace kernel32_core {
// Native process creation keeps AppContainer policy in the OS. The launcher
// must not invent a successful child or bypass that policy. Guest thread and
// loader identity remain separate runtime hooks.
inline uintptr_t resolve_process_creation(const char* dll,const char* name) {
    if(!dll||!name)return 0;
    bool kernel=!_stricmp(dll,"kernel32.dll")||!_stricmp(dll,"kernelbase.dll");
    bool threads=!_stricmp(dll,"api-ms-win-core-processthreads-l1-1-0.dll")||
                 !_stricmp(dll,"api-ms-win-core-processthreads-l1-1-1.dll");
    if(!kernel&&!threads)return 0;
    if(strcmp(name,"CreateProcessW"))return 0;
    using Create=BOOL (WINAPI*)(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION);
    HMODULE module=GetModuleHandleW(L"kernel32.dll");
    if(!module)return 0;
    static Create create=reinterpret_cast<Create>(reinterpret_cast<uintptr_t>(GetProcAddress(module,"CreateProcessW")));
    return reinterpret_cast<uintptr_t>(create);
}
}
