/* SPDX-License-Identifier: GPL-3.0-or-later */
/* x86 stage 3: the other half of the cyclic load pair. dll-d imports
 * dllc_value from dll-c.dll (the back edge that a loader which re-maps
 * the first module turns into a second copy) and exports dllb_value.
 * Freestanding i686 PE32, no CRT. */
typedef unsigned int u32;
u32 __stdcall dllc_value(void);   /* from dll-c.dll: the back edge */

static u32 g_attach = 0;
u32 __stdcall dlld_attach_count(void) { return g_attach; }
u32 __stdcall dlld_value(void) { return 0xD1D0u + (u32)(dllc_value() != 0); }

int __stdcall DllMain(void *hinst, u32 reason, void *reserved) {
    (void)hinst; (void)reserved;
    if (reason == 1) ++g_attach;
    return 1;
}
