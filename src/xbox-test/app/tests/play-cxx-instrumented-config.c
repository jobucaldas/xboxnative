// SPDX-License-Identifier: GPL-3.0-or-later
// Owned STRUCTURAL fixture for Play's msvc_x64 policy. Linked into a variant
// of play-cxx.dll in place of libcmt's loadcfg.obj, it gives the image the
// load-config shape measured on Megabonk/Unity modules: a 0x140 record with
// the genuine CRT cookie and guard slots, GuardFlags == CF_INSTRUMENTED
// (0x100, optional-header GUARD_CF clear) and a relocated
// VolatileMetadataPointer. The metadata bytes below are owned placeholders,
// not compiler-produced /volatileMetadata; the loader must never read them.
extern unsigned long long __security_cookie;
extern void* __guard_check_icall_fptr;
extern void* __guard_dispatch_icall_fptr;
extern void* __guard_xfg_check_icall_fptr;
extern void* __guard_xfg_dispatch_icall_fptr;
extern void* __guard_xfg_table_dispatch_icall_fptr;
extern void* __castguard_check_failure_os_handled_fptr;
extern void* __guard_memcpy_fptr;
static const unsigned char owned_volatile_metadata[24] = {24};
#ifndef PLAY_CXX_GUARD_FLAGS
#define PLAY_CXX_GUARD_FLAGS 0x100 // a different value is the negative control
#endif
// IMAGE_LOAD_CONFIG_DIRECTORY64, one pointer-sized slot per 8 bytes.
const void* const _load_config_used[0x140 / 8] = {
    [0x00 / 8] = (const void*)0x140,          // Size (TimeDateStamp 0)
    [0x58 / 8] = &__security_cookie,
    [0x70 / 8] = &__guard_check_icall_fptr,
    [0x78 / 8] = &__guard_dispatch_icall_fptr,
    [0x90 / 8] = (const void*)PLAY_CXX_GUARD_FLAGS, // GuardFlags CF_INSTRUMENTED
    [0x100 / 8] = owned_volatile_metadata,    // VolatileMetadataPointer
    [0x118 / 8] = &__guard_xfg_check_icall_fptr,
    [0x120 / 8] = &__guard_xfg_dispatch_icall_fptr,
    [0x128 / 8] = &__guard_xfg_table_dispatch_icall_fptr,
    [0x130 / 8] = &__castguard_check_failure_os_handled_fptr,
    [0x138 / 8] = &__guard_memcpy_fptr,
};
