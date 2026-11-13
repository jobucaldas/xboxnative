/* SPDX-License-Identifier: GPL-3.0-or-later
 * PE32 fixture for the API-set binder (host/x86/apiset.h). strlen is imported
 * through api-ms-win-crt-string-l1-1-0.dll and must bind and return 5.
 * frobnicate is imported through api-ms-win-foo-l1-1-0.dll, a name the binder
 * does not map: it must stay unresolved, and it is never called. Freestanding
 * apart from the SDK declarations; the import libraries are built by
 * x86-apiset.sh. */
#include <windows.h>
#include <string.h>

extern void frobnicate(void);
void* volatile keep_frob;

void WINAPI apiset_start(void) {
    keep_frob = (void*)&frobnicate;
    ExitProcess(strlen("hello") == 5u ? 0u : 1u);
}
