#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace kernel32_core {
// Attribute lists contain native kernel handles and are consumed by the OS.
// Bind exact documented signatures; missing AppContainer exports stay absent.
inline uintptr_t resolve_process_attributes(const char* dll,const char* name) {
    if (!dll || !name) return 0;
    bool kernel=!_stricmp(dll,"kernel32.dll") || !_stricmp(dll,"kernelbase.dll");
    bool threads=!_stricmp(dll,"api-ms-win-core-processthreads-l1-1-0.dll") ||
                 !_stricmp(dll,"api-ms-win-core-processthreads-l1-1-1.dll");
    if (!kernel && !threads) return 0;
    HMODULE module=GetModuleHandleW(L"kernel32.dll");
    if (!module) return 0;
#define K32_ATTRIBUTE(symbol,result,parameters) \
    if (!strcmp(name,#symbol)) { \
        using Function=result (WINAPI*)parameters; \
        static Function function=reinterpret_cast<Function>(reinterpret_cast<uintptr_t>(GetProcAddress(module,#symbol))); \
        return reinterpret_cast<uintptr_t>(function); \
    }
    K32_ATTRIBUTE(InitializeProcThreadAttributeList,BOOL,(LPPROC_THREAD_ATTRIBUTE_LIST,DWORD,DWORD,PSIZE_T));
    K32_ATTRIBUTE(UpdateProcThreadAttribute,BOOL,(LPPROC_THREAD_ATTRIBUTE_LIST,DWORD,DWORD_PTR,PVOID,SIZE_T,PVOID,PSIZE_T));
    K32_ATTRIBUTE(DeleteProcThreadAttributeList,void,(LPPROC_THREAD_ATTRIBUTE_LIST));
#undef K32_ATTRIBUTE
    return 0;
}
}
