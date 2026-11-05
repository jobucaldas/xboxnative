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
        {"wininet.dll", "InternetOpenA"},
        {"wininet.dll", "InternetConnectA"},
        {"wininet.dll", "InternetCrackUrlA"},
        {"wininet.dll", "InternetCloseHandle"},
        {"wininet.dll", "HttpOpenRequestA"},
        {"wininet.dll", "HttpSendRequestA"},
        {"wininet.dll", "HttpQueryInfoA"},
    };
inline uintptr_t resolve(const char* dll, const char* name) {
    if (!dll || !name) return 0;
    bool matched = false;
    for (const auto& entry : allowed)
        if (!_stricmp(dll, entry.dll) && !strcmp(name, entry.symbol)) {
            matched = true;
            break;
        }
    if (!matched) return 0;
    // DbgHelp is single threaded. Guest users still must serialize its API
    // calls; this lock protects only module acquisition, not guest operations.
    static Library libraries[] = {
        {"dbghelp.dll", L"dbghelp.dll", nullptr},
        {"wininet.dll", L"wininet.dll", nullptr},
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
