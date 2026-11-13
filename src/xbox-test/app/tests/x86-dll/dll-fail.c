/* SPDX-License-Identifier: GPL-3.0-or-later */
/* x86 stage 3: a DLL whose load must fail after its first dependency is mapped.
 * Its imports bind dll-leaf.dll first (mapped), then dll-corrupt.dll, whose file
 * the driver replaces with bytes that are not a PE image, so the load throws
 * part-way. Freestanding i686 PE32, no CRT. */
typedef unsigned int u32;

/* Both take no arguments, so __cdecl and __stdcall agree on the call; __cdecl
 * keeps the import name undecorated, matching the dlltool import libraries. */
u32 __cdecl leaf_marker(void);
u32 __cdecl corrupt_fn(void);

int __stdcall DllMain(void *hinst, u32 reason, void *reserved) {
    (void)hinst; (void)reason; (void)reserved;
    return 1;
}

u32 __stdcall fail_fn(void) { return leaf_marker() + corrupt_fn(); }

