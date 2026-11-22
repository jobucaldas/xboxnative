// SPDX-License-Identifier: GPL-3.0-or-later
// Owned stand-in for the Steam API provider (gbe_fork), used only by the
// off-device Play lobby test. It is not gbe_fork code and never ships: it
// reproduces the behaviour the lobby callback splice depends on, so Play's hooks
// can be observed without the real (uncommittable) steam_api64.dll.
//
// The behaviour, from gbe_fork's dll/steam_matchmaking.cpp and its
// ManualDispatch exports:
//   - ISteamMatchmaking calls answer a SteamAPICall_t that is NOT small: the pinned
//     gbe_fork issues random 64-bit handles and about half have the top bit set, so
//     this stand-in hands out handles with it set. The launcher must route a
//     result by which handles it issued, never by that bit. The call's result is
//     held for GetAPICallResult;
//   - each completed call queues a SteamAPICallCompleted_t (703) that
//     GetNextCallback hands out, one outstanding at a time until
//     FreeLastCallback;
//   - GetAPICallResult for a handle it never gave out answers false and touches
//     nothing; FreeLastCallback with nothing outstanding is harmless.
// Test-only exports report what the provider was asked, so the guest can prove
// the launcher's hooks sent a call on to it (or kept it away). It links against
// kernel32 only, like the other Play fixtures.
#include <windows.h>
#include <stdint.h>

extern "C" void* memset(void* d, int c, size_t n) {
    unsigned char* p = (unsigned char*)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
extern "C" void* memcpy(void* d, const void* s, size_t n) {
    unsigned char* p = (unsigned char*)d;
    const unsigned char* q = (const unsigned char*)s;
    while (n--) *p++ = *q++;
    return d;
}

// SDK callback structs, Win64 packing 8.
#pragma pack(push, 8)
struct CallbackMsg { int32_t user; int32_t callback; uint8_t* param; int32_t size; };
struct CallCompleted { uint64_t call; int32_t callback; uint32_t size; };
struct LobbyCreated { int32_t result; uint64_t lobby; };
struct LobbyEnter { uint64_t lobby; uint32_t permissions; bool locked; uint32_t response; };
struct LobbyMatchList { uint32_t count; };
#pragma pack(pop)

#define MAX_MSGS 16
#define MAX_DONE 16
struct Msg { int callback; uint32_t size; uint8_t data[32]; };
struct Done { uint64_t call; int callback; uint32_t size; uint8_t data[32]; };

static Msg queued[MAX_MSGS];
static int head, tail;
static Msg current;
static bool outstanding;
static Done done[MAX_DONE];
static int doneCount;
static uint64_t nextCall = 0x9f12345678900001ull;   // top bit set, like half of gbe_fork's
// 0 create, 1 list, 2 by-index, 3 join, 4 leave, 5 init, 6 pipe, 7 free with nothing
// outstanding, 8 result asked for a handle the provider never gave
static int counters[9];

static const uint64_t ProviderLobby = 0x1100001000099ull;   // fixture ids, never a real account
static const uint64_t ProviderByIndex = 0x1100001000A00ull;

static void copyIn(uint8_t* to, const void* from, uint32_t n) { for (uint32_t i = 0; i < n; ++i) to[i] = ((const uint8_t*)from)[i]; }

static uint64_t complete(int callback, const void* data, uint32_t size) {
    uint64_t call = nextCall++;
    if (doneCount < MAX_DONE) {
        Done* d = &done[doneCount++];
        d->call = call; d->callback = callback; d->size = size;
        copyIn(d->data, data, size);
    }
    CallCompleted announce;
    announce.call = call; announce.callback = callback; announce.size = size;
    if ((tail + 1) % MAX_MSGS != head) {
        Msg* m = &queued[tail];
        tail = (tail + 1) % MAX_MSGS;
        m->callback = 703; m->size = sizeof announce;
        copyIn(m->data, &announce, sizeof announce);
    }
    return call;
}

extern "C" {
__declspec(dllexport) int __cdecl SteamAPI_Init(void) { return 1; }

__declspec(dllexport) int32_t __cdecl SteamAPI_GetHSteamPipe(void) { ++counters[6]; return 1; }
__declspec(dllexport) void __cdecl SteamAPI_ManualDispatch_Init(void) { ++counters[5]; }
__declspec(dllexport) void __cdecl SteamAPI_ManualDispatch_RunFrame(int32_t pipe) { (void)pipe; }
__declspec(dllexport) bool __cdecl SteamAPI_ManualDispatch_GetNextCallback(int32_t pipe, CallbackMsg* msg) {
    (void)pipe;
    if (head == tail || !msg) return false;
    current = queued[head];
    head = (head + 1) % MAX_MSGS;
    msg->user = 1; msg->callback = current.callback; msg->param = current.data; msg->size = (int32_t)current.size;
    outstanding = true;
    return true;
}
__declspec(dllexport) void __cdecl SteamAPI_ManualDispatch_FreeLastCallback(int32_t pipe) {
    (void)pipe;
    if (!outstanding) ++counters[7];
    outstanding = false;
}
__declspec(dllexport) bool __cdecl SteamAPI_ManualDispatch_GetAPICallResult(int32_t pipe, uint64_t call, void* out, int cub, int expected, bool* failed) {
    (void)pipe;
    for (int i = 0; i < doneCount; ++i) {
        Done* d = &done[i];
        if (d->call != call) continue;
        if (d->callback != expected || cub < (int)d->size || !out) return false;
        copyIn((uint8_t*)out, d->data, d->size);
        if (failed) *failed = false;
        return true;
    }
    ++counters[8];
    return false;
}

__declspec(dllexport) uint64_t __cdecl SteamAPI_ISteamMatchmaking_CreateLobby(void* self, int type, int max) {
    (void)self; (void)type; (void)max;
    ++counters[0];
    LobbyCreated r; r.result = 1; r.lobby = ProviderLobby;
    return complete(513, &r, sizeof r);
}
__declspec(dllexport) uint64_t __cdecl SteamAPI_ISteamMatchmaking_RequestLobbyList(void* self) {
    (void)self;
    ++counters[1];
    LobbyMatchList r; r.count = 0;
    return complete(510, &r, sizeof r);
}
// A lobby the provider's own LAN list would hold, so the guest can tell who answered.
__declspec(dllexport) uint64_t __cdecl SteamAPI_ISteamMatchmaking_GetLobbyByIndex(void* self, int index) {
    (void)self;
    ++counters[2];
    return ProviderByIndex + (uint64_t)index;
}
__declspec(dllexport) uint64_t __cdecl SteamAPI_ISteamMatchmaking_JoinLobby(void* self, uint64_t lobby) {
    (void)self;
    ++counters[3];
    LobbyEnter r;
    r.lobby = lobby; r.permissions = 0; r.locked = false; r.response = 1;
    return complete(504, &r, sizeof r);
}
__declspec(dllexport) void __cdecl SteamAPI_ISteamMatchmaking_LeaveLobby(void* self, uint64_t lobby) { (void)self; (void)lobby; ++counters[4]; }

// Test-only: how often the provider was asked, by counter index (see above).
__declspec(dllexport) int __cdecl PlayLobbyCalls(int which) { return which >= 0 && which < 9 ? counters[which] : -1; }
// Test-only: the lobby the provider answers CreateLobby and GetLobbyByIndex(0) with.
__declspec(dllexport) uint64_t __cdecl PlayLobbyProviderLobby(int byIndex) { return byIndex ? ProviderByIndex : ProviderLobby; }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, void* reserved) { (void)h; (void)reason; (void)reserved; return TRUE; }

// Play maps guest images at an address of its choosing, so the image must carry a
// relocation directory. Nothing here needs relocating on its own; this pointer
// gives the linker one, as a real provider's code and data always do.
__declspec(dllexport) int provider_anchor;
__declspec(dllexport) int* provider_relocation = &provider_anchor;
