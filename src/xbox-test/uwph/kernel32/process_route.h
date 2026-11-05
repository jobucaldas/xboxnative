#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace kernel32_core {
// Play shares its native process and installs its guest working directory.
// Guest command-line, module identity, thread creation and termination remain
// intercepted by play_runtime, and are deliberately absent from this table.
inline uintptr_t resolve_process(const char* dll, const char* name) {
    if (!dll || !name) return 0;
    if (_stricmp(dll, "kernel32.dll") && _stricmp(dll, "kernelbase.dll")) return 0;
#define K32_PROCESS(symbol) if (!strcmp(name, #symbol)) return reinterpret_cast<uintptr_t>(&::symbol)
    K32_PROCESS(GetEnvironmentVariableW);
    K32_PROCESS(GetEnvironmentVariableA);
    K32_PROCESS(SetEnvironmentVariableW);
    K32_PROCESS(ExpandEnvironmentStringsW);
    K32_PROCESS(GetEnvironmentStringsW);
    K32_PROCESS(FreeEnvironmentStringsW);
    K32_PROCESS(GetCurrentDirectoryW);
    K32_PROCESS(GetCurrentDirectoryA);
    K32_PROCESS(GetFullPathNameW);
    K32_PROCESS(GetTempPathW);
    K32_PROCESS(GetTempFileNameW);
    K32_PROCESS(GetLastError);
    K32_PROCESS(SetLastError);
    K32_PROCESS(GetCurrentProcess);
    K32_PROCESS(GetCurrentThread);
    K32_PROCESS(GetCurrentThreadId);
    K32_PROCESS(GetProcessId);
    K32_PROCESS(GetThreadId);
    K32_PROCESS(GetExitCodeThread);
    K32_PROCESS(GetThreadPriority);
    K32_PROCESS(SetThreadPriority);
    K32_PROCESS(GetErrorMode);
    K32_PROCESS(SetErrorMode);
    K32_PROCESS(SetThreadErrorMode);
    K32_PROCESS(EncodePointer);
    K32_PROCESS(DecodePointer);
    K32_PROCESS(IsDebuggerPresent);
    K32_PROCESS(IsProcessorFeaturePresent);
    K32_PROCESS(SetHandleInformation);
    if (!strcmp(name, "GetStartupInfoW")) {
        using Query = void (WINAPI*)(LPSTARTUPINFOW);
        static Query query = []() -> Query {
            HMODULE module = LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto p = module ? GetProcAddress(module, "GetStartupInfoW") : nullptr;
            return reinterpret_cast<Query>(reinterpret_cast<uintptr_t>(p));
        }();
        // A void API cannot report unsupported behavior to its caller. Leave
        // this route absent when the OS lacks it; execution then fails closed.
        return reinterpret_cast<uintptr_t>(query);
    }
    if (!strcmp(name, "GetStartupInfoA")) {
        using Query = void (WINAPI*)(LPSTARTUPINFOA);
        static Query query = []() -> Query {
            HMODULE module = LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto p = module ? GetProcAddress(module, "GetStartupInfoA") : nullptr;
            return reinterpret_cast<Query>(reinterpret_cast<uintptr_t>(p));
        }();
        // A void API cannot report unsupported behavior to its caller. Leave
        // this route absent when the OS lacks it; execution then fails closed.
        return reinterpret_cast<uintptr_t>(query);
    }
#undef K32_PROCESS
    return 0;
}
}
