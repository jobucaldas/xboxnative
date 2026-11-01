#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace kernel32_core {
// Mapped guest unwind metadata is registered by Play with the native runtime.
// These APIs must use that same runtime, stack and exception dispatcher.
inline uintptr_t resolve_exception(const char* dll,const char* name) {
    if (!dll || !name) return 0;
    bool kernel=!_stricmp(dll,"kernel32.dll") || !_stricmp(dll,"kernelbase.dll");
    bool rtl=!_stricmp(dll,"ntdll.dll");
    bool error=!_stricmp(dll,"api-ms-win-core-errorhandling-l1-1-0.dll");
    bool debug=!_stricmp(dll,"api-ms-win-core-debug-l1-1-0.dll");
    if (!kernel && !rtl && !error && !debug) return 0;
#define K32_EXCEPTION(symbol) if (!strcmp(name,#symbol)) return reinterpret_cast<uintptr_t>(&::symbol)
    if (kernel || rtl) {
        K32_EXCEPTION(RtlCaptureContext);
        K32_EXCEPTION(RtlCaptureStackBackTrace);
        K32_EXCEPTION(RtlVirtualUnwind);
        K32_EXCEPTION(RtlUnwind);
        K32_EXCEPTION(RtlUnwindEx);
    }
    if (kernel || error) {
        K32_EXCEPTION(RaiseException);
        K32_EXCEPTION(SetUnhandledExceptionFilter);
        if (!strcmp(name,"UnhandledExceptionFilter")) {
            // UWP omits the desktop declaration. Keep this one typed native
            // filter; a missing OS implementation leaves resolution absent.
            using Filter=LONG (WINAPI*)(EXCEPTION_POINTERS*);
            static Filter filter=[]() -> Filter {
                HMODULE module=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
                auto p=module ? GetProcAddress(module,"UnhandledExceptionFilter") : nullptr;
                return reinterpret_cast<Filter>(reinterpret_cast<uintptr_t>(p));
            }();
            return reinterpret_cast<uintptr_t>(filter);
        }
    }
    if (kernel || debug) {
        K32_EXCEPTION(OutputDebugStringW);
        K32_EXCEPTION(DebugBreak);
    }
    if (kernel) {
        K32_EXCEPTION(FormatMessageA);
        K32_EXCEPTION(FormatMessageW);
    }
#undef K32_EXCEPTION
    return 0;
}
}
