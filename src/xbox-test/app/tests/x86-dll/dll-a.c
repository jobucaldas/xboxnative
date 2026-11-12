/* SPDX-License-Identifier: GPL-3.0-or-later */
/* x86 stage 3: middle guest DLL. Freestanding i686 PE32, no CRT. Imports
 * dllb_value from dll-b.dll (a guest DLL, bound by the recursive loader) and
 * exports dll_a_sum for the exe: import resolution between guest PE32 DLLs. */
typedef unsigned int u32;
u32 __stdcall dllb_value(void);   /* from dll-b.dll, resolved at load */

u32 __stdcall dll_a_sum(u32 n) {
    u32 s = 0;
    for (u32 i = 1; i <= n; ++i) s += dllb_value() + i;
    return s;
}

int __stdcall DllMain(void *hinst, u32 reason, void *reserved) {
    (void)hinst; (void)reason; (void)reserved;
    return 1;
}
