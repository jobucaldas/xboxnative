/* SPDX-License-Identifier: GPL-3.0-or-later
 * PE32 fixture for the API-set binder (host/x86/apiset.h). strlen is imported
 * through api-ms-win-crt-string-l1-1-0.dll and must bind and return 5.
 * frobnicate is imported through api-ms-win-foo-l1-1-0.dll, a name the binder
 * does not map. time is imported through api-ms-win-crt-time-l1-1-0.dll and must
 * stay unresolved: its width differs between msvcrt and the UCRT (apiset.h,
 * kWidthSensitive). Neither unresolved import is called. The raw symbol _time
 * is used so mingw's time->_time32 header rename does not change what is
 * imported. Freestanding apart from the SDK declarations; the import libraries
 * are built by x86-apiset.sh. */
#include <windows.h>
#include <string.h>

extern void frobnicate(void);
extern void time_import(void) __asm__("_time");
void* volatile keep_frob;
void* volatile keep_time;

void WINAPI apiset_start(void) {
    keep_frob = (void*)&frobnicate;
    keep_time = (void*)&time_import;
    ExitProcess(strlen("hello") == 5u ? 0u : 1u);
}
