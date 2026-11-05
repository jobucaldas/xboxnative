/* Synthetic import fixture only; SPDX-License-Identifier: GPL-3.0-or-later */
#include <windows.h>
__declspec(dllimport) int fixture_named(int);
__declspec(dllimport) int fixture_ordinal(int);
__declspec(dllimport) int entry_marker(int);

BOOL WINAPI fixture_dll_entry(HINSTANCE module, DWORD reason, LPVOID reserved);
static BOOL (WINAPI * volatile entry_anchor)(HINSTANCE,DWORD,LPVOID) = fixture_dll_entry;
BOOL WINAPI fixture_dll_entry(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)module; (void)reserved;
    if (entry_anchor != fixture_dll_entry) return FALSE;
    if (reason == DLL_PROCESS_ATTACH) {
        int named = fixture_named(1), ordinal = fixture_ordinal(2);
        return entry_marker(named + ordinal) == named + ordinal;
    }
    return TRUE;
}
