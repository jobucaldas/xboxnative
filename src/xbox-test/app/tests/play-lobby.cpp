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
// Phase 2 (#179): lobby data, members, owner, limit, type, list filter, chat, game server, invite.
STEAM_API const char* __cdecl SteamAPI_ISteamMatchmaking_GetLobbyData(void*, uint64_t, const char*);
STEAM_API bool __cdecl SteamAPI_ISteamMatchmaking_SetLobbyData(void*, uint64_t, const char*, const char*);
STEAM_API bool __cdecl SteamAPI_ISteamMatchmaking_DeleteLobbyData(void*, uint64_t, const char*);
STEAM_API int __cdecl SteamAPI_ISteamMatchmaking_GetLobbyDataCount(void*, uint64_t);
STEAM_API bool __cdecl SteamAPI_ISteamMatchmaking_GetLobbyDataByIndex(void*, uint64_t, int, char*, int, char*, int);
STEAM_API int __cdecl SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(void*, uint64_t);
STEAM_API uint64_t __cdecl SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(void*, uint64_t, int);
STEAM_API uint64_t __cdecl SteamAPI_ISteamMatchmaking_GetLobbyOwner(void*, uint64_t);
STEAM_API int __cdecl SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(void*, uint64_t);
STEAM_API bool __cdecl SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit(void*, uint64_t, int);
STEAM_API bool __cdecl SteamAPI_ISteamMatchmaking_SetLobbyType(void*, uint64_t, int);
STEAM_API void __cdecl SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(void*, const char*, const char*, int);
STEAM_API bool __cdecl SteamAPI_ISteamMatchmaking_SendLobbyChatMsg(void*, uint64_t, const void*, int);
STEAM_API int __cdecl SteamAPI_ISteamMatchmaking_GetLobbyChatEntry(void*, uint64_t, int, uint64_t*, void*, int, int32_t*);
STEAM_API bool __cdecl SteamAPI_ISteamMatchmaking_GetLobbyGameServer(void*, uint64_t, uint32_t*, uint16_t*, uint64_t*);
STEAM_API void __cdecl SteamAPI_ISteamMatchmaking_SetLobbyGameServer(void*, uint64_t, uint32_t, uint16_t, uint64_t);
STEAM_API bool __cdecl SteamAPI_ISteamMatchmaking_InviteUserToLobby(void*, uint64_t, uint64_t);
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
struct LobbyDataUpdate { uint64_t lobby; uint64_t member; uint8_t success; };
struct LobbyChatMsg { uint64_t lobby; uint64_t user; uint8_t type; uint32_t chatId; };
struct LobbyGameCreated { uint64_t lobby; uint64_t server; uint32_t ip; uint16_t port; };
#pragma pack(pop)
static_assert(sizeof(LobbyDataUpdate) == 24 && sizeof(LobbyChatMsg) == 24 && sizeof(LobbyGameCreated) == 24,
              "Win64 callback layouts of the SDK");

enum { Create, List, ByIndex, Join, Leave, Init, Pipe, FreeIdle, UnknownResult,
       P2First, P2Last = P2First + 16 };   // GetLobbyData .. InviteUserToLobby, in the provider's counter order
enum : int { CbLobbyEnter = 504, CbLobbyDataUpdate = 505, CbLobbyChatMsg = 507, CbLobbyGameCreated = 509,
             CbMatchList = 510, CbLobbyCreated = 513, CbCompleted = 703 };

// The fake CM's lobbies (play-runner.cpp's `lobbies` mode). Fixture ids.
const uint64_t CmCreated = 0x1100001000777ull;
const uint64_t CmListA = 0x1100001000010ull;
const uint64_t CmListB = 0x1100001000020ull;
const uint64_t TopBit = 1ull << 63;
// The members the fake CM reports, and the provider's own (play-lobby-provider.cpp).
const uint64_t CmOwner = 0x110000100000001ull;
const uint64_t CmHost = 0x110000100000002ull;
const uint64_t CmPeer = 0x110000100000003ull;
const uint64_t ProviderMember = 0x1100001000B00ull;
const uint64_t ProviderOwner = 0x1100001000C00ull;

int32_t g_pipe;
uint64_t g_completed[32];
int g_completedCount;
uint32_t g_enterCount;
uint64_t g_enterLobby;
uint32_t g_enterResponse;
// Phase 2 broadcasts, read with the layouts above. A message of the wrong size is not counted.
uint32_t g_dataCount, g_chatCount, g_serverCount;
uint64_t g_dataLobby, g_dataMember, g_chatLobby, g_chatUser, g_serverLobby, g_serverId;
uint8_t g_dataSuccess, g_chatType;
uint32_t g_chatId, g_serverIp;
uint16_t g_serverPort;

bool streq(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
bool memEq(const void* a, const void* b, int n) {
    for (int i = 0; i < n; ++i) if (((const uint8_t*)a)[i] != ((const uint8_t*)b)[i]) return false;
    return true;
}
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
        } else if (msg.callback == CbLobbyDataUpdate && msg.size == (int32_t)sizeof(LobbyDataUpdate)) {
            const LobbyDataUpdate* update = (const LobbyDataUpdate*)msg.param;
            ++g_dataCount; g_dataLobby = update->lobby; g_dataMember = update->member; g_dataSuccess = update->success;
        } else if (msg.callback == CbLobbyChatMsg && msg.size == (int32_t)sizeof(LobbyChatMsg)) {
            const LobbyChatMsg* chat = (const LobbyChatMsg*)msg.param;
            ++g_chatCount; g_chatLobby = chat->lobby; g_chatUser = chat->user; g_chatType = chat->type; g_chatId = chat->chatId;
        } else if (msg.callback == CbLobbyGameCreated && msg.size == (int32_t)sizeof(LobbyGameCreated)) {
            const LobbyGameCreated* made = (const LobbyGameCreated*)msg.param;
            ++g_serverCount; g_serverLobby = made->lobby; g_serverId = made->server; g_serverIp = made->ip; g_serverPort = made->port;
        }
        SteamAPI_ManualDispatch_FreeLastCallback(g_pipe);
    }
}
// Pumps until a broadcast counter reaches `want`.
bool waitFor(const uint32_t& counter, uint32_t want) {
    for (int waited = 0; waited < 3000 && counter < want; waited += 20) { pump(); Sleep(20); }
    return counter == want;
}
// The phase-2 exports as the provider answers them: values no launcher state could
// produce, each counted once per round. The guest makes this round before
// ManualDispatch_Init, and again after it when no tap is installed.
DWORD providerPhase2(void* self, int round) {
    char key[8], value[8];
    uint32_t ip = 0; uint16_t port = 0; uint64_t server = 0, user = 0; int32_t type = 0;
    const uint64_t lobby = CmCreated;
    if (!streq(SteamAPI_ISteamMatchmaking_GetLobbyData(self, lobby, "k"), "provider-data")) return 50;
    if (!SteamAPI_ISteamMatchmaking_SetLobbyData(self, lobby, "k", "v")) return 51;
    if (!SteamAPI_ISteamMatchmaking_DeleteLobbyData(self, lobby, "k")) return 52;
    if (SteamAPI_ISteamMatchmaking_GetLobbyDataCount(self, lobby) != 77) return 53;
    if (!SteamAPI_ISteamMatchmaking_GetLobbyDataByIndex(self, lobby, 0, key, sizeof key, value, sizeof value) ||
        !streq(key, "pk") || !streq(value, "pv")) return 54;
    if (SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(self, lobby) != 9) return 55;
    if (SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(self, lobby, 2) != ProviderMember + 2) return 56;
    if (SteamAPI_ISteamMatchmaking_GetLobbyOwner(self, lobby) != ProviderOwner) return 57;
    if (SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(self, lobby) != 11) return 58;
    if (!SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit(self, lobby, 8)) return 59;
    if (!SteamAPI_ISteamMatchmaking_SetLobbyType(self, lobby, 1)) return 60;
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(self, "mode", "coop", 0);
    if (!SteamAPI_ISteamMatchmaking_SendLobbyChatMsg(self, lobby, "hi", 2)) return 61;
    if (SteamAPI_ISteamMatchmaking_GetLobbyChatEntry(self, lobby, 0, &user, key, sizeof key, &type) != 5) return 62;
    if (!SteamAPI_ISteamMatchmaking_GetLobbyGameServer(self, lobby, &ip, &port, &server) || ip != 1 || port != 2 || server != 3) return 63;
    SteamAPI_ISteamMatchmaking_SetLobbyGameServer(self, lobby, 1, 2, 3);
    if (!SteamAPI_ISteamMatchmaking_InviteUserToLobby(self, lobby, CmPeer)) return 64;
    for (int i = P2First; i <= P2Last; ++i) if (PlayLobbyCalls(i) != round) return 100 + i;
    return 0;
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
    // The phase-2 exports (data, members, chat, game server, invite) are the provider's
    // too until the game calls ManualDispatch_Init: each answers and is counted once.
    if (DWORD bad = providerPhase2(self, 1)) return bad;

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
        // Phase 2 with no tap: still the provider's, a second round of every export.
        if (DWORD bad = providerPhase2(self, 2)) return bad;
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

    // 5 (#179 phase 2): the created lobby read through the launcher. The fake CM
    // settled it with KeyValues {map=alpha, mode=coop}, an owner and a peer.
    if (!streq(SteamAPI_ISteamMatchmaking_GetLobbyData(self, CmCreated, "mode"), "coop") ||
        !streq(SteamAPI_ISteamMatchmaking_GetLobbyData(self, CmCreated, "map"), "alpha")) return 130;
    if (!streq(SteamAPI_ISteamMatchmaking_GetLobbyData(self, CmCreated, "absent"), "")) return 131;
    if (SteamAPI_ISteamMatchmaking_GetLobbyDataCount(self, CmCreated) != 2) return 132;
    char key[16], value[16];
    if (!SteamAPI_ISteamMatchmaking_GetLobbyDataByIndex(self, CmCreated, 0, key, sizeof key, value, sizeof value) ||
        !streq(key, "map") || !streq(value, "alpha")) return 133;
    if (SteamAPI_ISteamMatchmaking_GetLobbyDataByIndex(self, CmCreated, 2, key, sizeof key, value, sizeof value)) return 134;
    if (SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(self, CmCreated) != 2) return 135;
    if (SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(self, CmCreated, 0) != CmOwner ||
        SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(self, CmCreated, 1) != CmPeer ||
        SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(self, CmCreated, 2) != 0) return 136;
    if (SteamAPI_ISteamMatchmaking_GetLobbyOwner(self, CmCreated) != CmOwner) return 137;
    if (SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(self, CmCreated) != 4) return 138;
    // The owner's write changes the local copy at once; the fake CM then pushes the
    // lobby back with a key of its own, which is the game's LobbyDataUpdate_t.
    if (!SteamAPI_ISteamMatchmaking_SetLobbyData(self, CmCreated, "mode", "pvp")) return 139;
    if (!streq(SteamAPI_ISteamMatchmaking_GetLobbyData(self, CmCreated, "mode"), "pvp")) return 140;
    if (!waitFor(g_dataCount, 1)) return 141;
    if (g_dataLobby != CmCreated || g_dataMember != CmCreated || g_dataSuccess != 1) return 142;
    if (!streq(SteamAPI_ISteamMatchmaking_GetLobbyData(self, CmCreated, "echo"), "1")) return 143;
    if (!SteamAPI_ISteamMatchmaking_DeleteLobbyData(self, CmCreated, "map") ||
        SteamAPI_ISteamMatchmaking_DeleteLobbyData(self, CmCreated, "absent")) return 144;
    if (SteamAPI_ISteamMatchmaking_GetLobbyDataCount(self, CmCreated) != 2) return 145;   // mode, echo
    if (!SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit(self, CmCreated, 8) ||
        SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(self, CmCreated) != 8) return 146;
    if (!SteamAPI_ISteamMatchmaking_SetLobbyType(self, CmCreated, 1) ||
        SteamAPI_ISteamMatchmaking_SetLobbyType(self, CmCreated, 9)) return 147;
    // Chat: sent to the CM, which echoes it as a push -> LobbyChatMsg_t -> GetLobbyChatEntry.
    const char hello[] = "hello";
    if (!SteamAPI_ISteamMatchmaking_SendLobbyChatMsg(self, CmCreated, hello, 5)) return 148;
    if (SteamAPI_ISteamMatchmaking_SendLobbyChatMsg(self, CmCreated + 1, hello, 5)) return 149;
    if (!waitFor(g_chatCount, 1)) return 150;
    if (g_chatLobby != CmCreated || g_chatUser != CmOwner || g_chatType != 1) return 151;
    uint64_t chatUser = 0; int32_t chatType = 0; char text[16];
    if (SteamAPI_ISteamMatchmaking_GetLobbyChatEntry(self, CmCreated, (int)g_chatId, &chatUser, text, sizeof text, &chatType) != 5 ||
        chatUser != CmOwner || chatType != 1 || !memEq(text, hello, 5)) return 152;
    // Game server: the CM pushes it back -> LobbyGameCreated_t -> GetLobbyGameServer.
    SteamAPI_ISteamMatchmaking_SetLobbyGameServer(self, CmCreated, 0x7f000001u, 27015, 0x1860000000042ull);
    if (!waitFor(g_serverCount, 1)) return 153;
    if (g_serverLobby != CmCreated || g_serverId != 0x1860000000042ull || g_serverIp != 0x7f000001u || g_serverPort != 27015) return 154;
    uint32_t ip = 0; uint16_t port = 0; uint64_t server = 0;
    if (!SteamAPI_ISteamMatchmaking_GetLobbyGameServer(self, CmCreated, &ip, &port, &server) ||
        ip != 0x7f000001u || port != 27015 || server != 0x1860000000042ull) return 155;
    // Invite: only into the lobby the account is in, and only for a real invitee.
    if (!SteamAPI_ISteamMatchmaking_InviteUserToLobby(self, CmCreated, CmPeer)) return 156;
    if (SteamAPI_ISteamMatchmaking_InviteUserToLobby(self, CmCreated, 0) ||
        SteamAPI_ISteamMatchmaking_InviteUserToLobby(self, CmCreated + 1, CmPeer)) return 157;
    // The list filter is kept for the next request, not sent to the provider.
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(self, "mode", "coop", 0);

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
    // The joined lobby, as the fake CM reported it: KeyValues {mode=join}, a host that
    // owns it and this account as a second member. Not ours to write.
    if (!streq(SteamAPI_ISteamMatchmaking_GetLobbyData(self, CmListA, "mode"), "join") ||
        SteamAPI_ISteamMatchmaking_GetLobbyDataCount(self, CmListA) != 1) return 158;
    if (SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(self, CmListA) != 2 ||
        SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(self, CmListA, 0) != CmOwner ||
        SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(self, CmListA, 1) != CmHost) return 159;
    if (SteamAPI_ISteamMatchmaking_GetLobbyOwner(self, CmListA) != CmHost ||
        SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(self, CmListA) != 4) return 160;
    if (SteamAPI_ISteamMatchmaking_SetLobbyData(self, CmListA, "mode", "x")) return 161;
    SteamAPI_ISteamMatchmaking_LeaveLobby(self, CmListA);
    if (PlayLobbyCalls(Leave) != 0) return 46;
    // Nothing the provider owns was freed out of turn.
    if (PlayLobbyCalls(FreeIdle) != 0 || PlayLobbyCalls(UnknownResult) != 0) return 47;
    // Every phase-2 call above was the launcher's: the provider still has only the one
    // round it answered before ManualDispatch_Init.
    for (int i = P2First; i <= P2Last; ++i) if (PlayLobbyCalls(i) != 1) return 170 + i;
    return 42;
}
}
