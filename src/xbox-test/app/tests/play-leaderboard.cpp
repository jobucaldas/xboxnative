// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that uses Steam leaderboards the way a game does,
// so Play's leaderboard bridge can be observed off-device. It statically imports
// steam_api64.dll (as a game built against the Steamworks SDK does), which Play
// serves from the configured provider (play-leaderboard-provider.dll, never
// gbe_fork's binary). It finds two boards, downloads the rows and uploads a
// score, and fails unless the provider read real players out of its cache file
// on the first Find. It also asks ISteamFriends for the persona names of those
// rows, as a rank screen does: the provider answers "Unknown User" for every
// user it does not know, and the launcher's persona tap must replace that with
// the name Steam gave, in memory.
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
STEAM_API const char* __cdecl SteamAPI_ISteamFriends_GetFriendPersonaName(void*, uint64_t);
STEAM_API const char* __cdecl SteamAPI_ISteamFriends_GetPersonaName(void*);
STEAM_API int __cdecl SteamAPI_ISteamFriends_RequestUserInformation(void*, uint64_t, int);
STEAM_API const char* __cdecl PlayLeaderboardTrace(void);
STEAM_API uint64_t __cdecl PlayLeaderboardCallBoard(uint64_t call);
STEAM_API int __cdecl PlayLeaderboardPersonaCalls(int which);

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
bool same(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
}

extern "C" {
int lb_anchor;
void* lb_relocation = &lb_anchor;

DWORD lb_entry() {
    // The board the game asks for by FindLeaderboard: the launcher seeds it.
    // Find answers a SteamAPICall_t; the board handle arrives in
    // LeaderboardFindResult_t, and that is what Download/Upload take.
    uint64_t call = SteamAPI_ISteamUserStats_FindLeaderboard(nullptr, "v2_kills");
    if (!call) return 12;
    uint64_t kills = PlayLeaderboardCallBoard(call);
    if (!kills) return 21;
    if (kills == call) return 22;   // the fixture must keep the two distinct
    // The first Find read the file the launcher wrote before answering.
    if (!contains(PlayLeaderboardTrace(), "v2_kills=3;")) return 13;
    int rows = int(SteamAPI_ISteamUserStats_DownloadLeaderboardEntries(nullptr, kills, 0 /*Global*/, 1, 100));
    if (rows != 3) return 14;
    Entry top{};
    if (!SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry(nullptr, kills, 0, &top, nullptr, 0)) return 15;
    // Descending: the highest score is rank 1, and it is another player's.
    if (top.rank != 1 || top.score != 1500 || top.user == 0) return 16;

    // An account the launcher was never told about keeps the provider's own
    // answer: the tap must not invent names.
    if (!contains(SteamAPI_ISteamFriends_GetFriendPersonaName(nullptr, 0x1100000000000099ull), "Unknown User")) return 25;

    // A board this launch never seeded: the provider reads nothing, so the game
    // sees the empty board it saw before the bridge existed.
    uint64_t emptyCall = SteamAPI_ISteamUserStats_FindOrCreateLeaderboard(nullptr, "v2_unseeded", 2, 1);
    if (!emptyCall) return 17;
    uint64_t empty = PlayLeaderboardCallBoard(emptyCall);
    if (!empty) return 23;
    if (contains(PlayLeaderboardTrace(), "v2_unseeded=3;")) return 18;
    if (SteamAPI_ISteamUserStats_DownloadLeaderboardEntries(nullptr, empty, 0, 1, 100) != 0) return 19;

    // A score goes to the board the game found; the launcher resolves that
    // board handle back to its name through the provider and forwards it to
    // Steam after the provider answered.
    int32_t details[] = {4, 2};
    SteamAPI_ISteamUserStats_UploadLeaderboardScore(nullptr, kills, 1 /*KeepBest*/, 2000, details, 2);
    if (!contains(PlayLeaderboardTrace(), "upload=1;")) return 20;

    // The rank screen reads the players' names. The provider answers "Unknown
    // User" for every SteamID it does not know (gbe_fork's LAN-only friend
    // list), so these must come from the launcher's Steam session instead.
    // Synthetic names: this fixture never carries a real account's persona.
    if (!same(SteamAPI_ISteamFriends_GetFriendPersonaName(nullptr, top.user), "Player Three")) return 24;
    if (!same(SteamAPI_ISteamFriends_GetPersonaName(nullptr), "Fixture Owner")) return 25;
    // The provider's own call still ran: the launcher replaced its answer, it
    // did not skip it.
    if (PlayLeaderboardPersonaCalls(0) < 1) return 26;
    // The API contract: false means "known, call the getters now"; true means
    // "in flight, wait for PersonaStateChange_t". Nothing in Play posts that
    // callback (the provider owns the callback plumbing), so the tap must never
    // answer true: a game that waited would never be woken. Both a name the
    // launcher holds and one it does not answer false; the getters are then
    // called, and the queued name arrives for the next frame.
    if (SteamAPI_ISteamFriends_RequestUserInformation(nullptr, top.user, 1) != 0) return 27;
    if (SteamAPI_ISteamFriends_RequestUserInformation(nullptr, 0x110000100000009ull, 1) != 0) return 28;
    if (PlayLeaderboardPersonaCalls(1) < 1) return 29;

    // This account's own score changed the board. The provider still answers
    // from the copy it read at the first Find, so the launcher's refreshed rows
    // are what the game must read on the next download.
    uint64_t againCall = SteamAPI_ISteamUserStats_FindLeaderboard(nullptr, "v2_kills");
    if (!againCall) return 30;
    uint64_t again = PlayLeaderboardCallBoard(againCall);
    if (again != kills) return 31;
    if (SteamAPI_ISteamUserStats_DownloadLeaderboardEntries(nullptr, again, 0, 1, 100) != 3) return 32;
    Entry top2{};
    if (!SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry(nullptr, again, 0, &top2, nullptr, 0)) return 33;
    if (top2.user != 0x110000100000001ull || top2.score != 2000 || top2.rank != 1) return 34;

    return 42;
}
}
