#pragma once
// Guest memory always uses documented AppContainer W^X primitives. Never hand
// VirtualAlloc/VirtualProtect's desktop implementation to an imported guest.

#include "../../shared/kernel_tail.h"

struct tagTHREADENTRY32; // SDK desktop-only declaration; pointer is opaque here.
namespace entry_kernel {
static void (*logger)(const char*);
#define ENTRY_NOTE(name) do {static LONG seen=0; if(InterlockedCompareExchange(&seen,1,0)==0 && logger) logger("ENTRY-BUILTIN CALL kernel32.dll!" name);}while(0)
static LPVOID WINAPI alloc(LPVOID p,SIZE_T n,DWORD flags,DWORD protection) { ENTRY_NOTE("VirtualAlloc");
    if((protection&0xff)==PAGE_EXECUTE_READWRITE || (protection&0xff)==PAGE_EXECUTE_WRITECOPY) {SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
    return VirtualAllocFromApp(p,n,flags,protection);
}
static BOOL WINAPI protect(LPVOID p,SIZE_T n,DWORD protection,PDWORD previous) { ENTRY_NOTE("VirtualProtect");
    if((protection&0xff)==PAGE_EXECUTE_READWRITE || (protection&0xff)==PAGE_EXECUTE_WRITECOPY) {SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return VirtualProtectFromApp(p,n,protection,previous);
}
static BOOL WINAPI process(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION info) { ENTRY_NOTE("CreateProcessW");
    if(info) *info={}; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
}
// Preserve native ABI and errors for the tail; absent exports remain unresolved.
static uintptr_t resolve(const char* name) {
    if(!strcmp(name,"VirtualAlloc"))return (uintptr_t)&alloc;
    if(!strcmp(name,"VirtualProtect"))return (uintptr_t)&protect;
    if(!strcmp(name,"CreateProcessW"))return (uintptr_t)&process;
    static const char* tail[]={"CreateToolhelp32Snapshot","Thread32First","Thread32Next","GetComputerNameW","GetStartupInfoA","GetSystemPowerStatus","SetDllDirectoryW","GetDllDirectoryW","LoadLibraryExW","LoadLibraryW","LoadLibraryExA","LoadLibraryA","SetThreadAffinityMask","VerifyVersionInfoW","CreateWaitableTimerA"};
    for(auto symbol:tail)if(!strcmp(name,symbol))return kernel_tail::resolve(name);
    return 0;
}
}

#undef ENTRY_NOTE
