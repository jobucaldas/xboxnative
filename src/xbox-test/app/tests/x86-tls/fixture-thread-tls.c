/* SPDX-License-Identifier: GPL-3.0-or-later
 * Freestanding PE32 executable for the static-TLS thread integration fixture.
 * The executable and tls-dll.dll each have an independent TLS template. */
typedef unsigned int u32;
#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
#define INF 0xFFFFFFFFu

IMP void* WINAPI GetStdHandle(u32);
IMP int WINAPI WriteFile(void*, const void*, u32, u32*, void*);
IMP u32 __cdecl strlen(const char*);
IMP void WINAPI ExitProcess(u32);
IMP void* WINAPI CreateThread(void*, u32, void*, void*, u32, u32*);
IMP u32 WINAPI WaitForSingleObject(void*, u32);
IMP int WINAPI GetExitCodeThread(void*, u32*);
IMP int WINAPI CloseHandle(void*);
IMP u32 WINAPI GetCurrentThreadId(void);

IMP u32 dll_tls_touch(void);
IMP u32 dll_tls_get(void);
IMP u32 dll_attach_get(void);
IMP u32 dll_detach_get(void);
IMP u32 dll_callback_attach_get(void);
IMP u32 dll_callback_detach_get(void);
IMP u32 dll_detach_tls_get(void);

static int failures, checks;
static u32 exe_tls_index, exe_attach_count, exe_detach_count, exe_detach_tls;
__declspec(thread) __attribute__((section(".tls"), used)) static u32 exe_tls_value;

static u32 fs_dword(u32 off) {
    u32 value;
    __asm__ volatile("movl %%fs:(%1), %0" : "=r"(value) : "r"(off));
    return value;
}
static u32 exe_tls(void) {
    u32 slots = fs_dword(0x2c);
    return *(u32*)(*(u32*)(slots + 4 * exe_tls_index));
}
static void set_exe_tls(u32 value) {
    u32 slots = fs_dword(0x2c);
    *(u32*)(*(u32*)(slots + 4 * exe_tls_index)) = value;
}
static void check(int value) { ++checks; if (!value) ++failures; }
static void out(const char* text) {
    u32 written = 0;
    WriteFile(GetStdHandle((u32)-11), text, strlen(text), &written, 0);
}
static int WINAPI exe_tls_callback(void* module, u32 reason, void* reserved) {
    (void)module; (void)reserved;
    if (reason == 2) { ++exe_attach_count; set_exe_tls(30); }
    if (reason == 3) { ++exe_detach_count; exe_detach_tls = exe_tls(); }
    return 1;
}
__attribute__((used)) static u32 tls_callbacks[2] = {(u32)exe_tls_callback, 0};
typedef struct { u32 start_raw, end_raw, index_ptr, callbacks, zero_fill, characteristics; } TlsDir;
extern char __tls_start__, __tls_end__;
__attribute__((used)) static TlsDir tls_dir = {
    (u32)(u32*)&__tls_start__, (u32)(u32*)&__tls_end__,
    (u32)(u32*)&exe_tls_index, (u32)(u32*)&tls_callbacks, 4, 0
};

static u32 worker(void* unused) {
    (void)unused;
    check(exe_tls() == 30);
    set_exe_tls(exe_tls() + 1);
    check(exe_tls() == 31);
    check(dll_tls_touch() == 21);
    return GetCurrentThreadId();
}

void WINAPI fixture_thread_tls_start(void) {
    out("x86-thread-tls: start\n");
    set_exe_tls(5);
    check(exe_tls() == 5 && dll_tls_get() == 7);
    u32 main_id = GetCurrentThreadId(), ids[2] = {}, code = 0;
    for (u32 i = 0; i < 2; ++i) {
        void* handle = CreateThread(0, 0, (void*)worker, 0, 0, &ids[i]);
        check(handle != 0 && ids[i] != 0 && ids[i] != main_id);
        check(WaitForSingleObject(handle, INF) == 0);
        check(GetExitCodeThread(handle, &code) && code == ids[i]);
        CloseHandle(handle);
    }
    check(ids[0] != ids[1]);
    check(exe_tls() == 5 && exe_attach_count == 2 && exe_detach_count == 2 && exe_detach_tls == 31);
    check(dll_tls_get() == 7 && dll_attach_get() == 2 && dll_detach_get() == 2);
    check(dll_callback_attach_get() == 2 && dll_callback_detach_get() == 2 && dll_detach_tls_get() == 21);
    check(exe_attach_count == 2);
    out("x86-thread-tls: per-thread exe+DLL TLS and notifications ok\n");
    char result[64];
    result[0] = 'c'; result[1] = 'h'; result[2] = 'e'; result[3] = 'c'; result[4] = 'k'; result[5] = 's'; result[6] = '=';
    u32 n = (u32)checks, digits = 0, divisor = 1;
    while (n / divisor >= 10) divisor *= 10;
    do { result[7 + digits++] = (char)('0' + (n / divisor) % 10); divisor /= 10; } while (divisor);
    result[7 + digits++] = '\n'; result[7 + digits++] = 'f'; result[7 + digits++] = 'a'; result[7 + digits++] = 'i'; result[7 + digits++] = 'l'; result[7 + digits++] = 'e'; result[7 + digits++] = 'd'; result[7 + digits++] = '=';
    n = (u32)failures; digits = 0; divisor = 1;
    while (n / divisor >= 10) divisor *= 10;
    do { result[15 + digits++] = (char)('0' + (n / divisor) % 10); divisor /= 10; } while (divisor);
    result[15 + digits++] = '\n'; result[15 + digits] = 0;
    out(result);
    ExitProcess(failures ? 100 + (u32)failures : 42);
}
