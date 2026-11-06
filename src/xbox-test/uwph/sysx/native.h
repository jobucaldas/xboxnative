#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <mutex>

// Only real System32 exports. No guest handle adaptation, DLL search through
// game directories, fallback success, or replacement network/crypto protocol.
namespace sysx_native {
struct Library {
    const char* name;
    const wchar_t* wide;
    HMODULE module;
};
inline constexpr struct { const char* dll; const char* symbol; } allowed[] = {
        {"dbghelp.dll", "SymInitialize"},
        {"dbghelp.dll", "SymFromAddr"},
        {"dbghelp.dll", "SymLoadModuleEx"},
        {"dbghelp.dll", "SymRegisterFunctionEntryCallback64"},
        // gbe_fork's crash printer (Steam API provider for owned games).
        {"dbghelp.dll", "SymSetOptions"},
        {"dbghelp.dll", "SymCleanup"},
        {"dbghelp.dll", "SymGetSymFromAddr64"},
        {"dbghelp.dll", "SymGetModuleBase64"},
        {"dbghelp.dll", "SymFunctionTableAccess64"},
        {"dbghelp.dll", "StackWalk64"},
        // Version queries (Unity, Rewired): the real OS version.
        {"ntdll.dll", "RtlGetVersion"},
        {"ntdll.dll", "RtlVerifyVersionInfo"},
        // Rust std (Fields of Mistria): file/pipe I/O through the native layer.
        {"ntdll.dll", "NtCreateNamedPipeFile"},
        {"ntdll.dll", "NtOpenFile"},
        {"ntdll.dll", "NtReadFile"},
        {"ntdll.dll", "NtWriteFile"},
        {"ntdll.dll", "RtlNtStatusToDosError"},
        // Wine's concrt140 (C++ runtime for games without their own copy):
        // the wait-on-address primitives under kernelbase WaitOnAddress.
        {"ntdll.dll", "RtlWaitOnAddress"},
        {"ntdll.dll", "RtlWakeAddressSingle"},
        {"ntdll.dll", "RtlWakeAddressAll"},
        {"wininet.dll", "InternetOpenA"},
        {"wininet.dll", "InternetConnectA"},
        {"wininet.dll", "InternetCrackUrlA"},
        {"wininet.dll", "InternetCloseHandle"},
        {"wininet.dll", "HttpOpenRequestA"},
        {"wininet.dll", "HttpSendRequestA"},
        {"wininet.dll", "HttpQueryInfoA"},
    };
// ntdll's C runtime subset (string, memory, number formatting and sorting:
// _vsnprintf, strlen, _ui64toa, qsort, ...). These names are lowercase, or one
// underscore and a lowercase letter or digit. They take no handles or guest
// state, so any of them is served natively. System calls and loader/runtime
// functions (Nt*, Zw*, Rtl*, Ldr*, __*) stay on the explicit list above.
inline bool ntdll_crt(const char* name) {
    if (name[0] >= 'a' && name[0] <= 'z') return true;
    return name[0] == '_' && ((name[1] >= 'a' && name[1] <= 'z') || (name[1] >= '0' && name[1] <= '9'));
}
inline uintptr_t resolve(const char* dll, const char* name) {
    if (!dll || !name) return 0;
    bool matched = !_stricmp(dll, "ntdll.dll") && ntdll_crt(name);
    for (const auto& entry : allowed)
        if (!matched && !_stricmp(dll, entry.dll) && !strcmp(name, entry.symbol)) {
            matched = true;
            break;
        }
    if (!matched) return 0;
    // DbgHelp is single threaded. Guest users still must serialize its API
    // calls; this lock protects only module acquisition, not guest operations.
    static Library libraries[] = {
        {"dbghelp.dll", L"dbghelp.dll", nullptr},
        {"wininet.dll", L"wininet.dll", nullptr},
        {"ntdll.dll", L"ntdll.dll", nullptr},
    };
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& library : libraries) if (!_stricmp(dll, library.name)) {
        if (!library.module)
            library.module = LoadLibraryExW(library.wide, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return library.module ? reinterpret_cast<uintptr_t>(GetProcAddress(library.module, name)) : 0;
    }
    return 0;
}
inline void discover(void (*log)(const char*)) {
    for (const auto& entry : allowed) {
        const auto address = resolve(entry.dll, entry.symbol);
        char message[256];
        snprintf(message, sizeof(message), "SYSX NATIVE %s!%s available=%u",
            entry.dll, entry.symbol, unsigned(address != 0));
        log(message);
    }
}
}
