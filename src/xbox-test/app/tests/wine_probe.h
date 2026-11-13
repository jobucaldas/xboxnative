// SPDX-License-Identifier: GPL-3.0-or-later
// Wine detection for the fixtures' platform gates, in one testable place.
// The marker is Wine's own ntdll export wine_get_version; the console's ntdll
// never exports it. Detection must not go through LoadLibraryW/GetProcAddress:
// Play hooks both and answers the lookup its own way, which could decide the
// answer. Instead the probe reads the export table straight out of the loaded
// ntdll image's PE headers in memory — no loader API in the path, so nothing
// Play does can influence the result. A host without the marker is treated as
// the console: every gated pass runs, nothing is skipped.
#pragma once
#include <windows.h>

namespace wine_probe {
// True only when the loaded ntdll's export table contains the exact name
// "wine_get_version". Any structural surprise (no module, bad MZ/PE magic,
// no export directory) answers false — the console path.
inline bool on_wine() {
    HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");
    if(!ntdll)return false;
    const auto* base=static_cast<const unsigned char*>(static_cast<const void*>(ntdll));
    const auto* dos=static_cast<const IMAGE_DOS_HEADER*>(static_cast<const void*>(base));
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE)return false;
    const auto& exportDir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!exportDir.VirtualAddress)return false;
    const auto* exports=reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base+exportDir.VirtualAddress);
    const DWORD* names=reinterpret_cast<const DWORD*>(base+exports->AddressOfNames);
    for(DWORD i=0;i<exports->NumberOfNames;++i){
        const char* name=reinterpret_cast<const char*>(base+names[i]);
        // Byte-exact match on the export name; no string API that a hook
        // could intercept.
        const char* want="wine_get_version";
        const char* a=name;const char* b=want;
        while(*a&&*a==*b){++a;++b;}
        if(*a==*b)return true;   // both ended: exact match
    }
    return false;
}
}
