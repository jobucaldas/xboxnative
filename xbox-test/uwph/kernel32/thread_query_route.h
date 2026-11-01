#pragma once
#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <cstring>
namespace kernel32_core {
#if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
// Xbox UWP activation rejects the desktop affinity import. Do not attempt an
// export lookup to evade that restriction, or report an affinity change.
inline DWORD_PTR WINAPI unsupported_thread_affinity(HANDLE,DWORD_PTR) {
    SetLastError(ERROR_NOT_SUPPORTED);return 0;
}
#endif
// Thread/process handles are native objects. Queries and access rights stay
// subject to the OS; this layer never synthesizes a successful remote access.
inline uintptr_t resolve_thread_query(const char* dll,const char* name) {
    if (!dll || !name) return 0;
    bool kernel=!_stricmp(dll,"kernel32.dll") || !_stricmp(dll,"kernelbase.dll");
    bool thread=!_stricmp(dll,"api-ms-win-core-processthreads-l1-1-0.dll") ||
                !_stricmp(dll,"api-ms-win-core-processthreads-l1-1-1.dll");
    if (!kernel && !thread) return 0;
#define K32_THREAD_QUERY(symbol) if (!strcmp(name,#symbol)) return reinterpret_cast<uintptr_t>(&::symbol)
    K32_THREAD_QUERY(OpenThread);
    K32_THREAD_QUERY(SuspendThread);
    K32_THREAD_QUERY(ResumeThread);
    if (!strcmp(name,"TerminateThread")) {
        using Terminate=BOOL (WINAPI*)(HANDLE,DWORD);
        static Terminate terminate=[]() -> Terminate {
            HMODULE module=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto p=module ? GetProcAddress(module,"TerminateThread") : nullptr;
            return reinterpret_cast<Terminate>(reinterpret_cast<uintptr_t>(p));
        }();
        return reinterpret_cast<uintptr_t>(terminate);
    }
    K32_THREAD_QUERY(GetThreadContext);
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    K32_THREAD_QUERY(SetThreadAffinityMask);
#else
    if(!strcmp(name,"SetThreadAffinityMask"))return reinterpret_cast<uintptr_t>(&unsupported_thread_affinity);
#endif
    K32_THREAD_QUERY(GetThreadDescription);
    K32_THREAD_QUERY(SetThreadDescription);
    K32_THREAD_QUERY(OpenProcess);
    K32_THREAD_QUERY(GetExitCodeProcess);
    if (kernel) {
        if (!strcmp(name,"ReadProcessMemory")) {
            using Read=BOOL (WINAPI*)(HANDLE,LPCVOID,LPVOID,SIZE_T,SIZE_T*);
            static Read read=[]() -> Read {
                HMODULE module=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
                auto p=module ? GetProcAddress(module,"ReadProcessMemory") : nullptr;
                return reinterpret_cast<Read>(reinterpret_cast<uintptr_t>(p));
            }();
            return reinterpret_cast<uintptr_t>(read);
        }
        K32_THREAD_QUERY(K32GetProcessMemoryInfo);
    }
#undef K32_THREAD_QUERY
    return 0;
}
}
