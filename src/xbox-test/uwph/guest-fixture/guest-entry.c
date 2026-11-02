/* Synthetic no-CRT x64 guest fixture; SPDX-License-Identifier: GPL-3.0-or-later */
#include <windows.h>
__declspec(dllimport) DWORD WINAPI entry_marker(DWORD);
DWORD WINAPI guest_entry(void);
static DWORD (WINAPI * volatile anchor)(void) = guest_entry;
DWORD WINAPI guest_entry(void) {
    if (anchor != guest_entry) return 1;
    entry_marker(42);
    return 42;
}
