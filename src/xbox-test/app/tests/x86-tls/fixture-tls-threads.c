/* SPDX-License-Identifier: GPL-3.0-or-later
 * Owned i686 (PE32) fixture: implicit TLS, TEB state and TLS callbacks per
 * thread. The main thread and CreateThread workers each touch a
 * __declspec(thread)-style variable of the exe's .tls section. MSDN "Using
 * Thread Local Storage": each thread has its own copy of the TLS data,
 * initialised from the template, and TLS callbacks get DLL_THREAD_ATTACH when a
 * thread starts and DLL_THREAD_DETACH when it exits (the initial thread gets no
 * THREAD_ATTACH). The variable is reached the way the compiler reaches
 * __declspec(thread) data: the running thread's TEB.ThreadLocalStoragePointer
 * (fs:[0x2C]) indexed by _tls_index, plus the variable's offset in the TLS data.
 * MSDN GetLastError: the last-error value is per thread, and a new thread starts
 * with 0. MSDN GetCurrentThreadId: the calling thread's id. MSDN ExitThread:
 * "Ends the calling thread" and does not return. Freestanding: no CRT. The
 * test script patches data-directory 9 to this file's TLS directory.
 * Workers run at the main thread's WaitForSingleObject, one at a time. */
typedef unsigned int u32;
#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
#define INF 0xFFFFFFFFu
IMP void* WINAPI GetStdHandle(u32);
IMP int WINAPI WriteFile(void*, const void*, u32, u32*, void*);
IMP u32 __cdecl strlen(const char*);
IMP void WINAPI ExitProcess(u32);
IMP void WINAPI ExitThread(u32);
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

static u32 fs_dword(u32 off) { u32 v; __asm__ volatile("movl %%fs:(%1), %0" : "=r"(v) : "r"(off)); return v; }

/* This thread's copy of a .tls variable, given the variable's template address. */
static u32 tls_addr(u32 tmpl) {
    u32 arr = fs_dword(0x2C);
    u32 block = *(u32*)(arr + 4 * tls_index);
    return block + (tmpl - (u32)&__tls_start__);
}
#define TLS_VAR(v) (*(u32*)tls_addr((u32)&(v)))

/* ExitThread through a volatile pointer: the compiler cannot assume it does not
 * return, so the code after a call that the surface must unwind is reachable
 * (and would run, if the surface returned into it). */
static void (WINAPI *volatile exit_fn)(u32) = ExitThread;

/* TLS callbacks. attach/detach record the thread they ran on; detach also reads
 * the variable (the thread's own copy) and, when g_detach_exit is set, calls
 * ExitThread and then records that the code after it ran (it must not). */
static u32 attach_calls, detach_calls, attach_tid, detach_tid, detach_seen;
static volatile int g_detach_exit;
static u32 g_after_detach_exit;
int WINAPI tls_callback(void* handle, u32 reason, void* reserved) {
    (void)handle; (void)reserved;
    if (reason == 2) { ++attach_calls; attach_tid = GetCurrentThreadId(); }
    if (reason == 3) {
        ++detach_calls; detach_tid = GetCurrentThreadId();
        detach_seen = TLS_VAR(tls_counter);
        if (g_detach_exit) { exit_fn(77); g_after_detach_exit = 1; }
    }
    return 1;
}
__attribute__((used)) static u32 tls_callbacks[2] = { (u32)tls_callback, 0 };

/* IMAGE_TLS_DIRECTORY32 (the test script points data-directory 9 at it). */
typedef struct { u32 start_raw, end_raw, index_ptr, callbacks, zero_fill, characteristics; } TlsDir;
__attribute__((used)) static TlsDir tls_dir = {
    (u32)(u32*)&__tls_start__, (u32)(u32*)&__tls_end__,
    (u32)(u32*)&tls_index, (u32)(u32*)&tls_callbacks, 4, 0
};

static u32 worker_seen, worker_err, worker_tid;
static u32 g_body_after;
/* 1: returns normally. */
static u32 WINAPI worker_ret(void* p) {
    (void)p;
    worker_tid = GetCurrentThreadId();
    worker_err = GetLastError();
    TLS_VAR(tls_counter) += 1;
    worker_seen = TLS_VAR(tls_counter);
    return 9;
}
/* 2: ExitThread in the body: the code after it must not run. */
static u32 WINAPI worker_exit(void* p) {
    (void)p;
    TLS_VAR(tls_counter) += 1;
    exit_fn(31);
    g_body_after = 1;
    return 0;
}
/* 3: returns 21; its detach callback calls ExitThread (g_detach_exit). */
static u32 WINAPI worker_detach(void* p) {
    (void)p;
    TLS_VAR(tls_counter) += 1;
    return 21;
}

static void num(const char* label, u32 v) {
    char b[48]; char* p = b; const char* l = label; while (*l) *p++ = *l++;
    *p++ = '='; char t[12]; int k = 0; if (!v) t[k++] = '0'; while (v) { t[k++] = (char)('0' + v % 10); v /= 10; }
    while (k) *p++ = t[--k];
    *p++ = '\n'; *p = 0; out(b);
}

/* After any worker: the main thread's TEB, LastError, thread id and its own
 * TLS copy are exactly as they were before the worker ran. */
static int main_intact(u32 self, u32 arr, u32 tid) {
    return fs_dword(0x18) == self && fs_dword(0x2C) == arr && GetLastError() == 5 &&
           GetCurrentThreadId() == tid && TLS_VAR(tls_counter) == 2;
}

void WINAPI fixture_tls_start(void) {
    out("x86-tls-threads: start\n");
    u32 main_tid = GetCurrentThreadId();
    u32 self = fs_dword(0x18), arr = fs_dword(0x2C);
    /* The main thread's copy: two increments, so a worker that reads the main
     * copy sees 2 where its own copy holds 1. */
    TLS_VAR(tls_counter) += 1;
    TLS_VAR(tls_counter) += 1;
    check(TLS_VAR(tls_counter) == 2 && tls_counter == 0);
    SetLastError(5);

    /* 1. A worker that returns: its own copy (1, not 2), its own thread id and
     * LastError 0; its detach callback sees the worker's copy and id. */
    u32 id1 = 0, code = 0;
    void* h1 = CreateThread(0, 0, (void*)worker_ret, 0, 0, &id1);
    check(h1 != 0 && id1 != 0 && id1 != main_tid);
    check(WaitForSingleObject(h1, INF) == 0);
    check(GetExitCodeThread(h1, &code) && code == 9);
    CloseHandle(h1);
    check(worker_seen == 1 && worker_tid == id1 && worker_err == 0);
    check(attach_calls == 1 && attach_tid == id1);
    check(detach_calls == 1 && detach_tid == id1 && detach_seen == 1);
    check(main_intact(self, arr, main_tid));

    /* 2. ExitThread in the body ends the thread: its exit code is 31, the code
     * after the call does not run, and the detach callbacks still run on it. */
    u32 id2 = 0;
    void* h2 = CreateThread(0, 0, (void*)worker_exit, 0, 0, &id2);
    check(WaitForSingleObject(h2, INF) == 0);
    check(GetExitCodeThread(h2, &code) && code == 31);
    CloseHandle(h2);
    check(g_body_after == 0 && attach_calls == 2 && detach_calls == 2 && detach_tid == id2);
    check(main_intact(self, arr, main_tid));

    /* 3. ExitThread in a detach callback ends only that callback: the thread
     * keeps its body's exit code (21), the code after ExitThread in the callback
     * does not run, and the main thread keeps running. */
    u32 id3 = 0;
    g_detach_exit = 1;
    void* h3 = CreateThread(0, 0, (void*)worker_detach, 0, 0, &id3);
    check(WaitForSingleObject(h3, INF) == 0);
    g_detach_exit = 0;
    check(GetExitCodeThread(h3, &code) && code == 21);
    CloseHandle(h3);
    check(g_after_detach_exit == 0 && detach_calls == 3 && detach_tid == id3);
    check(main_intact(self, arr, main_tid));

    out("x86-tls-threads: per-thread ok\n");
    num("checks", (u32)g_checks);
    num("failed", (u32)g_fail);
    ExitProcess(g_fail ? 100u + (u32)g_fail : 42u);
}
