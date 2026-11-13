/* SPDX-License-Identifier: GPL-3.0-or-later */
/* x86 stage 3: leaf guest DLL for the failed-load rollback check. It is mapped
 * by dll-fail.dll (which then fails) and LoadLibrary'd again by the exe. DllMain
 * counts DLL_PROCESS_ATTACH and leaf_marker returns the count, so the exe can
 * tell a fresh attach (1) from a stale unattached module (0) or a double attach
 * (2). Freestanding i686 PE32, no CRT. */
typedef unsigned int u32;
static u32 g_attach = 0;

u32 __stdcall leaf_marker(void) { return g_attach; }

int __stdcall DllMain(void *hinst, u32 reason, void *reserved) {
    (void)hinst; (void)reserved;
    if (reason == 1) g_attach++;
    return 1;
}
