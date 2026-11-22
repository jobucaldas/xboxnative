// SPDX-License-Identifier: GPL-3.0-or-later
typedef unsigned long DWORD;
typedef long LSTATUS;
typedef void *HKEY;
__declspec(dllimport) DWORD __stdcall GetModuleFileNameA(void *, char *, DWORD);
__declspec(dllimport) DWORD __stdcall GetLastError(void);
__declspec(dllimport) void __stdcall SetLastError(DWORD);
#ifdef STEAM_PROVIDER_DLL
__declspec(dllimport) int __stdcall GetModuleHandleExA(DWORD, const char *, void **);
int SteamAPI_Init(void) { return 42; }
// What gbe does to find steam_settings: its own module path, from an address inside it.
DWORD SteamAPI_SelfPath(char *buf, DWORD size) {
    void *self = 0;
    if (!GetModuleHandleExA(4 | 2, (const char *)&SteamAPI_Init, &self)) return 0;
    return GetModuleFileNameA(self, buf, size);
}
#else
typedef unsigned short WCHAR;
__declspec(dllimport) int __cdecl SteamAPI_Init(void);
__declspec(dllimport) DWORD __cdecl SteamAPI_SelfPath(char *, DWORD);
__declspec(dllimport) LSTATUS __stdcall RegOpenKeyExA(HKEY, const char *, DWORD, DWORD, HKEY *);
__declspec(dllimport) LSTATUS __stdcall RegQueryValueExA(HKEY, const char *, DWORD *, DWORD *, unsigned char *, DWORD *);
__declspec(dllimport) LSTATUS __stdcall RegCloseKey(HKEY);
__declspec(dllimport) DWORD __stdcall GetModuleFileNameW(void *, WCHAR *, DWORD);
__declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
__declspec(dllimport) int __stdcall GetModuleHandleExA(DWORD, const char *, void **);
__declspec(dllimport) int __stdcall GetModuleHandleExW(DWORD, const WCHAR *, void **);
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *);
__declspec(dllimport) const char *__stdcall GetCommandLineA(void);
#define HKEY_CURRENT_USER ((HKEY)(unsigned long)0x80000001u)
#define STAGED_API "C:\\staged\\steam\\480\\steam_api.dll"
#define STAGED_CLIENT "C:\\staged\\steam\\480\\steamclient.dll"
static int same(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int same_wide(const WCHAR *a, const char *b) {
    while (*a && *a == (unsigned char)*b) { ++a; ++b; }
    return *a == (unsigned char)*b;
}
static int open_active_process(HKEY *key) {
    return RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam\\ActiveProcess", 0, 0, key);
}
static DWORD len(const char *s) {
    DWORD n = 0;
    while (s[n]) ++n;
    return n;
}
static int module_path_a(void *module, const char *want) {
    char path[128] = {0};
    DWORD n = GetModuleFileNameA(module, path, sizeof(path));
    return same(path, want) && n == len(want);
}
static int module_path_w(void *module, const char *want) {
    WCHAR path[128] = {0};
    DWORD n = GetModuleFileNameW(module, path, 128);
    return same_wide(path, want) && n == len(want);
}
// GetModuleFileName contract: a fitting buffer returns the length with last error
// 0; a short one is NUL-terminated at n-1, returns n and sets 122; n == 0 returns 0
// and sets 122. `want` includes its terminating NUL.
static int fit_a(void *module, const char *want) {
    char b[128];
    DWORD L = len(want), i;
    SetLastError(5);
    if (GetModuleFileNameA(module, b, L + 1) != L || GetLastError() != 0 || !same(b, want)) return 1;
    for (i = 0; i < 128; ++i) b[i] = 'x';
    SetLastError(0);
    if (GetModuleFileNameA(module, b, L) != L || GetLastError() != 122 || b[L - 1] != 0 || b[L] != 'x') return 2;
    for (i = 0; i + 1 < L; ++i) if (b[i] != want[i]) return 3;
    SetLastError(0);
    if (GetModuleFileNameA(module, b, 0) != 0 || GetLastError() != 122) return 4;
    return 0;
}
static int fit_w(void *module, const WCHAR *want, DWORD L, DWORD small) {
    WCHAR b[128];
    DWORD i;
    SetLastError(5);
    if (GetModuleFileNameW(module, b, L + 1) != L || GetLastError() != 0) return 1;
    for (i = 0; i <= L; ++i) if (b[i] != want[i]) return 2;
    for (i = 0; i < 128; ++i) b[i] = 0x7777;
    SetLastError(0);
    if (GetModuleFileNameW(module, b, L) != L || GetLastError() != 122 || b[L - 1] != 0 || b[L] != 0x7777) return 3;
    for (i = 0; i + 1 < L; ++i) if (b[i] != want[i]) return 4;
    SetLastError(0);
    if (GetModuleFileNameW(module, b, 0) != 0 || GetLastError() != 122) return 5;
    for (i = 0; i < 128; ++i) b[i] = 0x7777;
    SetLastError(0);
    if (GetModuleFileNameW(module, b, small) != small || GetLastError() != 122 || b[small - 1] != 0 || b[small] != 0x7777) return 6;
    for (i = 0; i + 1 < small; ++i) if (b[i] != want[i]) return 7;
    return 0;
}
static DWORD image_span(void *base) {  // page-rounded SizeOfImage of a mapped PE32
    const unsigned char *p = (const unsigned char *)base;
    DWORD nt = *(const DWORD *)(p + 0x3c), size = *(const DWORD *)(p + nt + 24 + 56);
    return (size + 0xFFF) & ~(DWORD)0xFFF;
}
// FROM_ADDRESS only matches [base, base + image size): inside answers the module,
// the first byte past it, or a stack address, fail with 126 and a NULL handle.
static int from_address(void *module, int *probe) {
    void *h = (void *)1;
    const char *inside = (const char *)module + 0x10, *past = (const char *)module + image_span(module);
    if (!GetModuleHandleExA(4 | 2, inside, &h) || h != module) return 1;
    h = (void *)1;
    if (!GetModuleHandleExW(4 | 2, (const WCHAR *)inside, &h) || h != module) return 2;
    h = (void *)1; SetLastError(0);
    if (GetModuleHandleExA(4 | 2, past, &h) || h || GetLastError() != 126) return 3;
    h = (void *)1; SetLastError(0);
    if (GetModuleHandleExW(4 | 2, (const WCHAR *)past, &h) || h || GetLastError() != 126) return 4;
    h = (void *)1; SetLastError(0);
    if (GetModuleHandleExA(4 | 2, (const char *)probe, &h) || h || GetLastError() != 126) return 5;
    return 0;
}
#define STAGED_UNICODE "C:\\u\303\251\346\227\245\346\234\254\350\252\236\360\237\230\200\\steam_api.dll"
// The same path as UTF-16: e-acute, three BMP CJK units, then U+1F600 as a surrogate pair.
static const WCHAR STAGED_UNICODE_W[] = {'C', ':', '\\', 'u', 0xE9, 0x65E5, 0x672C, 0x8A9E, 0xD83D, 0xDE00, '\\',
    's', 't', 'e', 'a', 'm', '_', 'a', 'p', 'i', '.', 'd', 'l', 'l', 0};
// Mode comes from the last character of the command line:
//   's' (default) owned standard provider: steam_api.dll is the staged emulator;
//   'u' like 's' but the staged path is non-ASCII UTF-8 (GetModuleFileNameA/W decoding);
//   'n' nothing staged (no ownership): no staged path and no ActiveProcess key;
//   'w' SteamStub: the game keeps its steam_api.dll and loads the client the
//       ActiveProcess registry names, which must be the staged steamclient.dll.
int fixture_main(void) {
    const char *cmd = GetCommandLineA();
    char mode = 's';
    while (*cmd) { mode = *cmd; ++cmd; }
    HKEY key = 0;
    DWORD type = 0, size = sizeof(DWORD), account = 0;
    char path[128] = {0};
    if (SteamAPI_Init() != 42) return 10;
    // The main module keeps answering with the executable's own name.
    if (!module_path_a(0, "fixture.exe")) return 16;
    if (!module_path_w(0, "fixture.exe")) return 17;
    if (mode == 'u') {
        void *api = GetModuleHandleA("steam_api.dll");
        DWORD n = sizeof(STAGED_UNICODE_W) / sizeof(WCHAR) - 1;
        if (fit_a(api, STAGED_UNICODE)) return 50;
        if (fit_w(api, STAGED_UNICODE_W, n, 5)) return 51;
        // Cut between the surrogates: 9 units + NUL leaves the high surrogate last.
        WCHAR cut[16];
        SetLastError(0);
        if (GetModuleFileNameW(api, cut, 10) != 10 || GetLastError() != 122 || cut[8] != 0xD83D || cut[9] != 0) return 52;
        return 45;
    }
    if (mode == 'n') {
        if (same((GetModuleFileNameA(GetModuleHandleA("steam_api.dll"), path, sizeof(path)), path), STAGED_API)) return 22;
        if (!open_active_process(&key)) return 23;
        return 43;
    }
    if (open_active_process(&key)) return 11;
    if (RegQueryValueExA(key, "ActiveUser", 0, &type, (unsigned char *)&account, &size) || type != 4 || account != 1234) return 12;
    char client[64] = {0}; size = sizeof(client); type = 0;
    if (RegQueryValueExA(key, "SteamClientDll", 0, &type, (unsigned char *)client, &size) || type != 1) return 13;
    if (client[0] != 'C' || client[1] != ':' || client[2] != '\\') return 14;
    if (RegCloseKey(key)) return 15;
    if (mode == 'w') {
        // SteamStub's locator: load the registered client, then ask for its path.
        void *loaded = LoadLibraryA(client);
        if (!loaded) return 24;
        if (!module_path_a(loaded, STAGED_CLIENT) || !same(client, STAGED_CLIENT)) return 25;
        if (!module_path_w(loaded, STAGED_CLIENT)) return 27;
        // The game's own steam_api.dll is not the staged emulator.
        GetModuleFileNameA(GetModuleHandleA("steam_api.dll"), path, sizeof(path));
        if (same(path, STAGED_API) || same(path, STAGED_CLIENT)) return 26;
        return 44;
    }
    void *api = GetModuleHandleA("steam_api.dll");
    if (!module_path_a(api, STAGED_API)) return 18;
    if (!module_path_w(api, STAGED_API)) return 19;
    char self[128] = {0};
    if (SteamAPI_SelfPath(self, sizeof(self)) != sizeof(STAGED_API) - 1 || !same(self, STAGED_API)) return 21;
    // A short buffer truncates and still terminates; Windows returns nSize and sets 122.
    char tiny[8];
    for (int i = 0; i < 8; ++i) tiny[i] = 'x';
    SetLastError(0);
    if (GetModuleFileNameA(api, tiny, 8) != 8 || GetLastError() != 122 || tiny[7] != 0 || tiny[0] != 'C' || tiny[6] != 'g') return 20;  // "C:\stag"
    if (fit_a(api, STAGED_API)) return 30;
    WCHAR want[64];
    DWORD n = len(STAGED_API), i;
    for (i = 0; i <= n; ++i) want[i] = (unsigned char)STAGED_API[i];
    if (fit_w(api, want, n, 8)) return 31;
    if (fit_a(0, "fixture.exe")) return 32;
    int probe = 0;
    if (from_address(api, &probe)) return 33;
    if (from_address(GetModuleHandleA(0), &probe)) return 34;
    return 42;
}
#endif
