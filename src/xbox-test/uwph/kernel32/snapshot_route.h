#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace kernel32_core {
// The UWP SDK hides Toolhelp declarations. Preserve the documented ABI.
struct SnapshotThreadEntry {
    DWORD dwSize,cntUsage,th32ThreadID,th32OwnerProcessID;
    LONG tpBasePri,tpDeltaPri;
    DWORD dwFlags;
};
static_assert(sizeof(SnapshotThreadEntry)==28,"THREADENTRY32 ABI");
// Thread/process snapshots use native IDs and handles, matching guest threads.
// Module snapshots describe OS-registered modules; mapped guest module queries
// need the separate loader registry and are not proved by this route's tests.
inline uintptr_t resolve_snapshot(const char* dll,const char* name) {
    if(!dll||!name||(_stricmp(dll,"kernel32.dll")&&_stricmp(dll,"kernelbase.dll")))return 0;
    HMODULE module=GetModuleHandleW(L"kernel32.dll");if(!module)return 0;
    if(!strcmp(name,"Thread32First")) {
        using First=BOOL (WINAPI*)(HANDLE,SnapshotThreadEntry*);
        static First first=reinterpret_cast<First>(reinterpret_cast<uintptr_t>(GetProcAddress(module,"Thread32First")));
        return reinterpret_cast<uintptr_t>(first);
    }
    if(!strcmp(name,"Thread32Next")) {
        using Next=BOOL (WINAPI*)(HANDLE,SnapshotThreadEntry*);
        static Next next=reinterpret_cast<Next>(reinterpret_cast<uintptr_t>(GetProcAddress(module,"Thread32Next")));
        return reinterpret_cast<uintptr_t>(next);
    }
    if(strcmp(name,"CreateToolhelp32Snapshot"))return 0;
    using Snapshot=HANDLE (WINAPI*)(DWORD,DWORD);
    static Snapshot snapshot=reinterpret_cast<Snapshot>(reinterpret_cast<uintptr_t>(GetProcAddress(module,"CreateToolhelp32Snapshot")));
    return reinterpret_cast<uintptr_t>(snapshot);
}
}
