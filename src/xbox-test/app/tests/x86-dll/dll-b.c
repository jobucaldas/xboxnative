/* SPDX-License-Identifier: GPL-3.0-or-later */
/* x86 stage 3: leaf guest DLL. Freestanding i686 PE32, no CRT: DllMain plus
 * three named exports. DllMain records DLL_PROCESS_ATTACH in a global the exe
 * later reads through dllb_attach_marker, proving DllMain ran before use.
 * dllb_value is also resolved by ordinal through GetProcAddress (#2). */
typedef unsigned int u32;
static u32 g_attach = 0;

u32 __stdcall dllb_attach_marker(void) { return g_attach; }
u32 __stdcall dllb_value(void) { return 0xB00B5u; }
u32 __stdcall dllb_add(u32 a, u32 b) { return a + b; }

int __stdcall DllMain(void *hinst, u32 reason, void *reserved) {
    (void)hinst; (void)reserved;
    if (reason == 1) g_attach = 1;
    return 1;
}
