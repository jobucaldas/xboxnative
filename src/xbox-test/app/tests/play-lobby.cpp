// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that uses ISteamMatchmaking and pumps callbacks
// with SteamAPI_ManualDispatch_* the way a game does, so Play's lobby callback
// splice (steam_matchmaking.h) can be observed off-device. It statically imports
// steam_api64.dll, which Play serves from the configured provider
// (play-lobby-provider.dll, never gbe_fork's binary).
//
// The provider's own call handles have the top bit set here, as half of the pinned
// gbe_fork's random handles do, so the launcher must route by which handles it
// issued. Before it calls ManualDispatch_Init the game's calls must reach the
// provider (a launcher handle there would never be read). After Init, with the launcher's
// lobby tap installed, CreateLobby/RequestLobbyList/JoinLobby answer launcher
// handles whose results come from the fake CM behind the tap, while the
// provider's own queued callbacks still arrive, in order, exactly once.
// With PLAY_LOBBY_EXPECT=passthrough (no Play, no tap: the provider run by the
// ordinary loader) every call must stay with the provider throughout.
// Exit code 42 = every check passed; play-runtime.sh also checks the log lines
// Play must emit and what the fake CM was asked.
#include <windows.h>
#include <stdint.h>

#define STEAM_API extern "C" __declspec(dllimport)
STEAM_API int32_t __cdecl SteamAPI_GetHSteamPipe(void);
STEAM_API void __cdecl SteamAPI_ManualDispatch_Init(void);
STEAM_API void __cdecl SteamAPI_ManualDispatch_RunFrame(int32_t);
STEAM_API bool __cdecl SteamAPI_ManualDispatch_GetNextCallback(int32_t, void*);
STEAM_API void __cdecl SteamAPI_ManualDispatch_FreeLastCallback(int32_t);
STEAM_API bool __cdecl SteamAPI_ManualDispatch_GetAPICallResult(int32_t, uint64_t, void*, int, int, bool*);
STEAM_API uint64_t __cdecl SteamAPI_ISteamMatchmaking_CreateLobby(void*, int, int);
STEAM_API uint64_t __cdecl SteamAPI_ISteamMatchmaking_RequestLobbyList(void*);
STEAM_API uint64_t __cdecl SteamAPI_ISteamMatchmaking_GetLobbyByIndex(void*, int);
STEAM_API uint64_t __cdecl SteamAPI_ISteamMatchmaking_JoinLobby(void*, uint64_t);
STEAM_API void __cdecl SteamAPI_ISteamMatchmaking_LeaveLobby(void*, uint64_t);
STEAM_API int __cdecl PlayLobbyCalls(int which);
STEAM_API uint64_t __cdecl PlayLobbyProviderLobby(int byIndex);

namespace {
// SDK callback structs, Win64 packing 8. Declared here, not included, so the
// fixture checks the launcher's layouts against its own reading of the SDK.
#pragma pack(push, 8)
struct CallbackMsg { int32_t user; int32_t callback; uint8_t* param; int32_t size; };
struct CallCompleted { uint64_t call; int32_t callback; uint32_t size; };
struct LobbyCreated { int32_t result; uint64_t lobby; };
struct LobbyEnter { uint64_t lobby; uint32_t permissions; bool locked; uint32_t response; };
struct LobbyMatchList { uint32_t count; };
#pragma pack(pop)

enum { Create, List, ByIndex, Join, Leave, Init, Pipe, FreeIdle, UnknownResult };
enum : int { CbLobbyEnter = 504, CbMatchList = 510, CbLobbyCreated = 513, CbCompleted = 703 };

// The fake CM's lobbies (play-runner.cpp's `lobbies` mode). Fixture ids.
const uint64_t CmCreated = 0x1100001000777ull;
const uint64_t CmListA = 0x1100001000010ull;
const uint64_t CmListB = 0x1100001000020ull;
const uint64_t TopBit = 1ull << 63;

int32_t g_pipe;
uint64_t g_completed[32];
int g_completedCount;
uint32_t g_enterCount;
uint64_t g_enterLobby;
uint32_t g_enterResponse;

bool completed(uint64_t call) {
    for (int i = 0; i < g_completedCount; ++i) if (g_completed[i] == call) return true;
    return false;
}
// The game's pump: RunFrame, then GetNextCallback / read / FreeLastCallback until empty.
void pump() {
    SteamAPI_ManualDispatch_RunFrame(g_pipe);
    CallbackMsg msg;
    msg.user = 0; msg.callback = 0; msg.param = nullptr; msg.size = 0;
    while (SteamAPI_ManualDispatch_GetNextCallback(g_pipe, &msg)) {
        if (msg.callback == CbCompleted && msg.size == (int32_t)sizeof(CallCompleted) && g_completedCount < 32) {
            const CallCompleted* done = (const CallCompleted*)msg.param;
            g_completed[g_completedCount++] = done->call;
        } else if (msg.callback == CbLobbyEnter && msg.size == (int32_t)sizeof(LobbyEnter)) {
            const LobbyEnter* enter = (const LobbyEnter*)msg.param;
            ++g_enterCount; g_enterLobby = enter->lobby; g_enterResponse = enter->response;
        }
        SteamAPI_ManualDispatch_FreeLastCallback(g_pipe);
    }
}
bool await(uint64_t call) {
    for (int waited = 0; waited < 5000; waited += 20) {
        pump();
        if (completed(call)) return true;
        Sleep(20);
    }
    return false;
}
bool envIs(const char* name, const char* want) {
    char value[32];
    DWORD n = GetEnvironmentVariableA(name, value, sizeof value);
    if (!n || n >= sizeof value) return false;
    for (DWORD i = 0; i <= n; ++i) if (value[i] != want[i]) return false;
    return true;
}
}

extern "C" {
int lobby_anchor;
void* lobby_relocation = &lobby_anchor;

DWORD lobby_entry() {
    const bool passthrough = envIs("PLAY_LOBBY_EXPECT", "passthrough");

    // 2: the game has not called ManualDispatch_Init (it may still be a
    // SteamAPI_RunCallbacks game), so every call is the provider's.
    g_pipe = SteamAPI_GetHSteamPipe();
    if (g_pipe <= 0) return 10;
    void* self = nullptr;
    uint64_t early = SteamAPI_ISteamMatchmaking_CreateLobby(self, 2 /*Public*/, 4);
    if (!early || early == TopBit) return 11;
    uint64_t earlyList = SteamAPI_ISteamMatchmaking_RequestLobbyList(self);
    if (!earlyList || earlyList == early) return 12;
    if (PlayLobbyCalls(Create) != 1 || PlayLobbyCalls(List) != 1) return 13;
    if (SteamAPI_ISteamMatchmaking_GetLobbyByIndex(self, 0) != PlayLobbyProviderLobby(1)) return 14;
    if (PlayLobbyCalls(ByIndex) != 1) return 15;

    SteamAPI_ManualDispatch_Init();
    if (PlayLobbyCalls(Init) != 1) return 16;

    if (passthrough) {
        // No tap: Init changes nothing, every call is still the provider's.
        uint64_t call = SteamAPI_ISteamMatchmaking_CreateLobby(self, 2, 4);
        if (!call || PlayLobbyCalls(Create) != 2) return 17;
        uint64_t join = SteamAPI_ISteamMatchmaking_JoinLobby(self, CmListA);
        if (!join || PlayLobbyCalls(Join) != 1) return 18;
        SteamAPI_ISteamMatchmaking_LeaveLobby(self, CmListA);
        if (PlayLobbyCalls(Leave) != 1) return 19;
        if (!await(early) || !await(earlyList) || !await(call) || !await(join)) return 20;
        LobbyCreated created;
        bool failed = true;
        if (!SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, call, &created, sizeof created, CbLobbyCreated, &failed) ||
            failed || created.lobby != PlayLobbyProviderLobby(0)) return 21;
        return 42;
    }

    // 1: the provider's two earlier calls are still announced, once each, and
    // their results are the provider's to give.
    // 3: the launcher's handle is a launcher handle (top bit plus a counter, which the
    // provider's handles can also share), and the provider never saw the call.
    uint64_t call = SteamAPI_ISteamMatchmaking_CreateLobby(self, 2, 4);
    if (!call || !(call & TopBit) || call == early || call == earlyList) return 22;
    if (PlayLobbyCalls(Create) != 1) return 23;
    if (!await(call)) return 24;
    if (!await(early) || !await(earlyList)) return 25;
    // 4: the call result is the CM's answer, announced by SteamAPICallCompleted_t.
    // A buffer that is too small or a wrong callback id writes nothing.
    LobbyCreated created;
    created.result = 0x7bad; created.lobby = 0x7bad;
    bool failed = true;
    if (SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, call, &created, sizeof created - 1, CbLobbyCreated, &failed)) return 26;
    if (SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, call, &created, sizeof created, CbLobbyEnter, &failed)) return 27;
    if (created.result != 0x7bad || created.lobby != 0x7bad || !failed) return 28;
    if (!SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, call, &created, sizeof created, CbLobbyCreated, &failed)) return 29;
    if (failed || created.result != 1 || created.lobby != CmCreated) return 30;
    // The LobbyEnter_t broadcast that follows a create.
    for (int waited = 0; waited < 3000 && !g_enterCount; waited += 20) { pump(); Sleep(20); }
    if (g_enterCount != 1 || g_enterLobby != CmCreated || g_enterResponse != 1) return 31;
    // A handle the provider gave still goes to the provider.
    LobbyCreated theirs;
    failed = true;
    if (!SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, early, &theirs, sizeof theirs, CbLobbyCreated, &failed) ||
        failed || theirs.lobby != PlayLobbyProviderLobby(0)) return 32;
    // Nothing was announced twice or dropped: the two provider calls and the one
    // launcher call, no more.
    if (g_completedCount != 3) return 33;

    // 7: the list, by index in and out of range, join and leave.
    uint64_t list = SteamAPI_ISteamMatchmaking_RequestLobbyList(self);
    if (!(list & TopBit) || list == call || PlayLobbyCalls(List) != 1) return 34;
    if (!await(list)) return 35;
    LobbyMatchList matches;
    if (!SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, list, &matches, sizeof matches, CbMatchList, &failed) ||
        failed || matches.count != 2) return 36;
    if (SteamAPI_ISteamMatchmaking_GetLobbyByIndex(self, 0) != CmListA) return 37;
    if (SteamAPI_ISteamMatchmaking_GetLobbyByIndex(self, 1) != CmListB) return 38;
    if (SteamAPI_ISteamMatchmaking_GetLobbyByIndex(self, 2) != 0) return 39;
    if (SteamAPI_ISteamMatchmaking_GetLobbyByIndex(self, -1) != 0) return 40;
    if (PlayLobbyCalls(ByIndex) != 1) return 41;
    uint64_t join = SteamAPI_ISteamMatchmaking_JoinLobby(self, CmListA);
    if (!(join & TopBit) || PlayLobbyCalls(Join) != 0) return 43;
    if (!await(join)) return 44;
    LobbyEnter entered;
    if (!SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, join, &entered, sizeof entered, CbLobbyEnter, &failed) ||
        failed || entered.lobby != CmListA || entered.response != 1) return 45;
    SteamAPI_ISteamMatchmaking_LeaveLobby(self, CmListA);
    if (PlayLobbyCalls(Leave) != 0) return 46;
    // Nothing the provider owns was freed out of turn.
    if (PlayLobbyCalls(FreeIdle) != 0 || PlayLobbyCalls(UnknownResult) != 0) return 47;
    return 42;
}
}
