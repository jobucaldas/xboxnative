#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace kernel32_core {
inline uintptr_t resolve_system(const char* dll,const char* name) {
    if (!dll || !name) return 0;
    bool kernel=!_stricmp(dll,"kernel32.dll") || !_stricmp(dll,"kernelbase.dll");
    bool system=!_stricmp(dll,"api-ms-win-core-sysinfo-l1-1-0.dll") ||
                !_stricmp(dll,"api-ms-win-core-sysinfo-l1-2-0.dll");
    if (!kernel && !system) return 0;
#define K32_SYSTEM(symbol) if (!strcmp(name,#symbol)) return reinterpret_cast<uintptr_t>(&::symbol)
    K32_SYSTEM(GetSystemDirectoryW);
    K32_SYSTEM(GetSystemDirectoryA);
    if (!strcmp(name,"GetWindowsDirectoryW")) {
        using Query=UINT (WINAPI*)(LPWSTR,UINT);
        static Query query=[]() -> Query {
            HMODULE module=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto p=module ? GetProcAddress(module,"GetWindowsDirectoryW") : nullptr;
            return reinterpret_cast<Query>(reinterpret_cast<uintptr_t>(p));
        }();
        return reinterpret_cast<uintptr_t>(query);
    }
    K32_SYSTEM(GetComputerNameW);
    K32_SYSTEM(GetLogicalProcessorInformationEx);
    K32_SYSTEM(GetSystemTimes);
    K32_SYSTEM(GetSystemPowerStatus);
    K32_SYSTEM(GetVersionExW);
    if (!strcmp(name,"VerSetConditionMask")) {
        using Mask=ULONGLONG (WINAPI*)(ULONGLONG,DWORD,BYTE);
        static Mask mask=[]() -> Mask {
            HMODULE module=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto p=module ? GetProcAddress(module,"VerSetConditionMask") : nullptr;
            return reinterpret_cast<Mask>(reinterpret_cast<uintptr_t>(p));
        }();
        return reinterpret_cast<uintptr_t>(mask);
    }
#undef K32_SYSTEM
    return 0;
}
}
