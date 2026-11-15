// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef STEAM_PROVIDER_DLL
int SteamAPI_Init(void) { return 42; }
#else
typedef unsigned long DWORD;
typedef long LSTATUS;
typedef void *HKEY;
__declspec(dllimport) int __cdecl SteamAPI_Init(void);
__declspec(dllimport) LSTATUS __stdcall RegOpenKeyExA(HKEY, const char *, DWORD, DWORD, HKEY *);
__declspec(dllimport) LSTATUS __stdcall RegQueryValueExA(HKEY, const char *, DWORD *, DWORD *, unsigned char *, DWORD *);
__declspec(dllimport) LSTATUS __stdcall RegCloseKey(HKEY);
#define HKEY_CURRENT_USER ((HKEY)(unsigned long)0x80000001u)
int fixture_main(void) {
    HKEY key = 0;
    DWORD type = 0, size = sizeof(DWORD), account = 0;
    if (SteamAPI_Init() != 42) return 10;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam\\ActiveProcess", 0, 0, &key)) return 11;
    if (RegQueryValueExA(key, "ActiveUser", 0, &type, (unsigned char *)&account, &size) || type != 4 || account != 1234) return 12;
    char client[64] = {0}; size = sizeof(client); type = 0;
    if (RegQueryValueExA(key, "SteamClientDll", 0, &type, (unsigned char *)client, &size) || type != 1) return 13;
    if (client[0] != 'C' || client[1] != ':' || client[2] != '\\') return 14;
    if (RegCloseKey(key)) return 15;
    return 42;
}
#endif
