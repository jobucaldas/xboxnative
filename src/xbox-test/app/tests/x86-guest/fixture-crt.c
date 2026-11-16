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
#include <wchar.h>
#include <time.h>
#include <windows.h>
#include <mmsystem.h>
#include <shlwapi.h>

static int g_fail, g_checks;
static void check(int ok) { ++g_checks; if (!ok) ++g_fail; }

/* Thread bodies. The exit body calls ExitThread through a volatile pointer, so
 * the compiler cannot treat the call as noreturn: if the surface lets the call
 * return, g_after_exit becomes 1. A real ExitThread never returns. */
static volatile LONG g_after_exit, g_flag;
static HANDLE g_delay_event;
static DWORD WINAPI ret_body(LPVOID p) { return (DWORD)(ULONG_PTR)p + 7; }
static DWORD WINAPI flag_body(LPVOID p) { (void)p; g_flag = 1; return 0; }
static DWORD WINAPI delayed_event_body(LPVOID p) {
    (void)p; Sleep(20); g_flag = 1; SetEvent(g_delay_event); return 0;
}
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
static int same_w(const wchar_t* a, const wchar_t* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

struct enum_state { int count, valid, stop_after; };
static BOOL CALLBACK enum_resource_a(HMODULE module, LPCSTR type, LPSTR name, LONG_PTR opaque) {
    struct enum_state* state = (struct enum_state*)opaque;
    int index = state->count++;
    if (module != GetModuleHandle(NULL) || type != RT_RCDATA) state->valid = 0;
    HRSRC resource = FindResourceA(module, name, type);
    if (!resource || !SizeofResource(module, resource) || !LockResource(LoadResource(module, resource))) state->valid = 0;
    if (index == 0) state->valid &= !IS_INTRESOURCE(name) && strcmp(name, "ALPHA") == 0;
    else if (index == 1) state->valid &= !IS_INTRESOURCE(name) && strcmp(name, "BETA") == 0;
    else if (index == 2) state->valid &= IS_INTRESOURCE(name) && (ULONG_PTR)name == 101;
    else if (index == 3) state->valid &= IS_INTRESOURCE(name) && (ULONG_PTR)name == 202;
    else state->valid = 0;
    return !state->stop_after || state->count < state->stop_after;
}
static BOOL CALLBACK enum_resource_w(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR opaque) {
    struct enum_state* state = (struct enum_state*)opaque;
    int index = state->count++;
    if (module != GetModuleHandle(NULL) || (ULONG_PTR)type != (ULONG_PTR)RT_RCDATA) state->valid = 0;
    HRSRC resource = FindResourceW(module, name, type);
    if (!resource || !SizeofResource(module, resource) || !LockResource(LoadResource(module, resource))) state->valid = 0;
    if (index == 0) state->valid &= !IS_INTRESOURCE(name) && name[0] == L'A' && name[1] == L'L' && name[2] == L'P' && name[3] == L'H' && name[4] == L'A' && name[5] == 0;
    else if (index == 1) state->valid &= !IS_INTRESOURCE(name) && name[0] == L'B' && name[1] == L'E' && name[2] == L'T' && name[3] == L'A' && name[4] == 0;
    else if (index == 2) state->valid &= IS_INTRESOURCE(name) && (ULONG_PTR)name == 101;
    else if (index == 3) state->valid &= IS_INTRESOURCE(name) && (ULONG_PTR)name == 202;
    else state->valid = 0;
    return !state->stop_after || state->count < state->stop_after;
}

static void test_resources(void) {
    HRSRC resource = FindResourceA(NULL, MAKEINTRESOURCEA(101), RT_RCDATA);
    check(resource != NULL && SizeofResource(NULL, resource) == 4);
    HGLOBAL loaded = LoadResource(NULL, resource);
    const unsigned char* bytes = (const unsigned char*)LockResource(loaded);
    check(bytes && bytes[0] == 0x78 && bytes[1] == 0x56 && bytes[2] == 0x34 && bytes[3] == 0x12);
    check(resource == FindResourceA(NULL, "#101", "#10"));
    check(resource == FindResourceExA(NULL, RT_RCDATA, MAKEINTRESOURCEA(101), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)));
    char text[32]; wchar_t wide[32];
    check(LoadStringA(NULL, 7, text, sizeof text) == 12 && strcmp(text, "Pug resource") == 0);
    int wide_length = LoadStringW(NULL, 7, wide, 32), wide_equal = wide_length == 12 && wide[12] == 0;
    for (int i = 0; i < 12 && wide_equal; ++i) wide_equal = wide[i] == L"Pug resource"[i];
    check(wide_equal);
    struct enum_state state = {0, 1, 0};
    int enum_a_result = EnumResourceNamesA(NULL, RT_RCDATA, enum_resource_a, (LONG_PTR)&state);
    check(enum_a_result && state.count == 4 && state.valid);
    state = (struct enum_state){0, 1, 1};
    SetLastError(0);
    int enum_stop_result = EnumResourceNamesA(NULL, RT_RCDATA, enum_resource_a, (LONG_PTR)&state);
    DWORD enum_stop_error = GetLastError();
    int enum_stop_ok = !enum_stop_result && state.count == 1 && enum_stop_error == ERROR_RESOURCE_ENUM_USER_STOP;
    check(enum_stop_ok);
    state = (struct enum_state){0, 1, 0};
    check(EnumResourceNamesW(NULL, MAKEINTRESOURCEW(10), enum_resource_w, (LONG_PTR)&state) && state.count == 4 && state.valid);
    SetLastError(0);
    check(!EnumResourceNamesA(NULL, MAKEINTRESOURCEA(11), enum_resource_a, 0) && GetLastError() == ERROR_RESOURCE_TYPE_NOT_FOUND);
    SetLastError(0);
    check(FindResourceA(NULL, MAKEINTRESOURCEA(102), RT_RCDATA) == NULL && GetLastError() == ERROR_RESOURCE_NAME_NOT_FOUND);
    SetLastError(0);
    check(FindResourceA(NULL, MAKEINTRESOURCEA(101), MAKEINTRESOURCEA(11)) == NULL && GetLastError() == ERROR_RESOURCE_TYPE_NOT_FOUND);
    check(FreeResource(loaded) == 0);
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

static void test_winmm_shlwapi(void) {
    JOYINFO joy = {0}; JOYINFOEX joyex = {0}; JOYCAPSA joya = {0}; JOYCAPSW joyw = {0};
    WAVEOUTCAPSA wavea = {0}; WAVEOUTCAPSW wavew = {0}; WAVEINCAPSA wavein_a = {0}; WAVEINCAPSW wavein_w = {0};
    MMTIME position = {0}; MMTIME system_time = {0}; TIMECAPS caps = {0}; char error_text[32] = {0}; wchar_t error_w[32] = {0};
    check(joyGetNumDevs() == 0 && joyGetPos(0, &joy) == JOYERR_UNPLUGGED &&
          joyGetPosEx(0, &joyex) == JOYERR_UNPLUGGED &&
          joyGetDevCapsA(0, &joya, sizeof joya) == JOYERR_UNPLUGGED &&
          joyGetDevCapsW(0, &joyw, sizeof joyw) == JOYERR_UNPLUGGED);
    check(waveOutGetNumDevs() == 0 && waveOutOpen(NULL, WAVE_MAPPER, NULL, 0, 0, CALLBACK_NULL) == MMSYSERR_NODRIVER &&
          waveOutClose(NULL) == MMSYSERR_NODRIVER && waveOutPrepareHeader(NULL, NULL, 0) == MMSYSERR_NODRIVER &&
          waveOutUnprepareHeader(NULL, NULL, 0) == MMSYSERR_NODRIVER && waveOutWrite(NULL, NULL, 0) == MMSYSERR_NODRIVER &&
          waveOutPause(NULL) == MMSYSERR_NODRIVER && waveOutRestart(NULL) == MMSYSERR_NODRIVER &&
          waveOutReset(NULL) == MMSYSERR_NODRIVER && waveOutGetPosition(NULL, &position, sizeof position) == MMSYSERR_NODRIVER &&
          waveOutGetDevCapsA(0, &wavea, sizeof wavea) == MMSYSERR_NODRIVER &&
          waveOutGetDevCapsW(0, &wavew, sizeof wavew) == MMSYSERR_NODRIVER);
    check(waveInGetNumDevs() == 0 && waveInOpen(NULL, WAVE_MAPPER, NULL, 0, 0, CALLBACK_NULL) == MMSYSERR_NODRIVER &&
          waveInClose(NULL) == MMSYSERR_NODRIVER && waveInPrepareHeader(NULL, NULL, 0) == MMSYSERR_NODRIVER &&
          waveInUnprepareHeader(NULL, NULL, 0) == MMSYSERR_NODRIVER && waveInAddBuffer(NULL, NULL, 0) == MMSYSERR_NODRIVER &&
          waveInStart(NULL) == MMSYSERR_NODRIVER && waveInStop(NULL) == MMSYSERR_NODRIVER &&
          waveInReset(NULL) == MMSYSERR_NODRIVER && waveInGetPosition(NULL, &position, sizeof position) == MMSYSERR_NODRIVER &&
          waveInGetDevCapsA(0, &wavein_a, sizeof wavein_a) == MMSYSERR_NODRIVER &&
          waveInGetDevCapsW(0, &wavein_w, sizeof wavein_w) == MMSYSERR_NODRIVER);
    check(waveOutSetVolume(NULL, 0) == MMSYSERR_NODRIVER && waveOutMessage(NULL, 0, 0, 0) == MMSYSERR_NODRIVER &&
          waveInMessage(NULL, 0, 0, 0) == MMSYSERR_NODRIVER &&
          waveOutGetErrorTextA(MMSYSERR_NODRIVER, error_text, sizeof error_text) == MMSYSERR_NODRIVER &&
          waveOutGetErrorTextW(MMSYSERR_NODRIVER, error_w, sizeof error_w / sizeof error_w[0]) == MMSYSERR_NODRIVER &&
          waveInGetErrorTextA(MMSYSERR_NODRIVER, error_text, sizeof error_text) == MMSYSERR_NODRIVER &&
          waveInGetErrorTextW(MMSYSERR_NODRIVER, error_w, sizeof error_w / sizeof error_w[0]) == MMSYSERR_NODRIVER &&
          !PlaySoundA(NULL, NULL, 0) && !PlaySoundW(NULL, NULL, 0));
    check(timeGetDevCaps(&caps, sizeof caps) == TIMERR_NOERROR && caps.wPeriodMin == 1 && caps.wPeriodMax == 1000000 &&
          timeGetSystemTime(&system_time, sizeof system_time) == TIMERR_NOERROR && system_time.wType == TIME_MS &&
          system_time.u.ms == timeGetTime());
    UINT timer = timeSetEvent(1, 1, NULL, 0, TIME_ONESHOT);
    check(timer == 0 && timeKillEvent(1) == TIMERR_NOCANDO);
    char out[128] = {0}, path[64] = "dir", a[] = "Dir/File.TXT", trim_a[] = "dir\\file";
    wchar_t wout[128] = {0}, wpath[64] = L"dir", wa[] = L"Dir\\File.TXT", trim_w[] = L"dir\\file", root_w[] = L"\\\\server\\share\\";
    check(PathCombineA(out, "dir", "file.txt") == out && strcmp(out, "dir\\file.txt") == 0 &&
          PathCombineW(wout, L"dir", L"file.txt") == wout && same_w(wout, L"dir\\file.txt"));
    check(PathAppendA(path, "file.txt") && strcmp(path, "dir\\file.txt") == 0 &&
          PathAppendW(wpath, L"file.txt") && same_w(wpath, L"dir\\file.txt"));
    check(PathFindFileNameA(a) == a + 4 && PathFindFileNameW(wa) == wa + 4 &&
          PathFindExtensionA(a) == a + 8 && PathFindExtensionW(wa) == wa + 8);
    check(PathIsRelativeA("dir\\file") && PathIsRelativeW(L"dir\\file") &&
          !PathIsRelativeA("C:\\file") && !PathIsRelativeW(L"C:\\file"));
    typedef int (WINAPI *str_cmp_a_fn)(LPCSTR, LPCSTR);
    FARPROC str_cmp_a_proc = GetProcAddress(GetModuleHandleA("shlwapi.dll"), "StrCmpIA");
    str_cmp_a_fn str_cmp_a = NULL;
    memcpy(&str_cmp_a, &str_cmp_a_proc, sizeof str_cmp_a);
    FARPROC str_cmp_w_proc = GetProcAddress(GetModuleHandleA("shlwapi.dll"), "StrCmpIW");
    int (WINAPI *str_cmp_w)(LPCWSTR, LPCWSTR) = NULL;
    memcpy(&str_cmp_w, &str_cmp_w_proc, sizeof str_cmp_w);
    check(str_cmp_a && str_cmp_a("AbC", "aBc") == 0 && str_cmp_w && str_cmp_w(L"AbC", L"aBc") == 0 &&
          StrStrIA("Hello World", "WORLD") != NULL && StrStrIW(L"Hello World", L"WORLD") != NULL);
    check(PathRemoveFileSpecA(trim_a) && strcmp(trim_a, "dir") == 0 &&
          PathRemoveFileSpecW(trim_w) && same_w(trim_w, L"dir") &&
          !PathRemoveFileSpecW(root_w) && same_w(root_w, L"\\\\server\\share\\"));
    check(!PathFileExistsA("x86-user32-no-such-file") && !PathFileExistsW(L"x86-user32-no-such-file"));
}

int main(void) {
    printf("x86-crt: start\n");
    /* CRT startup already ran: argc/argv/_environ exist and stdout works. */
    check(printf("%s", "") == 0);
    puts("x86-crt: stdio ok");
    test_resources();
    puts("x86-crt: resources ok");

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
    WCHAR user_locale[LOCALE_NAME_MAX_LENGTH] = {0};
    check(GetUserDefaultLocaleName(user_locale, LOCALE_NAME_MAX_LENGTH) && user_locale[0] != 0);
    puts("x86-crt: locale name ok");
    check(GetSystemMetrics(SM_CXSCREEN) == 1280 && GetSystemMetrics(SM_CYSCREEN) == 720);
    test_winmm_shlwapi();
    puts("x86-crt: user32/winmm/shlwapi ok");

    /* A 1.5 MiB frame exceeds the old fixed 1 MiB stack. Touching each page
     * exercises the PE reserve size and Windows-style guard-page growth. */
    volatile unsigned char stack_probe[1536 * 1024];
    for (unsigned i = 0; i < sizeof stack_probe; i += 4096) stack_probe[i] = (unsigned char)(i / 4096 + 1);
    check(stack_probe[0] == 1 && stack_probe[1024 * 1024] == 1 && stack_probe[1536 * 1024 - 4096] == 128);
    puts("x86-crt: stack probe ok");

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
    ULONG compatibility = 2, queried = 0, returned = 0;
    check(HeapSetInformation(h, HeapCompatibilityInformation, &compatibility, sizeof compatibility));
    check(HeapQueryInformation(h, HeapCompatibilityInformation, &queried, sizeof queried, &returned));
    check(queried == 2 && returned == sizeof queried);
    check(HeapSetInformation(h, HeapEnableTerminationOnCorruption, NULL, 0));
    SetLastError(0);
    check(!HeapSetInformation((HANDLE)0xDEAD0000, HeapCompatibilityInformation, &compatibility, sizeof compatibility) && GetLastError() == ERROR_INVALID_PARAMETER);
    check(!HeapSetInformation(h, HeapCompatibilityInformation, &compatibility, 2) && GetLastError() == ERROR_INVALID_PARAMETER);
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
    DWORD version_types = VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER;
    ULONGLONG condition_mask = VerSetConditionMask(0x8000000000000000ULL,
        version_types, VER_GREATER_EQUAL);
    check((condition_mask & 0x8000000000000000ULL) != 0);
    OSVERSIONINFOEXA check_a; memset(&check_a, 0, sizeof check_a);
    check_a.dwOSVersionInfoSize = sizeof check_a;
    check_a.dwMajorVersion = 10; check_a.dwMinorVersion = 0; check_a.dwBuildNumber = 19045;
    check(VerifyVersionInfoA(&check_a, version_types, condition_mask) != 0);
    check_a.dwMajorVersion = 11;
    SetLastError(0);
    check(VerifyVersionInfoA(&check_a, VER_MAJORVERSION,
        VerSetConditionMask(0, VER_MAJORVERSION, VER_EQUAL)) == 0 &&
        GetLastError() == ERROR_OLD_WIN_VERSION);
    OSVERSIONINFOEXW check_w; memset(&check_w, 0, sizeof check_w);
    check_w.dwOSVersionInfoSize = sizeof check_w;
    check_w.dwMajorVersion = 10;
    check(VerifyVersionInfoW(&check_w, VER_MAJORVERSION,
        VerSetConditionMask(0, VER_MAJORVERSION, VER_EQUAL)) != 0);
    check(MulDiv(3, 2, 4) == 2 && MulDiv(-3, 2, 4) == -2);
    check(MulDiv(1, 1, 0) == -1);
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
    check(IsDebuggerPresent() == FALSE);
    SetLastError(123);
    OutputDebugStringA("x86 diagnostic A");
    OutputDebugStringW(L"x86 diagnostic W");
    check(GetLastError() == 123);
    check(AreFileApisANSI() == TRUE);
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
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    FARPROC open_event = GetProcAddress(kernel, "OpenEventA");
    check(open_event != NULL);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)kernel;
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)((BYTE*)kernel + dos->e_lfanew);
    check(dos->e_magic == IMAGE_DOS_SIGNATURE && nt->Signature == IMAGE_NT_SIGNATURE &&
          nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 && nt->FileHeader.NumberOfSections == 0);
    IMAGE_DATA_DIRECTORY* export_dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    IMAGE_EXPORT_DIRECTORY* exports = (IMAGE_EXPORT_DIRECTORY*)((BYTE*)kernel + export_dir->VirtualAddress);
    DWORD* functions = (DWORD*)((BYTE*)kernel + exports->AddressOfFunctions);
    DWORD* name_rvas = (DWORD*)((BYTE*)kernel + exports->AddressOfNames);
    WORD* ordinals = (WORD*)((BYTE*)kernel + exports->AddressOfNameOrdinals);
    FARPROC walked = NULL;
    for (DWORD i = 0; i < exports->NumberOfNames; ++i)
        if (strcmp((char*)kernel + name_rvas[i], "OpenEventA") == 0)
            walked = (FARPROC)((BYTE*)kernel + functions[ordinals[i]]);
    check(walked == open_event);
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
    /* Timed yields and waits use real elapsed time. The QPC and tick deltas must
     * agree with the same Sleep interval, and a worker's Sleep must delay the
     * event wait rather than being treated as an immediate cooperative yield. */
    {
        LARGE_INTEGER frequency, before, after;
        check(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0);
        DWORD tick_before = GetTickCount();
        QueryPerformanceCounter(&before);
        Sleep(50);
        QueryPerformanceCounter(&after);
        DWORD tick_elapsed = GetTickCount() - tick_before;
        LONGLONG qpc_elapsed = (after.QuadPart - before.QuadPart) * 1000 / frequency.QuadPart;
        check(tick_elapsed >= 45 && tick_elapsed < 500 && qpc_elapsed >= 45 && qpc_elapsed < 500);

        g_flag = 0;
        g_delay_event = CreateEventA(NULL, TRUE, FALSE, NULL);
        HANDLE delayed = CreateThread(NULL, 0, delayed_event_body, NULL, 0, NULL);
        DWORD wait_start = GetTickCount();
        DWORD wait_result = WaitForSingleObject(g_delay_event, 200);
        DWORD wait_elapsed = GetTickCount() - wait_start;
        check(g_delay_event != NULL && delayed != NULL && wait_result == WAIT_OBJECT_0 &&
              g_flag == 1 && wait_elapsed >= 15 && wait_elapsed < 500);
        WaitForSingleObject(delayed, INFINITE);
        CloseHandle(delayed); CloseHandle(g_delay_event);

        HANDLE timeout_event = CreateEventA(NULL, FALSE, FALSE, NULL);
        DWORD multiple_start = GetTickCount();
        DWORD multiple_result = WaitForMultipleObjects(1, &timeout_event, FALSE, 30);
        DWORD multiple_elapsed = GetTickCount() - multiple_start;
        check(multiple_result == WAIT_TIMEOUT && multiple_elapsed >= 25 && multiple_elapsed < 500);
        CloseHandle(timeout_event);
        DWORD message_start = GetTickCount();
        DWORD message_result = MsgWaitForMultipleObjects(0, NULL, FALSE, 20, 0);
        DWORD message_elapsed = GetTickCount() - message_start;
        check(message_result == WAIT_TIMEOUT && message_elapsed >= 15 && message_elapsed < 500);
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
