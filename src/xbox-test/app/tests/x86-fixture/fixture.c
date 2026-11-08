/* SPDX-License-Identifier: GPL-3.0-or-later
 * Owned i686 (PE32) fixture for the x86 execution core. Freestanding: no CRT
 * startup, so the only OS surface is the imports below. It also runs unchanged
 * under real Windows/Wine (the cross-check), where every import is the genuine
 * one; the emulator must produce the same stdout and the same exit code (42). */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
IMP void* WINAPI GetStdHandle(u32);
IMP int WINAPI WriteFile(void*, const void*, u32, u32*, void*);
IMP void WINAPI SetLastError(u32);
IMP u32 WINAPI GetLastError(void);
IMP void WINAPI ExitProcess(u32);
IMP u32 __cdecl strlen(const char*);
IMP void __cdecl qsort(void*, u32, u32, int(__cdecl*)(const void*, const void*));

extern char __ImageBase;

static int g_fail, g_checks;
static void check(int ok) { ++g_checks; if (!ok) ++g_fail; }
static const char* g_words[] = {"zeta", "alpha", "mid", "beta"};  /* absolute addresses: needs relocations */
static int g_data = 5;
static int g_bss[64];

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
static u32 crc32(const char* s, u32 n) {
    u32 c = 0xffffffffu;
    for (u32 i = 0; i < n; ++i) { c ^= (u8)s[i]; for (int b = 0; b < 8; ++b) c = (c >> 1) ^ (0xEDB88320u & (u32)-(int)(c & 1)); }
    return ~c;
}
static int cmp_int(const void* a, const void* b) { int x = *(const int*)a, y = *(const int*)b; return x < y ? -1 : x > y; }
static int cmp_word(const void* a, const void* b) {
    const char* x = *(const char* const*)a; const char* y = *(const char* const*)b;
    while (*x && *x == *y) { ++x; ++y; } return (u8)*x - (u8)*y;
}
static u32 fs_dword(u32 off) { u32 v; __asm__ volatile("movl %%fs:(%1), %0" : "=r"(v) : "r"(off)); return v; }

void WINAPI fixture_start(void) {
    out("x86-fixture: start\n");
    /* TEB32/PEB32 through the FS segment. */
    u32 self = fs_dword(0x18); check(self != 0 && *(u32*)(self + 0x18) == self);
    u32 peb = fs_dword(0x30); check(peb != 0 && *(u32*)(peb + 8) == (u32)&__ImageBase);
    out("x86-fixture: teb/peb ok\n");
    /* Relocated absolute pointers and .data/.bss. */
    check(cmp_word(&g_words[1], &g_words[1]) == 0 && g_words[0][0] == 'z' && g_data == 5);
    for (int i = 0; i < 64; ++i) g_bss[i] = (i * 37 + 11) % 101;
    qsort(g_bss, 64, 4, cmp_int);                       /* cdecl callback into guest code */
    int sorted = 1; for (int i = 1; i < 64; ++i) sorted &= g_bss[i - 1] <= g_bss[i];
    check(sorted);
    qsort((void*)g_words, 4, 4, cmp_word);
    check(g_words[0][0] == 'a' && g_words[3][0] == 'z');
    /* Integer ALU: shifts, multiply/divide, signed compare. */
    u32 x = 0x12345678u; check(((x << 4) | (x >> 28)) == 0x23456781u);
    int neg = -1234567; check(neg / 1000 == -1234 && neg % 1000 == -567 && (neg >> 3) == -154321);
    check((u32)(0xdeadbeefu * 31u) == 0xf70a1ef1u);
    /* crc32 of the greeting is a stable value everywhere. */
    const char msg[] = "hello from 32-bit"; check(crc32(msg, sizeof msg - 1) == 0x6154c7f2u);
    out_num("crc", crc32(msg, sizeof msg - 1));
    /* Last-error is both an API and TEB32+0x34. */
    SetLastError(1234); check(GetLastError() == 1234 && fs_dword(0x34) == 1234);
    out(msg); out("\n");
    out_num("checks", (u32)g_checks); out_num("failed", (u32)g_fail);
    ExitProcess(g_fail ? 100u + (u32)g_fail : 42u);
}
