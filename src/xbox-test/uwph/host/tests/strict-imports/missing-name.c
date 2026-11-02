/* Synthetic failure fixture only; SPDX-License-Identifier: GPL-3.0-or-later */
#include <windows.h>
__declspec(dllimport) int absent_name(int);
__declspec(dllimport) int entry_marker(int);
BOOL WINAPI missing_name_dll_entry(HINSTANCE module, DWORD reason, LPVOID reserved);
static BOOL (WINAPI * volatile entry_anchor)(HINSTANCE,DWORD,LPVOID) = missing_name_dll_entry;
BOOL WINAPI missing_name_dll_entry(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)module; (void)reserved;
    if (entry_anchor != missing_name_dll_entry) return FALSE;
    if (reason == DLL_PROCESS_ATTACH) { entry_marker(0); absent_name(1); return FALSE; }
    return TRUE;
}
