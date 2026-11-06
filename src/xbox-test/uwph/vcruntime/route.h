// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

// vcruntime140.dll and vcruntime140_1.dll are Visual C++ redistributable DLLs.
// Desktop Windows gets them from the VC++ redistributable (Steam installs it
// from the 228989 common-redistributable depot); the console ships neither, so
// a game that does not carry its own copy fails with ERROR_MOD_NOT_FOUND.
//
// The Universal CRT (ucrtbase.dll, an OS component present on the console)
// exports the same exception-handling, RTTI and string functions. Wine 10's
// vcruntime140 and vcruntime140_1 are thin modules that forward exactly these
// names to ucrtbase. This route does the same: each forwarded name resolves to
// the console's own ucrtbase export, so a throw raised by one module and caught
// by another always uses one runtime and one per-thread exception state.
//
// The handful of names vcruntime implements itself (telemetry hooks, the
// __vcrt_* loader helpers, __uncaught_exceptions, __report_gsfailure and
// unexpected) are owned implementations below. The loader helpers call the
// host's guest-aware kernel32, so they see mapped game modules. A name that is
// neither forwarded nor owned stays unresolved; no placeholder is returned.
// A game's own vcruntime140.dll (app-local deployment) is mapped instead of
// this route by the caller, as Windows prefers the application directory.
namespace vcruntime_core {
// (dll, name) -> address, the host's own import resolver.
using Lookup = uintptr_t (*)(const char* dll, const char* name);
inline std::atomic<Lookup> lookup{nullptr};

// Wine 10 dlls/vcruntime140/vcruntime140.spec: every "ucrtbase.<name>" forward,
// all architectures. Names absent from the running ucrtbase stay unresolved.
inline constexpr const char* forwarded[] = {
    "_CreateFrameInfo", "_CxxThrowException", "_EH_prolog", "_FindAndUnlinkFrame",
    "_IsExceptionObjectToBeDestroyed", "_SetWinRTOutOfMemoryExceptionCallback",
    "__AdjustPointer", "__C_specific_handler", "__CxxDetectRethrow", "__CxxExceptionFilter",
    "__CxxFrameHandler", "__CxxFrameHandler2", "__CxxFrameHandler3", "__CxxLongjmpUnwind",
    "__CxxQueryExceptionSize", "__CxxRegisterExceptionObject", "__CxxUnregisterExceptionObject",
    "__DestructExceptionObject", "__RTCastToVoid", "__RTDynamicCast", "__RTtypeid",
    "__current_exception", "__current_exception_context", "__intrinsic_setjmp",
    "__intrinsic_setjmpex", "__jump_unwind", "__processing_throw", "__std_exception_copy",
    "__std_exception_destroy", "__std_terminate", "__std_type_info_compare",
    "__std_type_info_destroy_list", "__std_type_info_hash", "__std_type_info_name",
    "__unDName", "__unDNameEx", "__uncaught_exception", "_chkesp", "_except_handler2",
    "_except_handler3", "_except_handler4_common", "_get_purecall_handler", "_get_unexpected",
    "_global_unwind2", "_local_unwind2", "_local_unwind4", "_longjmpex", "_local_unwind",
    "_purecall", "_seh_longjmp_unwind4", "_seh_longjmp_unwind", "_set_purecall_handler",
    "_set_se_translator", "_setjmp3", "longjmp", "memchr", "memcmp", "memcpy", "memmove",
    "memset", "set_unexpected", "strchr", "strrchr", "strstr", "wcschr", "wcsrchr", "wcsstr",
};
// Wine 10 dlls/vcruntime140_1: __CxxFrameHandler4 (x64 FH4 tables), which
// ucrtbase also exports on x64. The two NLG entries are debugger hooks.
inline constexpr const char* forwarded_1[] = {"__CxxFrameHandler4", "__NLG_Dispatch2", "__NLG_Return2"};
// Names that are stubs in Wine's vcruntime140 but that a native ucrtbase may
// export: offered when the running ucrtbase has them, otherwise unresolved.
inline constexpr const char* native_only[] = {
    "_NLG_Dispatch2", "_NLG_Return", "_NLG_Return2", "__BuildCatchObject",
    "__BuildCatchObjectHelper", "__C_specific_handler_noexcept", "__FrameUnwindFilter",
    "__GetPlatformExceptionInfo", "__NLG_Dispatch2", "__NLG_Return2", "__TypeMatch",
    "_is_exception_typeof",
};

inline uintptr_t find(const char* dll, const char* name) {
    auto host = lookup.load();
    return host ? host(dll, name) : 0;
}
template <typename F> inline F bind(const char* dll, const char* name) {
    return reinterpret_cast<F>(find(dll, name));
}

// Telemetry hooks the CRT startup calls around main(); vcruntime records an ETW
// event. There is no ETW provider for guests: nothing to record.
inline void __cdecl telemetry_main_invoke_trigger(HINSTANCE) {}
inline void __cdecl telemetry_main_return_trigger(HINSTANCE) {}
// C++17 std::uncaught_exceptions(): the count of exceptions thrown and not yet
// caught on this thread, which ucrtbase keeps in __processing_throw.
inline int __cdecl uncaught_exceptions() {
    auto counter = bind<int* (__cdecl*)()>("ucrtbase.dll", "__processing_throw");
    return counter ? *counter() : 0;
}
inline BOOL __cdecl initialize_critical_section_ex(CRITICAL_SECTION* section, DWORD spin, DWORD flags) {
    auto call = bind<BOOL (WINAPI*)(CRITICAL_SECTION*, DWORD, DWORD)>("kernel32.dll", "InitializeCriticalSectionEx");
    if (!call) { SetLastError(ERROR_PROC_NOT_FOUND); return FALSE; }
    return call(section, spin, flags);
}
inline DWORD __cdecl get_module_file_name(HMODULE module, LPWSTR buffer, DWORD size) {
    auto call = bind<DWORD (WINAPI*)(HMODULE, LPWSTR, DWORD)>("kernel32.dll", "GetModuleFileNameW");
    if (!call) { SetLastError(ERROR_PROC_NOT_FOUND); return 0; }
    return call(module, buffer, size);
}
inline HMODULE __cdecl get_module_handle(LPCWSTR name) {
    auto call = bind<HMODULE (WINAPI*)(LPCWSTR)>("kernel32.dll", "GetModuleHandleW");
    if (!call) { SetLastError(ERROR_PROC_NOT_FOUND); return nullptr; }
    return call(name);
}
inline HMODULE __cdecl load_library_ex(LPCWSTR name, HANDLE file, DWORD flags) {
    auto call = bind<HMODULE (WINAPI*)(LPCWSTR, HANDLE, DWORD)>("kernel32.dll", "LoadLibraryExW");
    if (!call) { SetLastError(ERROR_PROC_NOT_FOUND); return nullptr; }
    return call(name, file, flags);
}
// /GS failure. vcruntime fast-fails, which ends the process with no record;
// raise the same status non-continuably so the host's guest crash report sees
// it and the faulting cookie.
inline void __cdecl report_gsfailure(ULONG_PTR cookie) {
    RaiseException(STATUS_STACK_BUFFER_OVERRUN, EXCEPTION_NONCONTINUABLE, 1, &cookie);
}
// Calls the handler installed by set_unexpected, then terminates (C++ [except.unexpected]).
inline void __cdecl call_unexpected() {
    using Handler = void (__cdecl*)();
    if (auto get = bind<Handler (__cdecl*)()>("ucrtbase.dll", "_get_unexpected"))
        if (auto handler = get()) handler();
    if (auto terminate = bind<Handler>("ucrtbase.dll", "terminate")) terminate();
    RaiseException(STATUS_NONCONTINUABLE_EXCEPTION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

#if defined(_M_X64)
// x64 C++ throw. ucrtbase's own _CxxThrowException finds the image that holds
// the ThrowInfo with ntdll's RtlPcToFileHeader, which knows only OS-loaded
// modules: for a host-mapped game image it yields 0, the catch handler then
// reads the image-relative type RVAs from address 0 and faults. This is the
// same record (MSVC ehdata.h: magic, object, ThrowInfo, image base) built with
// the host's guest-aware RtlPcToFileHeader. A rethrow ("throw;") passes no
// ThrowInfo and keeps base 0, as on Windows.
inline void __stdcall cxx_throw_exception(void* object, const void* info) {
    constexpr DWORD cxx_exception = 0xE06D7363, magic = 0x19930520, pure_magic = 0x01994000;
    constexpr unsigned is_pure = 0x8, is_winrt = 0x10;  // ThrowInfo attributes
    ULONG_PTR args[4] = {magic, reinterpret_cast<ULONG_PTR>(object), reinterpret_cast<ULONG_PTR>(info), 0};
    if (info) {
        unsigned attributes = *static_cast<const unsigned*>(info);
        if (attributes & is_pure) args[0] = pure_magic;
        // WinRT exceptions also carry an exception-info block before the
        // object: that path stays with ucrtbase (no guest WinRT projections).
        if (attributes & is_winrt) {
            if (auto native = bind<void (__stdcall*)(void*, const void*)>("ucrtbase.dll", "_CxxThrowException"))
                native(object, info);
        }
        PVOID base = nullptr;
        if (auto header = bind<PVOID (WINAPI*)(PVOID, PVOID*)>("kernel32.dll", "RtlPcToFileHeader"))
            header(const_cast<void*>(info), &base);
        args[3] = reinterpret_cast<ULONG_PTR>(base);
    }
    RaiseException(cxx_exception, EXCEPTION_NONCONTINUABLE, 4, args);
}
#endif

struct Owned { const char* name; uintptr_t address; };
inline const Owned owned[] = {
    {"__telemetry_main_invoke_trigger", reinterpret_cast<uintptr_t>(&telemetry_main_invoke_trigger)},
    {"__telemetry_main_return_trigger", reinterpret_cast<uintptr_t>(&telemetry_main_return_trigger)},
    {"__uncaught_exceptions", reinterpret_cast<uintptr_t>(&uncaught_exceptions)},
    {"__vcrt_InitializeCriticalSectionEx", reinterpret_cast<uintptr_t>(&initialize_critical_section_ex)},
    {"__vcrt_GetModuleFileNameW", reinterpret_cast<uintptr_t>(&get_module_file_name)},
    {"__vcrt_GetModuleHandleW", reinterpret_cast<uintptr_t>(&get_module_handle)},
    {"__vcrt_LoadLibraryExW", reinterpret_cast<uintptr_t>(&load_library_ex)},
    {"__report_gsfailure", reinterpret_cast<uintptr_t>(&report_gsfailure)},
    {"unexpected", reinterpret_cast<uintptr_t>(&call_unexpected)},
#if defined(_M_X64)
    {"_CxxThrowException", reinterpret_cast<uintptr_t>(&cxx_throw_exception)},
#endif
};

// Lowercase DLL key, as the host normalizes it.
inline bool handles(const char* key) {
    return key && (!strcmp(key, "vcruntime140.dll") || !strcmp(key, "vcruntime140_1.dll"));
}
template <size_t N> inline bool listed(const char* const (&names)[N], const char* name) {
    for (auto candidate : names) if (!strcmp(candidate, name)) return true;
    return false;
}
// Address for key!name, or 0 when this route has no answer (unknown name or
// ordinal, or the running ucrtbase lacks the export). Exact-case names only.
inline uintptr_t resolve(const char* key, const char* name, Lookup host) {
    if (!handles(key) || !name || !host) return 0;
    lookup.store(host);
    bool first = !strcmp(key, "vcruntime140.dll");
    if (first) for (auto& entry : owned) if (!strcmp(entry.name, name)) return entry.address;
    if (first ? listed(forwarded, name) || listed(native_only, name) : listed(forwarded_1, name))
        return host("ucrtbase.dll", name);
    return 0;
}
}
