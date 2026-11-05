// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
// Own CRT-free fixture: an inert Wine-shaped load config and an exercised DIR64 relocation.
__declspec(dllexport) const struct { DWORD size; BYTE rest[316]; } _load_config_used={0x140,{0}};
__declspec(dllexport) void* volatile relocation_anchor=(void*)&relocation_anchor;
BOOL WINAPI config_entry(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance; (void)reserved;
    if (reason==DLL_PROCESS_ATTACH) OutputDebugStringA("CONFIG attach");
    return relocation_anchor==(void*)&relocation_anchor;
}
