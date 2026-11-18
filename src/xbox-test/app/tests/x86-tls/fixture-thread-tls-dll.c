/* SPDX-License-Identifier: GPL-3.0-or-later
 * Freestanding PE32 DLL for the static-TLS thread integration fixture. */
typedef unsigned int u32;
#define WINAPI __attribute__((stdcall))
#define EXPORT __declspec(dllexport)

static u32 dll_attach_count, dll_detach_count;
static u32 callback_attach_count, callback_detach_count, detach_tls_value;
static u32 dll_tls_index;

__declspec(thread) __attribute__((section(".tls"), used)) static u32 dll_tls_value = 7;

static u32 fs_dword(u32 off) {
    u32 value;
    __asm__ volatile("movl %%fs:(%1), %0" : "=r"(value) : "r"(off));
    return value;
}
static u32 tls_value(void) {
    u32 slots = fs_dword(0x2c);
    return *(u32*)(*(u32*)(slots + 4 * dll_tls_index));
}
static void set_tls_value(u32 value) {
    u32 slots = fs_dword(0x2c);
    *(u32*)(*(u32*)(slots + 4 * dll_tls_index)) = value;
}

static int WINAPI dll_tls_callback(void* module, u32 reason, void* reserved) {
    (void)module; (void)reserved;
    if (reason == 2) { ++callback_attach_count; set_tls_value(20); }
    if (reason == 3) { ++callback_detach_count; detach_tls_value = tls_value(); }
    return 1;
}
__attribute__((used)) static u32 tls_callbacks[2] = {(u32)dll_tls_callback, 0};

typedef struct { u32 start_raw, end_raw, index_ptr, callbacks, zero_fill, characteristics; } TlsDir;
extern char __tls_start__, __tls_end__;
__attribute__((used)) static TlsDir tls_dir = {
    (u32)(u32*)&__tls_start__, (u32)(u32*)&__tls_end__,
    (u32)(u32*)&dll_tls_index, (u32)(u32*)&tls_callbacks, 4, 0
};

int WINAPI DllMain(void* module, u32 reason, void* reserved) {
    (void)module; (void)reserved;
    if (reason == 2) ++dll_attach_count;
    if (reason == 3) ++dll_detach_count;
    return 1;
}
EXPORT u32 dll_tls_touch(void) { set_tls_value(tls_value() + 1); return tls_value(); }
EXPORT u32 dll_tls_get(void) { return tls_value(); }
EXPORT u32 dll_attach_get(void) { return dll_attach_count; }
EXPORT u32 dll_detach_get(void) { return dll_detach_count; }
EXPORT u32 dll_callback_attach_get(void) { return callback_attach_count; }
EXPORT u32 dll_callback_detach_get(void) { return callback_detach_count; }
EXPORT u32 dll_detach_tls_get(void) { return detach_tls_value; }
