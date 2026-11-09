#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace kernel32_core {
// Growable function tables (Mono's JIT registers its generated code this way).
// The native unwinder reads tables only from the list its own ntdll owns:
// RtlLookupFunctionEntry and RtlVirtualUnwind never consult a private
// registry, so a local list would accept every table and then fail to unwind
// through it. Forward to the OS exports; when the OS has none, return the
// documented failure instead of claiming a registration nothing will read.
// STATUS_NOT_SUPPORTED (ntstatus.h); the UWP SDK's winnt.h omits it.
inline constexpr DWORD status_not_supported=0xC00000BB;
inline DWORD NTAPI add_growable_unsupported(void**,RUNTIME_FUNCTION*,DWORD,DWORD,ULONG_PTR,ULONG_PTR) {
    return status_not_supported;
}
// No table was ever issued, so an unknown handle has nothing to grow or free;
// Wine's own implementation ignores a handle it does not own the same way.
inline void NTAPI grow_unsupported(void*,DWORD) {}
inline void NTAPI delete_growable_unsupported(void*) {}
inline uintptr_t resolve_growable(const char* name) {
    // System32 only, the same acquisition sysx_native uses for ntdll exports.
    static HMODULE ntdll=LoadLibraryExW(L"ntdll.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(ntdll) if(auto p=reinterpret_cast<uintptr_t>(GetProcAddress(ntdll,name))) return p;
    if(!strcmp(name,"RtlAddGrowableFunctionTable")) return reinterpret_cast<uintptr_t>(&add_growable_unsupported);
    if(!strcmp(name,"RtlGrowFunctionTable")) return reinterpret_cast<uintptr_t>(&grow_unsupported);
    if(!strcmp(name,"RtlDeleteGrowableFunctionTable")) return reinterpret_cast<uintptr_t>(&delete_growable_unsupported);
    return 0;
}
// Mapped guest unwind metadata is registered by Play with the native runtime.
// These APIs must use that same runtime, stack and exception dispatcher.
inline uintptr_t resolve_exception(const char* dll,const char* name) {
    if (!dll || !name) return 0;
    bool kernel=!_stricmp(dll,"kernel32.dll") || !_stricmp(dll,"kernelbase.dll");
    bool rtl=!_stricmp(dll,"ntdll.dll");
    bool error=!_stricmp(dll,"api-ms-win-core-errorhandling-l1-1-0.dll");
    bool debug=!_stricmp(dll,"api-ms-win-core-debug-l1-1-0.dll");
    // The rtlsupport API set names the same native unwind runtime (Rewired's
    // WindowsGamingInput plugin imports through it).
    bool support=!_stricmp(dll,"api-ms-win-core-rtlsupport-l1-1-0.dll") ||
                 !_stricmp(dll,"api-ms-win-core-rtlsupport-l1-2-0.dll");
    if (!kernel && !rtl && !error && !debug && !support) return 0;
#define K32_EXCEPTION(symbol) if (!strcmp(name,#symbol)) return reinterpret_cast<uintptr_t>(&::symbol)
    if (support || rtl) K32_EXCEPTION(RtlLookupFunctionEntry); // kernel32: mapper builtin
    // ntdll-only in Wine 10 (ntdll.spec); the rtlsupport set maps to ntdll.
    if (rtl || support) {
        if (!strcmp(name,"RtlAddGrowableFunctionTable") ||
            !strcmp(name,"RtlGrowFunctionTable") ||
            !strcmp(name,"RtlDeleteGrowableFunctionTable")) return resolve_growable(name);
    }
    if (kernel || rtl || support) {
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
