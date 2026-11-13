/* SPDX-License-Identifier: GPL-3.0-or-later
 * Owned i686 (PE32) fixture with the real mingw CRT: printf/puts, malloc,
 * qsort, TLS, the heap, VirtualAlloc and the kernel32 version/module calls a
 * game's startup makes. It is an ordinary Windows program, so the same binary
 * runs under Wine (the cross-check): identical stdout and exit code. Every
 * value printed is deterministic; time and counters are only tested for
 * plausibility, never printed. */
#include <stdarg.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

static int g_fail, g_checks;
static void check(int ok) { ++g_checks; if (!ok) ++g_fail; }

/* Thread bodies. The exit body calls ExitThread through a volatile pointer, so
 * the compiler cannot treat the call as noreturn: if the surface lets the call
 * return, g_after_exit becomes 1. A real ExitThread never returns. */
static volatile LONG g_after_exit, g_flag;
static DWORD WINAPI ret_body(LPVOID p) { return (DWORD)(ULONG_PTR)p + 7; }
static DWORD WINAPI flag_body(LPVOID p) { (void)p; g_flag = 1; return 0; }
static VOID (WINAPI *volatile exit_thread_fn)(DWORD) = ExitThread;
static DWORD WINAPI exit_body(LPVOID p) {
    exit_thread_fn((DWORD)(ULONG_PTR)p + 9);
    g_after_exit = 1;
    return 0;
}

static int cmp_int(const void* a, const void* b) {
    int x = *(const int*)a, y = *(const int*)b;
    return x < y ? -1 : x > y;
}
static int cmp_str(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static int sum_varargs(int n, ...) {
    va_list ap; va_start(ap, n); int s = 0;
    for (int i = 0; i < n; ++i) s += va_arg(ap, int);
    va_end(ap); return s;
}

/* A struct with pointers and a double: exercises the 32-bit layouts a game's
 * own structs have, and printf's 64-bit double argument passing. */
struct Rec { const char* name; int id; double weight; };
static double total_weight(const struct Rec* r, int n) {
    double t = 0; for (int i = 0; i < n; ++i) t += r[i].weight; return t;
}

int main(void) {
    printf("x86-crt: start\n");
    /* CRT startup already ran: argc/argv/_environ exist and stdout works. */
    check(printf("%s", "") == 0);
    puts("x86-crt: stdio ok");

    /* Integer formatting, including the 64-bit and pointer forms. */
    char buf[128];
    int n = snprintf(buf, sizeof buf, "%d|%u|%x|%X|%o|%c|%s|%5.2f|%ld|%llu",
                     -42, 42u, 0xbeefu, 0xbeefu, 8u, 'Z', "str", 3.14159, -1234567L, 0x123456789aULL);
    check(n > 0 && strcmp(buf, "-42|42|beef|BEEF|10|Z|str| 3.14|-1234567|78187493530") == 0);
    printf("x86-crt: printf %s\n", buf);

    /* sprintf takes its varargs on the stack, not a va_list; snprintf truncates
     * and still returns the untruncated length; strchr finds the terminator.
     * The buffer is prefilled so a missing NUL shows up as trailing junk. */
    char sp[64];
    memset(sp, 0xAA, sizeof sp);
    int sn = sprintf(sp, "%d/%s/%.1f", 7, "hi", 1.5);
    check(sn == 8 && strcmp(sp, "7/hi/1.5") == 0 && sp[8] == 0);
    char tr[6];
    char ten[11]; memcpy(ten, "abcdefghij", sizeof ten);  /* opaque to the compiler */
    int tn = snprintf(tr, sizeof tr, "%s", ten);
    check(tn == 10 && strcmp(tr, "abcde") == 0);
    check(strchr("abc", 0) == "abc" + 3 && strrchr("a/b/c", '/') == "a/b/c" + 3);
    printf("x86-crt: sprintf %s %s\n", sp, tr);

    /* Heap: malloc/calloc/realloc/free with a real free list. */
    int* v = (int*)malloc(64 * sizeof(int));
    check(v != NULL);
    for (int i = 0; i < 64; ++i) v[i] = (i * 37 + 11) % 101;
    qsort(v, 64, sizeof(int), cmp_int);
    int sorted = 1; for (int i = 1; i < 64; ++i) sorted &= v[i - 1] <= v[i];
    check(sorted);
    v = (int*)realloc(v, 128 * sizeof(int));
    check(v != NULL && v[0] <= v[63]);
    free(v);
    int* z = (int*)calloc(16, sizeof(int));
    int zeroed = 1; for (int i = 0; i < 16; ++i) zeroed &= z[i] == 0;
    check(zeroed); free(z);
    puts("x86-crt: heap ok");

    /* Strings and a qsort with a guest comparator over guest pointers. */
    const char* words[] = {"zeta", "alpha", "mid", "beta"};
    qsort(words, 4, sizeof(char*), cmp_str);
    check(strcmp(words[0], "alpha") == 0 && strcmp(words[3], "zeta") == 0);
    check(strlen("hello") == 5 && strncmp("abcdef", "abcxyz", 3) == 0);
    char cat[32]; strcpy(cat, "ab"); strcat(cat, "cd");
    check(strcmp(cat, "abcd") == 0 && memcmp(cat, "abcz", 3) == 0);
    /* setlocale: the query and a request for "C" answer "C"; an unavailable
     * locale answers NULL. mingw's CRT calls this from its startup path on
     * some builds, so an unbound stub stops a program before main. */
    const char* loc_q = setlocale(LC_ALL, NULL);
    const char* loc_c = setlocale(LC_ALL, "C");
    const char* loc_bad = setlocale(LC_ALL, "xx_YY.UTF-8");
    check(loc_q && strcmp(loc_q, "C") == 0 && loc_c && strcmp(loc_c, "C") == 0 && loc_bad == NULL);
    puts("x86-crt: strings ok");

    /* Variadic guest code calling variadic host code. */
    check(sum_varargs(4, 1, 2, 3, 4) == 10);
    struct Rec recs[] = {{"a", 1, 0.5}, {"b", 2, 1.25}, {"c", 3, 2.25}};
    check(total_weight(recs, 3) == 4.0);
    printf("x86-crt: varargs %d %.2f\n", sum_varargs(3, 10, 20, 30), total_weight(recs, 3));

    /* kernel32: heap, TLS, virtual memory, module and version queries. */
    HANDLE h = HeapCreate(0, 4096, 0);
    check(h != NULL);
    void* hp = HeapAlloc(h, HEAP_ZERO_MEMORY, 256);
    check(hp != NULL);
    check(((unsigned char*)hp)[255] == 0);
    HeapFree(h, 0, hp); HeapDestroy(h);
    DWORD slot = TlsAlloc();
    check(slot != TLS_OUT_OF_INDEXES);
    check(TlsSetValue(slot, (LPVOID)0x1234) && TlsGetValue(slot) == (LPVOID)0x1234);
    TlsFree(slot);
    void* va = VirtualAlloc(NULL, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    check(va != NULL);
    memset(va, 0x5a, 65536);
    check(((unsigned char*)va)[65535] == 0x5a);
    DWORD old = 0;
    check(VirtualProtect(va, 65536, PAGE_READONLY, &old) != 0);
    MEMORY_BASIC_INFORMATION mbi;
    check(VirtualQuery(va, &mbi, sizeof mbi) == sizeof mbi);
    VirtualFree(va, 0, MEM_RELEASE);
    check(GetModuleHandleA(NULL) != NULL);
    /* FreeLibrary of the image handle succeeds; an unknown handle fails with
     * ERROR_MOD_NOT_FOUND, as Windows and Wine answer. IsDBCSLeadByteEx is
     * false on the single-byte and UTF-8 code pages this surface answers. Some
     * mingw builds import both from the CRT startup path, so an unbound stub
     * stops a program before main. */
    check(FreeLibrary(GetModuleHandleA(NULL)) != 0);
    SetLastError(0);
    check(FreeLibrary((HMODULE)(ULONG_PTR)0xDEAD0000) == 0 && GetLastError() == ERROR_MOD_NOT_FOUND);
    check(IsDBCSLeadByteEx(CP_ACP, 0x81) == 0 && IsDBCSLeadByteEx(CP_UTF8, 0xE3) == 0);
    check(GetTickCount() != 0 || GetTickCount() == 0);  /* callable, not printed */
    OSVERSIONINFOA vi; memset(&vi, 0, sizeof vi); vi.dwOSVersionInfoSize = sizeof vi;
    check(GetVersionExA(&vi) != 0 && vi.dwMajorVersion >= 5);
    /* OSVERSIONINFOEXA: the tail after szCSDVersion is wServicePackMajor,
     * wServicePackMinor, wSuiteMask, wProductType, wReserved. */
    OSVERSIONINFOEXA vx; memset(&vx, 0, sizeof vx); vx.dwOSVersionInfoSize = sizeof vx;
    check(GetVersionExA((OSVERSIONINFOA*)&vx) != 0 && vx.dwMajorVersion == 10 &&
          vx.wServicePackMajor == 0 && vx.wServicePackMinor == 0 && vx.wProductType == VER_NT_WORKSTATION);
    /* A 148-byte A struct must not be written past its end. */
    unsigned char vbuf[160];
    memset(vbuf, 0xEE, sizeof vbuf);
    *(DWORD*)vbuf = sizeof(OSVERSIONINFOA);
    check(GetVersionExA((OSVERSIONINFOA*)vbuf) != 0 && vbuf[148] == 0xEE && vbuf[159] == 0xEE);
    /* SYSTEMTIME is eight WORDs: a 4-byte store per field would smash the next
     * one (month would read back as the year). Ranges, not exact values, so the
     * same check holds under Wine's real clock. Nothing past the 16-byte struct
     * may be written. */
    unsigned char st[24];
    memset(st, 0xEE, sizeof st);
    GetSystemTime((SYSTEMTIME*)st);
    check(((WORD*)st)[0] >= 2020 && ((WORD*)st)[1] >= 1 && ((WORD*)st)[1] <= 12 &&
          ((WORD*)st)[3] >= 1 && ((WORD*)st)[3] <= 31 && ((WORD*)st)[4] <= 23);
    check(st[16] == 0xEE && st[23] == 0xEE);
    char mod[260];
    check(GetModuleFileNameA(NULL, mod, sizeof mod) > 0);
    /* Module names are case-insensitive on Windows: the image's own name in a
     * different case must find the same module. The launcher passes a
     * mixed-case executable name, so a case-sensitive module map misses here. */
    char up[260];
    int mi = 0;
    for (; mod[mi] && mi < (int)sizeof up - 1; ++mi)
        up[mi] = mod[mi] >= 'a' && mod[mi] <= 'z' ? (char)(mod[mi] - 32) : mod[mi];
    up[mi] = 0;
    check(GetModuleHandleA(up) != NULL && GetModuleHandleA(up) == GetModuleHandleA(NULL));
    /* GetModuleHandleA (MSDN): the modules a process has from its start answer
     * without LoadLibrary, case-insensitively, with or without ".dll" (the
     * extension is appended when omitted); the same name answers the same
     * handle. An unknown name is NULL with ERROR_MOD_NOT_FOUND (126). */
    check(GetModuleHandleA("KERNEL32.DLL") != NULL && GetModuleHandleA("kernel32") == GetModuleHandleA("kernel32.dll"));
    check(GetModuleHandleA("ntdll.dll") != NULL && GetModuleHandleA("NTDLL") == GetModuleHandleA("ntdll.dll"));
    check(GetModuleHandleA("msvcrt.dll") != NULL && GetModuleHandleA("kernelbase") != NULL);
    SetLastError(0);
    check(GetModuleHandleA("no-such-module.dll") == NULL && GetLastError() == ERROR_MOD_NOT_FOUND);
    SetLastError(0);
    check(GetModuleHandleW(L"no-such-module") == NULL && GetLastError() == ERROR_MOD_NOT_FOUND);
    /* GetStartupInfoW (processthreadsapi.h): fills a STARTUPINFOW of cb = 68
     * bytes on i686, the same size as the ANSI form (the string members differ
     * only in pointee type). Only cb is compared; the rest is host-defined. */
    STARTUPINFOW si; memset(&si, 0, sizeof si);
    GetStartupInfoW(&si);
    check(sizeof si == 68 && si.cb == 68);
    /* OpenEventA (synchapi.h) and its export: a probe via GetProcAddress must
     * find it; no named event exists, so NULL with ERROR_FILE_NOT_FOUND (2). */
    check(GetProcAddress(GetModuleHandleA("kernel32.dll"), "OpenEventA") != NULL);
    SetLastError(0);
    check(OpenEventA(SYNCHRONIZE, FALSE, "xbn-no-such-event") == NULL && GetLastError() == ERROR_FILE_NOT_FOUND);
    puts("x86-crt: kernel32 ok");

    /* Interlocked and critical sections on the single guest thread. */
    volatile LONG counter = 0;
    check(InterlockedIncrement(&counter) == 1 && InterlockedDecrement(&counter) == 0);
    check(InterlockedExchange(&counter, 7) == 0 && counter == 7);
    check(InterlockedCompareExchange(&counter, 9, 7) == 7 && counter == 9);
    CRITICAL_SECTION cs; InitializeCriticalSection(&cs);
    EnterCriticalSection(&cs); LeaveCriticalSection(&cs); DeleteCriticalSection(&cs);
    puts("x86-crt: sync ok");

    /* CreateThread (MSDN): lpThreadId receives a nonzero identifier, unique per
     * thread, that GetThreadId reports for the handle. GetExitCodeThread (MSDN)
     * answers the code the thread returned, or the code ExitThread was given.
     * ExitThread (MSDN) ends the calling thread and does not return, so
     * g_after_exit stays 0. Both bodies run at the first wait. */
    {
        DWORD tid_a = 0, tid_b = 0, code_a = 0, code_b = 0;
        HANDLE th_a = CreateThread(NULL, 0, ret_body, (LPVOID)(ULONG_PTR)3, 0, &tid_a);
        HANDLE th_b = CreateThread(NULL, 0, exit_body, (LPVOID)(ULONG_PTR)5, 0, &tid_b);
        check(th_a != NULL && th_b != NULL);
        check(tid_a != 0 && tid_b != 0 && tid_a != tid_b);
        check(GetThreadId(th_a) == tid_a && GetThreadId(th_b) == tid_b);
        check(WaitForSingleObject(th_a, INFINITE) == WAIT_OBJECT_0);
        check(WaitForSingleObject(th_b, INFINITE) == WAIT_OBJECT_0);
        check(GetExitCodeThread(th_a, &code_a) && code_a == 10);  /* returned 3 + 7 */
        check(GetExitCodeThread(th_b, &code_b) && code_b == 14);  /* ExitThread(5 + 9) */
        check(g_after_exit == 0);
        CloseHandle(th_a); CloseHandle(th_b);
    }
    /* Polling is how a game waits for its worker: `while (!flag) Sleep(1)` and
     * `while (GetExitCodeThread(...) == STILL_ACTIVE) Sleep(1)` (MSDN Sleep
     * yields; MSDN GetExitCodeThread reports STILL_ACTIVE until the thread
     * exits). The worker only runs at a yield point here, so each loop is
     * bounded: a surface that never runs the worker fails the check instead of
     * hanging the test. */
    {
        HANDLE th_f = CreateThread(NULL, 0, flag_body, NULL, 0, NULL);
        int spins = 0;
        while (!g_flag && spins < 1000) { Sleep(1); ++spins; }
        check(g_flag == 1 && spins < 1000);
        WaitForSingleObject(th_f, INFINITE);
        CloseHandle(th_f);

        HANDLE th_p = CreateThread(NULL, 0, ret_body, (LPVOID)(ULONG_PTR)1, 0, NULL);
        DWORD code = STILL_ACTIVE;
        spins = 0;
        while (GetExitCodeThread(th_p, &code) && code == STILL_ACTIVE && spins < 1000) { Sleep(1); ++spins; }
        check(code == 8 && spins < 1000);  /* returned 1 + 7 */
        CloseHandle(th_p);
    }
    puts("x86-crt: threads ok");

    /* strftime with the real CRT prototype: size_t strftime(char *dest,
     * size_t maxsize, const char *format, const struct tm *tm) — dest first,
     * format third. The struct tm is filled by hand from a fixed timestamp so
     * the same check passes under Wine's real strftime: 2026-11-09 is a
     * Monday, 13:26:40, day 313 of the year. */
    {
        struct tm tmv;
        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = 2026 - 1900; tmv.tm_mon = 10; tmv.tm_mday = 9;
        tmv.tm_hour = 13; tmv.tm_min = 26; tmv.tm_sec = 40;
        tmv.tm_wday = 1; tmv.tm_yday = 312;
        char tsb[64];
        size_t tn2 = strftime(tsb, sizeof tsb, "%Y-%m-%d %H:%M:%S %a", &tmv);
        check(tn2 == 23 && tn2 < sizeof tsb);
        printf("x86-crt: strftime %s\n", tsb);
        /* The formatted fields must land in the buffer in the right places:
         * year first (dest is arg 0), weekday last (from arg 3's tm_wday).
         * With the arguments swapped the formatter reads its format from the
         * uninitialized destination and writes garbage or nothing. */
        check(tsb[0] == '2' && tsb[1] == '0' && tsb[2] == '2' && tsb[3] == '6');
        check(tsb[4] == '-' && tsb[5] == '1' && tsb[6] == '1' && tsb[7] == '-' && tsb[8] == '0' && tsb[9] == '9');
        check(tsb[10] == ' ' && tsb[11] == '1' && tsb[12] == '3');
        check(tsb[20] == 'M' && tsb[21] == 'o');  /* "Mo" of Monday at the tail */
    }
    printf("x86-crt: time ok\n");

    /* __iob_func is a FUNCTION in the real UCRT/stdint headers: FILE*
     * __iob_func(void) returns the stdio table pointer. A surface that binds
     * it as data puts the table's bytes in the IAT, and a call through the
     * IAT jumps at data (ExecuteViolation at the _iob page). Called through
     * the IAT it must return the table pointer that stdin/stdout/stderr
     * live in; dereferencing stdin/stdout/stderr proves the table is real. */
    {
        FARPROC pfn = GetProcAddress(LoadLibraryA("msvcrt.dll"), "__iob_func");
        FILE* (__cdecl *iob_func)(void) = (FILE* (*)(void))(void(*)(void))pfn;
        check(iob_func != NULL);
        if (iob_func) {
            FILE* tbl = iob_func();
            check(tbl != NULL);
            check((FILE*)stdin >= tbl && (FILE*)stdin < tbl + 3);
            check((const char*)stdout >= (const char*)tbl && (const char*)stdout - (const char*)tbl < (ptrdiff_t)(3 * sizeof(FILE)));
            check(ferror(stdin) == 0);  /* the table's FILE structs are usable */
        }
    }
    puts("x86-crt: iob ok");

    printf("x86-crt: checks %d failed %d\n", g_checks, g_fail);
    return g_fail ? 100 + g_fail : 42;
}
