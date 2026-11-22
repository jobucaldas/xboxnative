// SPDX-License-Identifier: GPL-3.0-or-later
typedef unsigned long DWORD;
typedef long LSTATUS;
typedef void *HKEY;
__declspec(dllimport) DWORD __stdcall GetModuleFileNameA(void *, char *, DWORD);
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
// Mode comes from the last character of the command line:
//   's' (default) owned standard provider: steam_api.dll is the staged emulator;
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
    // A short buffer truncates and still terminates, as the exe name always did.
    char tiny[8];
    for (int i = 0; i < 8; ++i) tiny[i] = 'x';
    if (GetModuleFileNameA(api, tiny, 8) != 7 || tiny[7] != 0 || tiny[0] != 'C' || tiny[6] != 'g') return 20;  // "C:\stag"
    return 42;
}
#endif
