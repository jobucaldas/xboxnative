// SPDX-License-Identifier: GPL-3.0-or-later
// CRT-free PE32 fixture: exercise a real msvcr110.dll strlen import.
typedef unsigned int size_t;
__declspec(dllimport) size_t __cdecl strlen(const char*);
__declspec(dllimport) __attribute__((stdcall)) void ExitProcess(unsigned int);

void entry(void) {
    ExitProcess(strlen("donor") == 5 ? 42 : 7);
}
