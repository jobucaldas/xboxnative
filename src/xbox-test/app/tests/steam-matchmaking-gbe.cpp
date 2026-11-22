// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that loads the PINNED gbe_fork steam_api64.dll as a
// Steamworks game does and probes the two things the launcher's lobby callback
// splice (steam_matchmaking.h) depends on:
//   - the provider's own SteamAPICall_t handles are random 64-bit values (the top
//     bit is set on about half of them), so the launcher cannot route a result by
//     that bit: its handles are routed by ownership, and a provider handle with
//     the top bit set must still complete and give its own result;
//   - for a handle it never gave out, GetAPICallResult answers false and touches
//     nothing, GetNextCallback answers false when its queue is empty, and
//     FreeLastCallback with nothing outstanding is harmless.
// The Wine test in play-runtime.sh uses a stand-in provider written in this
// repository; this one proves the real pinned binary behaves the same way
// (tests/steam-matchmaking-gbe.sh). Prints RESULT lines to stdout; the shell test
// owns the assertions. Exit 0 = the provider answered every probe.
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>

#define S extern "C" __declspec(dllimport)
S bool   __cdecl SteamAPI_Init();
S void   __cdecl SteamAPI_Shutdown();
S int    __cdecl SteamAPI_GetHSteamPipe();
S void*  __cdecl SteamAPI_SteamMatchmaking_v009();
S uint64_t __cdecl SteamAPI_ISteamMatchmaking_CreateLobby(void*, int, int);
S uint64_t __cdecl SteamAPI_ISteamMatchmaking_RequestLobbyList(void*);
S void   __cdecl SteamAPI_ManualDispatch_Init();
S void   __cdecl SteamAPI_ManualDispatch_RunFrame(int);
S bool   __cdecl SteamAPI_ManualDispatch_GetNextCallback(int, void*);
S bool   __cdecl SteamAPI_ManualDispatch_GetAPICallResult(int, uint64_t, void*, int, int, bool*);
S void   __cdecl SteamAPI_ManualDispatch_FreeLastCallback(int);

namespace {
// Steam's CallbackMsg_t and SteamAPICallCompleted_t (Win64, packing 8).
#pragma pack(push, 8)
struct CallbackMsg { int32_t user; int32_t callback; uint8_t* param; int32_t size; };
struct CallCompleted { uint64_t call; int32_t callback; uint32_t size; };
#pragma pack(pop)
enum : int { CallbackCompleted = 703, CallbackLobbyCreated = 513, CallbackLobbyMatchList = 510 };
const uint64_t TopBit = 1ull << 63;

int g_pipe = 0;
HANDLE g_out = INVALID_HANDLE_VALUE;

// No CRT in this fixture, so print with the Win32 formatter. It has no %llu:
// 64-bit values are printed as two 32-bit halves.
void emit(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    DWORD written = 0;
    WriteFile(g_out, buf, (DWORD)n, &written, nullptr);
    WriteFile(g_out, "\r\n", 2, &written, nullptr);
}

// Drain the provider's callback queue. Returns how many messages it handed out,
// or -1 when it never ran dry (a provider that always has one is not "empty").
// `watch` is a call whose SteamAPICallCompleted_t is looked for; *seen is set when it arrives.
int drain(uint64_t watch, bool* seen) {
    int count = 0;
    SteamAPI_ManualDispatch_RunFrame(g_pipe);
    CallbackMsg msg{};
    while (SteamAPI_ManualDispatch_GetNextCallback(g_pipe, &msg)) {
        if (++count > 1000) return -1;
        if (watch && msg.callback == CallbackCompleted && msg.size == (int32_t)sizeof(CallCompleted) &&
            ((const CallCompleted*)msg.param)->call == watch && seen)
            *seen = true;
        SteamAPI_ManualDispatch_FreeLastCallback(g_pipe);
    }
    return count;
}
bool await(uint64_t call) {
    bool seen = false;
    for (int waited = 0; waited < 3000 && !seen; waited += 20) {
        drain(call, &seen);
        if (!seen) Sleep(20);
    }
    return seen;
}
// A buffer the provider must not touch, to see whether it did.
bool untouched(const uint8_t* bytes, int n) {
    for (int i = 0; i < n; ++i) if (bytes[i] != 0xAB) return false;
    return true;
}
} // namespace

extern "C" int guest_entry() {
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!SteamAPI_Init()) { emit("RESULT init=0"); return 10; }
    g_pipe = SteamAPI_GetHSteamPipe();
    SteamAPI_ManualDispatch_Init();
    void* mm = SteamAPI_SteamMatchmaking_v009();
    if (!mm) { emit("RESULT matchmaking=0"); return 11; }
    emit("RESULT init=1 pipe=%d", g_pipe);

    // FreeLastCallback before anything is outstanding must not hurt the queue.
    SteamAPI_ManualDispatch_FreeLastCallback(g_pipe);
    SteamAPI_ManualDispatch_FreeLastCallback(g_pipe);
    emit("RESULT free_idle=survived");

    // Whatever the provider queued at start-up is handed out and the queue then
    // runs dry: GetNextCallback answers false on an empty queue.
    int startup = drain(0, nullptr);
    CallbackMsg msg{};
    bool empty = !SteamAPI_ManualDispatch_GetNextCallback(g_pipe, &msg);
    emit("RESULT next_empty=%d startup_drained=%d", (int)empty, startup >= 0);
    SteamAPI_ManualDispatch_FreeLastCallback(g_pipe);

    // A handle the provider never gave out (a launcher handle: top bit set):
    // false, and neither the failed flag nor the buffer is written.
    uint8_t buffer[32];
    for (int i = 0; i < 32; ++i) buffer[i] = 0xAB;
    bool failed = true;
    bool answered = SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, TopBit | 1, buffer, 16, CallbackLobbyCreated, &failed);
    emit("RESULT unknown_result ret=%d failed_untouched=%d buffer_untouched=%d", (int)answered, (int)failed, (int)untouched(buffer, 32));

    // The provider's own handles: nonzero and distinct. The pin issues RANDOM 64-bit
    // handles, about half with the top bit set, so the launcher cannot tell its
    // handles from the provider's by that bit and routes by ownership instead.
    uint64_t create = SteamAPI_ISteamMatchmaking_CreateLobby(mm, 2 /*Public*/, 4);
    uint64_t list = SteamAPI_ISteamMatchmaking_RequestLobbyList(mm);
    emit("RESULT create nonzero=%d", (int)(create != 0));
    emit("RESULT list nonzero=%d", (int)(list != 0));
    emit("RESULT distinct=%d", (int)(create != list));
    emit("RESULT handles create=%08x%08x list=%08x%08x", (unsigned)(create >> 32), (unsigned)create, (unsigned)(list >> 32), (unsigned)list);

    // Its own call completes, and a handle it never gave out (its own with the top
    // bit flipped) neither answers nor costs the provider the result it holds.
    bool completed = await(create);
    emit("RESULT create_completed=%d", (int)completed);
    for (int i = 0; i < 32; ++i) buffer[i] = 0xAB;
    failed = true;
    bool flipped = SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, create ^ TopBit, buffer, 16, CallbackLobbyCreated, &failed);
    emit("RESULT flipped_result ret=%d failed_untouched=%d buffer_untouched=%d", (int)flipped, (int)failed, (int)untouched(buffer, 32));
    failed = true;
    bool own = SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, create, buffer, 16, CallbackLobbyCreated, &failed);
    emit("RESULT own_result ret=%d failed=%d", (int)own, (int)failed);

    // Calls whose handle has the top bit set still complete and still give their
    // result. Ask until the provider has issued one (every call has a 1 in 2 chance).
    int calls = 0, topbit = 0, completedAll = 0, ownAll = 0, flipAll = 0;
    for (int i = 0; i < 64 && (calls < 4 || !topbit); ++i) {
        uint64_t call = SteamAPI_ISteamMatchmaking_RequestLobbyList(mm);
        ++calls;
        if (call & TopBit) ++topbit;
        if (await(call)) ++completedAll;
        uint8_t result[4] = {0xAB, 0xAB, 0xAB, 0xAB};
        bool f = true;
        if (SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, call, result, 4, CallbackLobbyMatchList, &f) && !f) ++ownAll;
        for (int k = 0; k < 32; ++k) buffer[k] = 0xAB;
        f = true;
        if (!SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, call ^ TopBit, buffer, 16, CallbackLobbyMatchList, &f) && f && untouched(buffer, 32)) ++flipAll;
    }
    emit("RESULT lists calls=%d topbit=%d completed=%d own_ok=%d flipped_ok=%d", calls, topbit, completedAll, ownAll, flipAll);

    SteamAPI_Shutdown();
    return 0;
}

extern "C" void guest_start() { ExitProcess((UINT)guest_entry()); }
