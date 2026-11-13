/* SPDX-License-Identifier: GPL-3.0-or-later */
/* x86 stage 3: one half of a cyclic load pair (dll-c imports dll-d,
 * dll-d imports dll-c). Windows' loader maps each module once and the
 * cycle resolves against the in-progress mapping; a loader that maps a
 * module again on the back edge runs each DllMain twice (the attach
 * counters catch it) or rebinds a second copy (the base check catches
 * it). Freestanding i686 PE32, no CRT. */
typedef unsigned int u32;
u32 __stdcall dlld_value(void);   /* from dll-d.dll: the back edge */

static u32 g_attach = 0;
u32 __stdcall dllc_attach_count(void) { return g_attach; }
u32 __stdcall dllc_value(void) { return 0xC0C0u; }
u32 __stdcall dllc_sum(u32 n) { u32 s = 0; for (u32 i = 1; i <= n; ++i) s += dlld_value() + i; return s; }

int __stdcall DllMain(void *hinst, u32 reason, void *reserved) {
    (void)hinst; (void)reserved;
    if (reason == 1) ++g_attach;
    return 1;
}
