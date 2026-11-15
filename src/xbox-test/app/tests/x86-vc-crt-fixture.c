// SPDX-License-Identifier: GPL-3.0-or-later
// CRT-free PE32 fixture: exercise real msvcr110.dll heap and string imports.
typedef unsigned int size_t;
__declspec(dllimport) size_t __cdecl strlen(const char*);
__declspec(dllimport) void* __cdecl malloc(size_t);
__declspec(dllimport) void __cdecl free(void*);
__declspec(dllimport) __attribute__((stdcall)) void ExitProcess(unsigned int);

void entry(void) {
    void* allocation = malloc(16);
    if (!allocation) ExitProcess(8);
    free(allocation);
    ExitProcess(strlen("donor") == 5 ? 42 : 7);
}
