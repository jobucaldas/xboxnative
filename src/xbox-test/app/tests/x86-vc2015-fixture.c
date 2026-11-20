// SPDX-License-Identifier: GPL-3.0-or-later
// PE32 fixture for x86-vc2015: one undeclared api-ms-win-crt name and one msvcp140 name,
// both taken by address so the imports are kept. Built by x86-vc2015.sh.
extern void _c_exit(void);
extern void _Mtx_lock(void*);
__declspec(dllimport) void __stdcall ExitProcess(unsigned int);
void (*volatile c_exit_ptr)(void) = _c_exit;
void (*volatile mtx_lock_ptr)(void*) = _Mtx_lock;
void _entry(void) { ExitProcess(0u); }
