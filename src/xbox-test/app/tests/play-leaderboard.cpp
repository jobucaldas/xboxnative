// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that uses Steam leaderboards the way a game does,
// so Play's leaderboard bridge can be observed off-device. It statically imports
// steam_api64.dll (as a game built against the Steamworks SDK does), which Play
// serves from the configured provider (play-leaderboard-provider.dll, never
// gbe_fork's binary). It finds two boards, downloads the rows and uploads a
// score, and fails unless the provider read real players out of its cache file
// on the first Find.
// Exit code 42 = every check passed; play-runtime.sh also checks the log lines
// Play must emit.
#include <windows.h>
#include <stdint.h>

#define STEAM_API extern "C" __declspec(dllimport)
STEAM_API uint64_t __cdecl SteamAPI_ISteamUserStats_FindLeaderboard(void*, const char*);
STEAM_API uint64_t __cdecl SteamAPI_ISteamUserStats_FindOrCreateLeaderboard(void*, const char*, int, int);
STEAM_API uint64_t __cdecl SteamAPI_ISteamUserStats_DownloadLeaderboardEntries(void*, uint64_t, int, int, int);
STEAM_API int __cdecl SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry(void*, uint64_t, int, void*, int32_t*, int);
STEAM_API uint64_t __cdecl SteamAPI_ISteamUserStats_UploadLeaderboardScore(void*, uint64_t, int, int32_t, const int32_t*, int);
STEAM_API const char* __cdecl PlayLeaderboardTrace(void);

namespace {
struct Entry { uint64_t user; int32_t rank, score, details; uint64_t ugc; };

// kernel32 only, like the other Play fixtures: no C runtime to link against.
bool contains(const char* haystack, const char* needle) {
    for (; *haystack; ++haystack) {
        const char* a = haystack;
        const char* b = needle;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (!*b) return true;
    }
    return false;
}
}

extern "C" {
int lb_anchor;
void* lb_relocation = &lb_anchor;

DWORD lb_entry() {
    // The board the game asks for by FindLeaderboard: the launcher seeds it.
    uint64_t kills = SteamAPI_ISteamUserStats_FindLeaderboard(nullptr, "v2_kills");
    if (!kills) return 12;
    // The first Find read the file the launcher wrote before answering.
    if (!contains(PlayLeaderboardTrace(), "v2_kills=3;")) return 13;
    int rows = int(SteamAPI_ISteamUserStats_DownloadLeaderboardEntries(nullptr, kills, 0 /*Global*/, 1, 100));
    if (rows != 3) return 14;
    Entry top{};
    if (!SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry(nullptr, kills, 0, &top, nullptr, 0)) return 15;
    // Descending: the highest score is rank 1, and it is another player's.
    if (top.rank != 1 || top.score != 1500 || top.user == 0) return 16;

    // A board this launch never seeded: the provider reads nothing, so the game
    // sees the empty board it saw before the bridge existed.
    uint64_t empty = SteamAPI_ISteamUserStats_FindOrCreateLeaderboard(nullptr, "v2_unseeded", 2, 1);
    if (!empty) return 17;
    if (contains(PlayLeaderboardTrace(), "v2_unseeded=3;")) return 18;
    if (SteamAPI_ISteamUserStats_DownloadLeaderboardEntries(nullptr, empty, 0, 1, 100) != 0) return 19;

    // A score goes to the board the game found; the launcher forwards it to
    // Steam after the provider answered.
    int32_t details[] = {4, 2};
    SteamAPI_ISteamUserStats_UploadLeaderboardScore(nullptr, kills, 1 /*KeepBest*/, 2000, details, 2);
    if (!contains(PlayLeaderboardTrace(), "upload=1;")) return 20;

    return 42;
}
}
