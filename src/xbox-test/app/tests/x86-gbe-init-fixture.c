// SPDX-License-Identifier: GPL-3.0-or-later
// Tiny freestanding Steamworks client used only by x86-gbe-init.sh.
typedef unsigned int u32;
typedef unsigned long long u64;
typedef int BOOL;

__declspec(dllimport) BOOL __cdecl SteamAPI_Init(void);
__declspec(dllimport) void __cdecl SteamAPI_RunCallbacks(void);
__declspec(dllimport) void *__cdecl SteamAPI_SteamUser_v023(void);
__declspec(dllimport) void *__cdecl SteamAPI_SteamApps_v009(void);
__declspec(dllimport) u64 __cdecl SteamAPI_ISteamUser_GetSteamID(void *user);
__declspec(dllimport) BOOL __cdecl SteamAPI_ISteamApps_BIsSubscribedApp(void *apps, u32 appid);
__declspec(dllimport) void __stdcall OutputDebugStringA(const char *text);
__declspec(dllimport) void __stdcall ExitProcess(u32 code);

static int guest_main(void) {
    if (!SteamAPI_Init()) {
        OutputDebugStringA("GBE SteamAPI_Init=0\n");
        return 10;
    }
    OutputDebugStringA("GBE SteamAPI_Init=1\n");
    SteamAPI_RunCallbacks();

    void *user = SteamAPI_SteamUser_v023();
    u64 steam_id = user ? SteamAPI_ISteamUser_GetSteamID(user) : 0;
    OutputDebugStringA(steam_id ? "GBE ISteamUser::GetSteamID=nonzero\n" :
                                 "GBE ISteamUser::GetSteamID=zero\n");

    void *apps = SteamAPI_SteamApps_v009();
    BOOL subscribed = apps ? SteamAPI_ISteamApps_BIsSubscribedApp(apps, 480) : 0;
    OutputDebugStringA(subscribed ? "GBE ISteamApps::BIsSubscribedApp=1\n" :
                                   "GBE ISteamApps::BIsSubscribedApp=0\n");
    return steam_id && subscribed ? 42 : 11;
}

void fixture_start(void) { ExitProcess((u32)guest_main()); }
