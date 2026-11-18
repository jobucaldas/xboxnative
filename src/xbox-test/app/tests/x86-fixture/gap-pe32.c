/* SPDX-License-Identifier: GPL-3.0-or-later
 * Owned-free i686 (PE32) launcher diagnostic fixture. It imports one
 * x86-registry export and one absent API-set CRT export, keeps both by storing
 * their addresses in a volatile global, and never calls either. */
#include <windows.h>

__declspec(dllimport) DWORD __cdecl XInputGetState(DWORD, void*);
__declspec(dllimport) void __cdecl _aligned_free(void*);
void* volatile gap_keep;

void WINAPI gap_start(void) {
    gap_keep = (void*)&XInputGetState;
    gap_keep = (void*)&_aligned_free;
    ExitProcess(0u);
}
