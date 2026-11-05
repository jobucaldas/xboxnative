#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace kernel32_core {
// Entries and headers are native, 16-byte-aligned x64 SLIST objects. Use the
// platform operations so its atomic sequence/depth and ABA protection stay
// intact under concurrent guest threads.
inline uintptr_t resolve_slist(const char* dll, const char* name) {
    if (!dll || !name) return 0;
    if (_stricmp(dll, "kernel32.dll") && _stricmp(dll, "kernelbase.dll") &&
        _stricmp(dll, "api-ms-win-core-interlocked-l1-1-0.dll")) return 0;
#define K32_SLIST(symbol) if (!strcmp(name, #symbol)) return reinterpret_cast<uintptr_t>(&::symbol)
    K32_SLIST(InitializeSListHead);
    K32_SLIST(InterlockedPushEntrySList);
    K32_SLIST(InterlockedPopEntrySList);
    K32_SLIST(InterlockedFlushSList);
#undef K32_SLIST
    return 0;
}
}
