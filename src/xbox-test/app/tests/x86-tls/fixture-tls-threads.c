/* SPDX-License-Identifier: GPL-3.0-or-later
 * Owned i686 (PE32) fixture: implicit TLS and TEB state per thread. The main
 * thread and a worker from CreateThread each increment a __declspec(thread)
 * variable of the exe's .tls section. MSDN "Using Thread Local Storage": each
 * thread has its own copy of the TLS data, initialised from the template, and
 * TLS callbacks get DLL_THREAD_ATTACH when a thread starts and
 * DLL_THREAD_DETACH when it exits (the initial thread gets no THREAD_ATTACH).
 * The variable is reached the way the compiler reaches __declspec(thread) data:
 * the running thread's TEB.ThreadLocalStoragePointer (fs:[0x2C]) indexed by
 * _tls_index, plus the variable's offset in the TLS data. MSDN GetLastError:
 * the last-error value is per thread, and a new thread starts with 0. MSDN
 * GetCurrentThreadId: the calling thread's id, distinct per thread. Freestanding:
 * no CRT. The test script patches data-directory 9 to this file's TLS directory.
 * The same binary runs under real Windows, which does all of this itself. */
typedef unsigned int u32;
#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
IMP void* WINAPI GetStdHandle(u32);
IMP int WINAPI WriteFile(void*, const void*, u32, u32*, void*);
IMP u32 __cdecl strlen(const char*);
IMP void WINAPI ExitProcess(u32);
IMP void* WINAPI CreateThread(void*, u32, void*, void*, u32, u32*);
IMP u32 WINAPI WaitForSingleObject(void*, u32);
IMP int WINAPI GetExitCodeThread(void*, u32*);
IMP int WINAPI CloseHandle(void*);
IMP u32 WINAPI GetLastError(void);
IMP void WINAPI SetLastError(u32);
IMP u32 WINAPI GetCurrentThreadId(void);

extern char __tls_start__, __tls_end__;

static int g_fail, g_checks;
static void check(int ok) { ++g_checks; if (!ok) ++g_fail; }

static void out(const char* s) {
    u32 n = 0; WriteFile(GetStdHandle((u32)-11), s, strlen(s), &n, 0);
}

/* The per-thread variable (raw data 0) and the index the directory names. */
__attribute__((section(".tls"), used)) static u32 tls_counter = 0;
static u32 tls_index;  /* AddressOfIndex target: slot 0 for the exe */

/* TLS callbacks: record the reason and the thread they ran on. */
static u32 attach_calls, detach_calls, attach_tid, detach_tid;
int WINAPI tls_callback(void* handle, u32 reason, void* reserved) {
    (void)handle; (void)reserved;
    if (reason == 2) { ++attach_calls; attach_tid = GetCurrentThreadId(); }
    if (reason == 3) { ++detach_calls; detach_tid = GetCurrentThreadId(); }
    return 1;
}
__attribute__((used)) static u32 tls_callbacks[2] = { (u32)tls_callback, 0 };

/* IMAGE_TLS_DIRECTORY32 (the test script points data-directory 9 at it). */
typedef struct { u32 start_raw, end_raw, index_ptr, callbacks, zero_fill, characteristics; } TlsDir;
__attribute__((used)) static TlsDir tls_dir = {
    (u32)(u32*)&__tls_start__, (u32)(u32*)&__tls_end__,
    (u32)(u32*)&tls_index, (u32)(u32*)&tls_callbacks, 4, 0
};

static u32 fs_dword(u32 off) { u32 v; __asm__ volatile("movl %%fs:(%1), %0" : "=r"(v) : "r"(off)); return v; }

/* This thread's copy of a .tls variable, given the variable's template address. */
static u32 tls_addr(u32 tmpl) {
    u32 arr = fs_dword(0x2C);
    u32 block = *(u32*)(arr + 4 * tls_index);
    return block + (tmpl - (u32)&__tls_start__);
}
#define TLS_VAR(v) (*(u32*)tls_addr((u32)&(v)))

static u32 worker_seen, worker_err, worker_tid;
static u32 WINAPI worker(void* p) {
    (void)p;
    worker_tid = GetCurrentThreadId();
    worker_err = GetLastError();
    TLS_VAR(tls_counter) += 1;
    worker_seen = TLS_VAR(tls_counter);
    return 9;
}

static void num(const char* label, u32 v) {
    char b[48]; char* p = b; const char* l = label; while (*l) *p++ = *l++;
    *p++ = '='; char t[12]; int k = 0; if (!v) t[k++] = '0'; while (v) { t[k++] = (char)('0' + v % 10); v /= 10; }
    while (k) *p++ = t[--k];
    *p++ = '\n'; *p = 0; out(b);
}

void WINAPI fixture_tls_start(void) {
    out("x86-tls-threads: start\n");
    u32 main_tid = GetCurrentThreadId();
    TLS_VAR(tls_counter) += 1;
    check(TLS_VAR(tls_counter) == 1);

    SetLastError(5);
    u32 worker_id = 0, code = 0;
    void* th = CreateThread(0, 0, (void*)worker, 0, 0, &worker_id);
    check(th != 0 && worker_id != 0 && worker_id != main_tid);
    check(WaitForSingleObject(th, 0xFFFFFFFFu) == 0);
    check(GetExitCodeThread(th, &code) && code == 9);
    CloseHandle(th);

    /* Each thread has its own copy: the worker's increment left the main
     * thread's copy at 1, and the worker saw its own 1, not the main copy's 2.
     * The template in the image never changes. */
    check(TLS_VAR(tls_counter) == 1 && worker_seen == 1);
    check(tls_counter == 0);
    /* The worker's TEB: its own thread id and a clean LastError; the main
     * thread's LastError (5) is untouched by the worker. */
    check(worker_tid == worker_id && worker_err == 0 && GetLastError() == 5);
    /* TLS callbacks: DLL_THREAD_ATTACH and DLL_THREAD_DETACH once each, both
     * on the worker's thread (the initial thread gets no THREAD_ATTACH). */
    check(attach_calls == 1 && attach_tid == worker_id);
    check(detach_calls == 1 && detach_tid == worker_id);
    out("x86-tls-threads: per-thread ok\n");
    num("checks", (u32)g_checks);
    num("failed", (u32)g_fail);
    ExitProcess(g_fail ? 100u + (u32)g_fail : 42u);
}
