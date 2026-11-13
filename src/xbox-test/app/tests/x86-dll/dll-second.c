/* SPDX-License-Identifier: GPL-3.0-or-later */
/* x86 stage 3: the second DLL of the SizeOfImage pair. Its preferred base is
 * the SAME as dll-zeroent's (both 0x10000000, the mingw default), so the
 * loader must relocate this one into the dedicated DLL arena — a walk that
 * still searched from the guest heap's old 0x10000000 base would either die
 * on the heap overlap or land inside dll-zeroent's reserved SizeOfImage
 * (Bugbot 4559749442/4559749445). The fixture proves the two markers stay
 * independent after both are loaded. Freestanding i686 PE32, no CRT. */
typedef unsigned int u32;

u32 second_marker = 0x2E7E1001u;
u32 __stdcall second_fn(void) { return 0x2E7E1002u; }
