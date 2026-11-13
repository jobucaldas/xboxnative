/* SPDX-License-Identifier: GPL-3.0-or-later
 * Owned-free i686 (PE32) fixture for the launcher's gap list. It imports one
 * kernel32 export that the x86 surface does not register (GetSystemFirmwareTable,
 * sysinfoapi.h), keeps the import by storing its address in a volatile global,
 * and never calls it. A run must exit 0 and name exactly that import as an
 * unresolved one. */
#include <windows.h>

void* volatile gap_keep;

void WINAPI gap_start(void) {
    gap_keep = (void*)&GetSystemFirmwareTable;
    ExitProcess(0u);
}
