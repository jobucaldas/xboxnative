/* SPDX-License-Identifier: GPL-3.0-or-later
 * Owned i686 (PE32) fixture for the implicit TLS path: a .tls section with raw
 * data and zero-fill, a TLS callback, and reads of the TLS block through
 * TEB32.ThreadLocalStoragePointer (fs:[0x2C]) and the index variable, exactly
 * the sequence the Windows loader sets up for IMAGE_DIRECTORY_ENTRY_TLS.
 * Freestanding: no CRT. The .tls section and the IMAGE_TLS_DIRECTORY32 are
 * declared here; the test script patches data-directory 9 to point at the
 * directory (binutils ld emits the section but leaves the directory empty).
 * The same binary runs under real Windows/Wine, whose loader does the whole
 * setup itself: identical stdout and exit code. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
IMP void* WINAPI GetStdHandle(u32);
IMP int WINAPI WriteFile(void*, const void*, u32, u32*, void*);
IMP u32 __cdecl strlen(const char*);
IMP void WINAPI ExitProcess(u32);

extern char __ImageBase;

static int g_fail, g_checks;
static void check(int ok) { ++g_checks; if (!ok) ++g_fail; }

static void out(const char* s) {
    u32 n = 0; WriteFile(GetStdHandle((u32)-11), s, strlen(s), &n, 0);
    check(n == strlen(s));
}
static void out_num(const char* label, u32 v) {
    char b[48]; char* p = b; const char* l = label; while (*l) *p++ = *l++;
    *p++ = '='; char t[12]; int k = 0; if (!v) t[k++] = '0'; while (v) { t[k++] = (char)('0' + v % 10); v /= 10; }
    while (k) *p++ = t[--k];
    *p++ = '\n'; *p = 0; out(b);
}

/* The implicit TLS variables. Placed in .tls by hand: this mingw gcc lowers
 * __thread to emutls (a different mechanism), and the point of the fixture is
 * the PE TLS directory the real loader honours. Because these are ordinary
 * variables in a section, the compiler reaches them by absolute address (the
 * image template), never fs-relative — so the per-thread block is verified
 * only through the fs:[0x2C] chain below, and the template checks assert the
 * loader copied rather than moved the data. */
__attribute__((section(".tls"), used)) static u32 tls_init = 7;   /* raw data */
__attribute__((section(".tls"), used)) static u32 tls_zeroed;     /* raw zeros */
/* The callback records that it ran, with the reason it was given. */
static u32 cb_calls, cb_reason;
int WINAPI tls_callback(void* handle, u32 reason, void* reserved) {
    (void)handle; (void)reserved;
    ++cb_calls; cb_reason = reason;
    return 1;
}
/* binutils ld wraps a .tls section in ___tls_start__/___tls_end__ (C level:
 * two leading underscores); the loader copies [Start, End) and appends
 * SizeOfZeroFill zero bytes. */
extern char __tls_start__, __tls_end__;

/* The callback array must precede the directory it is named in. */
__attribute__((used)) static u32 tls_callbacks[2] = { (u32)tls_callback, 0 };
/* IMAGE_TLS_DIRECTORY32, assembled by hand and referenced by data directory 9
 * (patched by the test script to this symbol). */
typedef struct {
    u32 start_raw, end_raw, index_ptr, callbacks, zero_fill, characteristics;
} TlsDir;
static u32 tls_index;                      /* AddressOfIndex target: slot 0 */
__attribute__((used)) static TlsDir tls_dir = {
    (u32)(u32*)&__tls_start__, (u32)(u32*)&__tls_end__,
    (u32)(u32*)&tls_index, (u32)(u32*)&tls_callbacks, 4, 0
};

static u32 fs_dword(u32 off) { u32 v; __asm__ volatile("movl %%fs:(%1), %0" : "=r"(v) : "r"(off)); return v; }

void WINAPI fixture_tls_start(void) {
    out("x86-tls: start\n");
    /* The loader set up ThreadLocalStoragePointer. */
    u32 tlsp = fs_dword(0x2C); check(tlsp != 0);
    u32 slot0 = *(u32*)tlsp; check(slot0 != 0);
    /* The index variable the directory points at says which slot is ours. */
    check(tls_index == 0);
    /* The block: starts at __tls_start__, so a variable's offset in it is
     * (its address - __tls_start__). Raw data copied, zero-fill zeroed. */
    u32 off_init = (u32)&tls_init - (u32)&__tls_start__;
    u32 off_zeroed = (u32)&tls_zeroed - (u32)&__tls_start__;
    check(*(u32*)(slot0 + off_init) == 7);    /* tls_init's raw value */
    check(*(u32*)(slot0 + off_zeroed) == 0);  /* tls_zeroed: SizeOfZeroFill */
    *(u32*)(slot0 + off_zeroed) = 99;         /* the block is writable, per thread */
    check(*(u32*)(slot0 + off_zeroed) == 99);
    /* And the block is a copy, not the image template: writing the block left
     * the template's variable alone (the loader must not alias them). */
    check(tls_zeroed == 0);
    out("x86-tls: block ok\n");
    /* The callback ran once with DLL_PROCESS_ATTACH before the entry point. */
    check(cb_calls == 1 && cb_reason == 1);
    out("x86-tls: callback ok\n");
    out_num("checks", (u32)g_checks); out_num("failed", (u32)g_fail);
    ExitProcess(g_fail ? 100u + (u32)g_fail : 42u);
}
