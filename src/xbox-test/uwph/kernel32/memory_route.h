#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>
namespace kernel32_core {
inline HANDLE WINAPI create_mapping(HANDLE file, LPSECURITY_ATTRIBUTES attributes,
        DWORD protection, DWORD high, DWORD low, LPCWSTR name) {
    return CreateFileMappingFromApp(file, attributes, protection,
        (ULONGLONG(high) << 32) | low, name);
}
inline void* WINAPI map_view(HANDLE mapping, DWORD access, DWORD high, DWORD low, SIZE_T bytes) {
    return MapViewOfFileFromApp(mapping, access, (ULONGLONG(high) << 32) | low, bytes);
}
inline HANDLE WINAPI open_mapping(DWORD access,BOOL inherit,LPCWSTR name) {
    return OpenFileMappingFromApp(access,inherit,name);
}

// This documented desktop query is not declared in the UWP SDK. Resolve only
// this exact typed API from trusted OS modules; preserve native query results.
inline BOOL WINAPI heap_query(HANDLE heap, HEAP_INFORMATION_CLASS kind,
        PVOID data, SIZE_T capacity, PSIZE_T length) {
    using Query = BOOL (WINAPI*)(HANDLE, HEAP_INFORMATION_CLASS, PVOID, SIZE_T, PSIZE_T);
    static Query query = []() -> Query {
        for (auto name : {L"kernel32.dll", L"kernelbase.dll"}) {
            HMODULE module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (module) if (auto p = GetProcAddress(module, "HeapQueryInformation"))
                return reinterpret_cast<Query>(reinterpret_cast<uintptr_t>(p));
        }
        return nullptr;
    }();
    if (!query) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE; }
    return query(heap, kind, data, capacity, length);
}

// Heap/global/local storage and queried pages belong to the native process.
// Executable allocation/protection remains in kernel_tail's W^X adapters.
inline uintptr_t resolve_memory(const char* dll, const char* name) {
    if (!dll || !name) return 0;
    bool kernel = !_stricmp(dll, "kernel32.dll") || !_stricmp(dll, "kernelbase.dll");
    bool heap = !_stricmp(dll, "api-ms-win-core-heap-l1-1-0.dll");
    bool memory = !_stricmp(dll, "api-ms-win-core-memory-l1-1-0.dll") ||
                  !_stricmp(dll, "api-ms-win-core-memory-l1-1-1.dll");
    bool system = !_stricmp(dll, "api-ms-win-core-sysinfo-l1-1-0.dll") ||
                  !_stricmp(dll, "api-ms-win-core-sysinfo-l1-2-0.dll");
    if (!kernel && !heap && !memory && !system) return 0;
    if (kernel || memory) {
        if (!strcmp(name, "CreateFileMappingW")) return reinterpret_cast<uintptr_t>(&create_mapping);
        if (!strcmp(name, "MapViewOfFile")) return reinterpret_cast<uintptr_t>(&map_view);
        if (!strcmp(name, "OpenFileMappingW")) return reinterpret_cast<uintptr_t>(&open_mapping);
        if (!strcmp(name, "UnmapViewOfFile")) return reinterpret_cast<uintptr_t>(&::UnmapViewOfFile);
    }
#define K32_MEMORY(symbol) if (!strcmp(name, #symbol)) return reinterpret_cast<uintptr_t>(&::symbol)
    if (kernel || heap) { K32_MEMORY(GetProcessHeap); }
    if (kernel || heap) { K32_MEMORY(HeapAlloc); }
    if (kernel || heap) { K32_MEMORY(HeapFree); }
    if (kernel || heap) { K32_MEMORY(HeapReAlloc); }
    if (kernel || heap) { K32_MEMORY(HeapSize); }
    if (kernel || heap) { if (!strcmp(name, "HeapQueryInformation")) return reinterpret_cast<uintptr_t>(&heap_query); }
    if (kernel) { K32_MEMORY(GlobalAlloc); }
    if (kernel) { K32_MEMORY(GlobalFree); }
    if (kernel) { K32_MEMORY(GlobalLock); }
    if (kernel) { K32_MEMORY(GlobalUnlock); }
    if (kernel) { K32_MEMORY(LocalAlloc); }
    if (kernel) { K32_MEMORY(LocalReAlloc); }
    if (kernel) { K32_MEMORY(LocalFree); }
    if (kernel || memory) { K32_MEMORY(VirtualFree); }
    if (kernel || memory) { K32_MEMORY(VirtualQuery); }
    if (kernel || system) { K32_MEMORY(GlobalMemoryStatusEx); }
    if (kernel || system) { K32_MEMORY(GetSystemInfo); }
    if (kernel || system) { K32_MEMORY(GetNativeSystemInfo); }
#undef K32_MEMORY
    return 0;
}
}
