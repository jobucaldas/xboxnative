// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that loads the PINNED gbe_fork steam_api64.dll as a
// Steamworks game does, and reports what that provider read out of its
// leaderboard cache file. It exists because the Wine test in play-runtime.sh
// uses a stand-in provider written here; this one proves the real pinned binary
// reads the file the launcher writes (tests/steam-leaderboard-gbe.sh).
// Prints RESULT lines to stdout. Exit 0 = the provider answered; the shell test
// owns the assertions.
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>

#define S extern "C" __declspec(dllimport)
S bool   __cdecl SteamAPI_Init();
S void   __cdecl SteamAPI_Shutdown();
S int    __cdecl SteamAPI_GetHSteamPipe();
S void*  __cdecl SteamAPI_SteamUserStats_v013();
S uint64_t __cdecl SteamAPI_ISteamUserStats_FindLeaderboard(void*, const char*);
S uint64_t __cdecl SteamAPI_ISteamUserStats_DownloadLeaderboardEntries(void*, uint64_t, int, int, int);
S bool   __cdecl SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry(void*, uint64_t, int, void*, int32_t*, int);
S int    __cdecl SteamAPI_ISteamUserStats_GetLeaderboardEntryCount(void*, uint64_t);
S const char* __cdecl SteamAPI_ISteamUserStats_GetLeaderboardName(void*, uint64_t);
S void   __cdecl SteamAPI_ManualDispatch_Init();
S void   __cdecl SteamAPI_ManualDispatch_RunFrame(int);
S bool   __cdecl SteamAPI_ManualDispatch_GetNextCallback(int, void*);
S bool   __cdecl SteamAPI_ManualDispatch_GetAPICallResult(int, uint64_t, void*, int, int, bool*);
S void   __cdecl SteamAPI_ManualDispatch_FreeLastCallback(int);

namespace {
// SDK callback structs (isteamuserstats.h / isteamutils.h).
struct FindResult { uint64_t handle; uint8_t found; };
struct DownloadResult { uint64_t board; uint64_t entries; int count; };
struct Entry { uint64_t user; int32_t rank; int32_t score; int32_t details; uint64_t ugc; };
// Steam's CallbackMsg_t, in full: m_hSteamUser, m_iCallback, m_pubParam,
// m_cubParam. GetNextCallback writes the whole object, so every field must be
// here; a short struct would be overrun on each poll (24 bytes on Win64).
struct CallbackMsg { int32_t user; int32_t callback; uint8_t* param; int32_t size; };
enum : int { CallbackLeaderboardFindResult = 1104, CallbackLeaderboardScoresDownloaded = 1105,
             CallbackSteamAPICallCompleted = 703 };

int g_pipe = 0;
HANDLE g_out = INVALID_HANDLE_VALUE;

// No CRT in this fixture, so print with the Win32 formatter. It has no %llu:
// 64-bit ids are printed as two 32-bit halves.
void emit(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    DWORD written = 0;
    WriteFile(g_out, buf, (DWORD)n, &written, nullptr);
    WriteFile(g_out, "\r\n", 2, &written, nullptr);
}

// Drain the provider's callback queue until the given call result arrives.
bool await(uint64_t call, void* out, int size, int expected, int ms) {
    for (int waited = 0; waited < ms; waited += 20) {
        SteamAPI_ManualDispatch_RunFrame(g_pipe);
        CallbackMsg msg{};
        while (SteamAPI_ManualDispatch_GetNextCallback(g_pipe, &msg)) {
            if (msg.callback == CallbackSteamAPICallCompleted) {
                bool failed = false;
                if (SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, call, out, size, expected, &failed)) {
                    SteamAPI_ManualDispatch_FreeLastCallback(g_pipe);
                    return !failed;
                }
            }
            SteamAPI_ManualDispatch_FreeLastCallback(g_pipe);
        }
        Sleep(20);
    }
    return false;
}
} // namespace

extern "C" int guest_entry() {
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!SteamAPI_Init()) { emit("RESULT init=0"); return 10; }
    g_pipe = SteamAPI_GetHSteamPipe();
    SteamAPI_ManualDispatch_Init();
    void* stats = SteamAPI_SteamUserStats_v013();
    if (!stats) { emit("RESULT stats=0"); return 11; }

    uint64_t call = SteamAPI_ISteamUserStats_FindLeaderboard(stats, "v2_kills");
    FindResult fr{};
    if (!await(call, &fr, sizeof fr, CallbackLeaderboardFindResult, 3000)) { emit("RESULT find=timeout"); return 12; }
    emit("RESULT find found=%d name=%s", (int)fr.found, SteamAPI_ISteamUserStats_GetLeaderboardName(stats, fr.handle));
    if (!fr.found || !fr.handle) return 13;

    emit("RESULT entry_count=%d", SteamAPI_ISteamUserStats_GetLeaderboardEntryCount(stats, fr.handle));
    uint64_t dcall = SteamAPI_ISteamUserStats_DownloadLeaderboardEntries(stats, fr.handle, 0, 1, 100);
    DownloadResult dr{};
    if (!await(dcall, &dr, sizeof dr, CallbackLeaderboardScoresDownloaded, 3000)) { emit("RESULT download=timeout"); return 14; }
    emit("RESULT download_count=%d", dr.count);
    for (int i = 0; i < dr.count; ++i) {
        Entry e{};
        if (!SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry(stats, dr.entries, i, &e, nullptr, 0)) {
            emit("RESULT entry_%d=missing", i);
            continue;
        }
        emit("RESULT entry_%d rank=%d score=%d user_set=%d", i, e.rank, e.score, e.user != 0);
    }
    SteamAPI_Shutdown();
    return 0;
}

extern "C" void guest_start() { ExitProcess((UINT)guest_entry()); }
