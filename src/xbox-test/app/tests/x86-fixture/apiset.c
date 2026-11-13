/* SPDX-License-Identifier: GPL-3.0-or-later
 * PE32 fixture for the API-set binder (host/x86/apiset.h). Imports, by contract:
 *  - strlen through api-ms-win-crt-string-l1-1-0.dll: binds, returns 5.
 *  - _get_osfhandle through api-ms-win-crt-filesystem-l1-1-0.dll: binds (intptr_t
 *    is 32-bit on i386) and is called on fd 1, which must not be -1.
 *  - time through api-ms-win-crt-time-l1-1-0.dll: stays unresolved (time_t is
 *    64-bit in the UCRT; apiset.h kWidthSensitive).
 *  - _lseeki64 through api-ms-win-crt-filesystem-l1-1-0.dll: stays unresolved
 *    (64-bit offset and return; kWidthSensitive).
 *  - frobnicate through api-ms-win-foo-l1-1-0.dll: stays unresolved (not mapped).
 * The unresolved imports are never called. Symbol names are the import-library
 * spellings x86-apiset.sh produces (dlltool prefixes one underscore to each def
 * name): _time, __lseeki64, __get_osfhandle. Freestanding apart from the SDK. */
#include <windows.h>
#include <string.h>

extern void frobnicate(void);
extern void time_import(void) __asm__("_time");
extern void lseek_import(void) __asm__("__lseeki64");
extern int get_osfhandle_import(int) __asm__("__get_osfhandle");
void* volatile keep_frob;
void* volatile keep_time;
void* volatile keep_lseek;

void WINAPI apiset_start(void) {
    keep_frob = (void*)&frobnicate;
    keep_time = (void*)&time_import;
    keep_lseek = (void*)&lseek_import;
    int h = get_osfhandle_import(1);
    ExitProcess((strlen("hello") == 5u && h != -1) ? 0u : 1u);
}
