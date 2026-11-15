/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A DLL whose absolute pointer needs a HIGHLOW base relocation. */
typedef unsigned int u32;
static volatile u32 value = 42;
static u32 * volatile value_ptr = (u32 *)&value;

__declspec(dllexport) u32 __stdcall collision_fn(void) { return *value_ptr; }

int __stdcall DllMain(void *hinst, u32 reason, void *reserved) {
    (void)hinst; (void)reason; (void)reserved;
    return 1;
}
