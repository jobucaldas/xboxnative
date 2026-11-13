/* SPDX-License-Identifier: GPL-3.0-or-later */
/* x86 stage 3: a data DLL with NO entry point (AddressOfEntryPoint == 0,
 * built with ld --entry 0) and a huge SizeOfImage (a 4 MiB uninitialised
 * section over a few KiB of file) — the discriminating image for two loader
 * checks:
 * - entry RVA 0 must mean "no DllMain": a loader that tests the absolute
 *   entry (base + RVA, never zero for a mapped image) calls the image base
 *   as code (Bugbot 4559749424);
 * - the reservation must cover SizeOfImage, not the file size, so a second
 *   DLL cannot be placed over this one's uninitialised tail (Bugbot
 *   4559749445).
 * Exports two markers: a readable dword and a function (called through
 * GetProcAddress by the fixture to prove the mapping is executable where it
 * counts, not just reserved). Freestanding i686 PE32, no CRT. */
typedef unsigned int u32;

/* .data marker: a reader proves the initialized part of the image mapped. */
u32 zeroent_marker = 0x2E7E0001u;

/* Big uninitialised section (.bss): forces SizeOfImage far past the file
 * size. The tail dword lives ~3.9 MiB in; the fixture reads it through a
 * getter to prove the whole reservation is this image's. */
static u32 bss_tail[1000 * 1024];  /* ~3.9 MiB of BSS */
u32 __stdcall zeroent_tail_get(void) { return bss_tail[999 * 1024]; }

void __stdcall zeroent_tail_set(u32 v) { bss_tail[999 * 1024] = v; }

/* No DllMain at all: with --entry 0 the PE's AddressOfEntryPoint field is 0,
 * which is the point of this image. The linker needs a symbol; the entry is
 * forced to 0 on the command line. */
u32 __stdcall zeroent_fn(void) { return 0x2E7E0002u; }
