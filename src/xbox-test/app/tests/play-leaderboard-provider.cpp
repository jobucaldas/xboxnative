// SPDX-License-Identifier: GPL-3.0-or-later
// Owned stand-in for the Steam API provider (gbe_fork), used only by the
// off-device Play test. It is not gbe_fork code and never ships: it reproduces
// the one behaviour the leaderboard bridge depends on, so Play's seed can be
// observed without the real (uncommittable) steam_api64.dll.
//
// The behaviour, from gbe_fork's dll/steam_user_stats_leaderboard.cpp:
//   - FindLeaderboard/FindOrCreateLeaderboard resolve a board from its own cache
//     and read the leaderboard file the FIRST time that board is found in the
//     process (cache_leaderboard_ifneeded -> load_leaderboard_entries);
//   - the file is <local_save_path>\<appid>\leaderboard\<lowercased name>, a
//     flat array of little-endian uint32: steamid low, steamid high, score,
//     details count, details...;
//   - DownloadLeaderboardEntries answers from that in-memory vector.
// This stand-in does exactly that, records what it read, and reports it through
// a test-only export so the guest can assert the ordering. It links against
// kernel32 only, like the other Play fixtures.
#include <windows.h>
#include <stdint.h>

// kernel32 only, like the other Play fixtures. The compiler still lowers struct
// clears and copies to these, so they are defined here rather than pulled from a
// C runtime the fixtures do not link.
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

#define MAX_BOARDS 8
#define MAX_ENTRIES 64
#define MAX_DETAILS 8
#define MAX_WORDS 1024
#define MAX_CALLS 16

typedef struct { uint64_t steamid; int32_t score; int32_t details[MAX_DETAILS]; int32_t count; } Entry;
typedef struct { char name[128]; int entries; Entry rows[MAX_ENTRIES]; } Board;

static Board boards[MAX_BOARDS];
static int boardCount;
static char traceBuf[1024];
static int traceLen;

static int same(const char* a, const char* b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static int sameNoCase(const char* a, const char* b) {
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
        if (x != y) return 0;
        if (!x) return 1;
    }
}
static void copyLower(char* out, int size, const char* in) {
    int i = 0;
    for (; in[i] && i < size - 1; ++i) out[i] = (in[i] >= 'A' && in[i] <= 'Z') ? char(in[i] - 'A' + 'a') : in[i];
    out[i] = 0;
}
static void append(const char* s) {
    while (*s && traceLen < (int)sizeof traceBuf - 1) traceBuf[traceLen++] = *s++;
    traceBuf[traceLen] = 0;
}
static void appendInt(int v) {
    char digits[12];
    int n = 0;
    if (v < 0) { append("-"); v = -v; }
    do { digits[n++] = char('0' + v % 10); v /= 10; } while (v);
    while (n) { char one[2] = {digits[--n], 0}; append(one); }
}

// <save path>\<appid>\leaderboard\<lowercased name>
static void leaderboardFile(const char* name, char* out, DWORD size) {
    char path[MAX_PATH] = {0}, appid[32] = {0}, lowered[128];
    DWORD n = GetEnvironmentVariableA("PLAY_LB_SAVE_PATH", path, MAX_PATH);
    GetEnvironmentVariableA("PLAY_LB_APPID", appid, sizeof appid);
    copyLower(lowered, sizeof lowered, name);
    DWORD at = 0;
    for (DWORD i = 0; i < n && at + 1 < size; ++i) out[at++] = path[i];
    if (at && out[at - 1] != '\\' && at + 1 < size) out[at++] = '\\';
    for (DWORD i = 0; appid[i] && at + 1 < size; ++i) out[at++] = appid[i];
    const char* tail = "\\leaderboard\\";
    for (DWORD i = 0; tail[i] && at + 1 < size; ++i) out[at++] = tail[i];
    for (DWORD i = 0; lowered[i] && at + 1 < size; ++i) out[at++] = lowered[i];
    out[at] = 0;
}

static Board* find(const char* name) {
    int i;
    for (i = 0; i < boardCount; ++i) if (sameNoCase(boards[i].name, name)) return &boards[i];
    if (boardCount >= MAX_BOARDS) return 0;
    Board* b = &boards[boardCount++];
    for (i = 0; i < (int)sizeof *b; ++i) ((char*)b)[i] = 0;
    for (i = 0; name[i] && i < (int)sizeof b->name - 1; ++i) b->name[i] = name[i];
    // The first Find of a board reads the file; later ones never do.
    char file[MAX_PATH];
    leaderboardFile(name, file, sizeof file);
    HANDLE h = CreateFileA(file, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h != INVALID_HANDLE_VALUE) {
        static uint32_t words[MAX_WORDS];
        DWORD got = 0, read = 0;
        ReadFile(h, words, sizeof words, &got, 0);
        CloseHandle(h);
        size_t n = got / 4;
        size_t at = 0;
        while (at + 4 <= n && b->entries < MAX_ENTRIES) {
            Entry* e = &b->rows[b->entries];
            e->steamid = (uint64_t)words[at] | ((uint64_t)words[at + 1] << 32);
            e->score = (int32_t)words[at + 2];
            uint32_t count = words[at + 3];
            at += 4;
            if (at + count > n) break;
            while (count-- && e->count < MAX_DETAILS) e->details[e->count++] = (int32_t)words[at++];
            ++b->entries;
        }
        (void)read;
    }
    append(name); append("="); appendInt(b->entries); append(";");
    return b;
}

extern "C" {
__declspec(dllexport) int __cdecl SteamAPI_Init(void) { return 1; }

// Like gbe_fork: Find answers with a SteamAPICall_t, and the board handle is
// delivered separately in LeaderboardFindResult_t. The two are different
// values on purpose, so a launcher that keys uploads off the call id fails.
static uint64_t nextCall = 100;
static uint64_t callBoard[MAX_CALLS];

// SteamLeaderboard_t is an index into the provider's own cache, 1-based, and it
// is stable for a board: a second Find of the same board answers the same
// handle, as gbe_fork's cached_leaderboards index does.
__declspec(dllexport) uint64_t __cdecl SteamAPI_ISteamUserStats_FindLeaderboard(void* self, const char* name) {
    (void)self;
    if (!name || !*name) return 0;
    Board* b = find(name);
    if (!b) return 0;
    if (nextCall >= 100 + MAX_CALLS) return 0;
    callBoard[nextCall - 100] = (uint64_t)(b - boards) + 1;
    return nextCall++;
}
__declspec(dllexport) uint64_t __cdecl SteamAPI_ISteamUserStats_FindOrCreateLeaderboard(void* self, const char* name, int sort, int display) {
    (void)sort; (void)display;
    return SteamAPI_ISteamUserStats_FindLeaderboard(self, name);
}
// The handle the game passes back to Download/Upload, as the CallResult gave it.
__declspec(dllexport) const char* __cdecl SteamAPI_ISteamUserStats_GetLeaderboardName(void* self, uint64_t board) {
    (void)self;
    if (board == 0 || board > (uint64_t)boardCount) return "";
    return boards[board - 1].name;
}
__declspec(dllexport) uint64_t __cdecl SteamAPI_ISteamUserStats_DownloadLeaderboardEntries(void* self, uint64_t board, int request, int start, int end) {
    (void)self; (void)request;
    int n;
    if (board == 0 || board > (uint64_t)boardCount) return 0;
    n = boards[board - 1].entries;
    if (end - start + 1 < n) n = end - start + 1;
    append("download="); appendInt(n); append(";");
    return (uint64_t)n;   // the entry count, as the CallResult carries
}
typedef struct { uint64_t user; int32_t rank, score, details; uint64_t ugc; } LeaderboardEntry_t;
__declspec(dllexport) int __cdecl SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry(void* self, uint64_t board, int index, LeaderboardEntry_t* out, int32_t* details, int maxDetails) {
    (void)self;
    Board* b;
    Entry* e;
    int i;
    if (board == 0 || board > (uint64_t)boardCount) return 0;
    b = &boards[board - 1];
    if (index < 0 || index >= b->entries) return 0;
    e = &b->rows[index];
    if (out) {
        out->user = e->steamid; out->rank = index + 1; out->score = e->score;
        out->details = e->count; out->ugc = 0;
    }
    for (i = 0; details && i < maxDetails && i < e->count; ++i) details[i] = e->details[i];
    return 1;
}
__declspec(dllexport) uint64_t __cdecl SteamAPI_ISteamUserStats_UploadLeaderboardScore(void* self, uint64_t board, int method, int32_t score, const int32_t* details, int count) {
    (void)self; (void)method; (void)score; (void)details; (void)count;
    append("upload="); appendInt((int)board); append(";");
    return 1;
}
// ---- ISteamFriends, answering the way gbe_fork does with disable_networking=1
// (dll/steam_friends.cpp): its friend list is only ever filled from its own LAN
// network, so every SteamID that is not the local user reads "Unknown User";
// RequestUserInformation is a TODO that answers false; GetPersonaName is the
// staged account name. The launcher's persona tap replaces these answers in
// memory, and the provider's own call still runs (the call counter proves it).
static int personaNameCalls, personaRequestCalls;
__declspec(dllexport) const char* __cdecl SteamAPI_ISteamFriends_GetFriendPersonaName(void* self, uint64_t steamid) {
    (void)self; (void)steamid;
    ++personaNameCalls;
    return "Unknown User";
}
__declspec(dllexport) const char* __cdecl SteamAPI_ISteamFriends_GetPersonaName(void* self) {
    (void)self;
    return "Player";
}
__declspec(dllexport) int __cdecl SteamAPI_ISteamFriends_RequestUserInformation(void* self, uint64_t steamid, int nameOnly) {
    (void)self; (void)steamid; (void)nameOnly;
    ++personaRequestCalls;
    return 0;   // gbe_fork's TODO: "we already know everything"
}
// Test-only: what LeaderboardFindResult_t.m_hSteamLeaderboard carries for the
// call Find returned. The fixture guest has no CallResult plumbing, so it reads
// the handle here; a real game gets it from the callback.
__declspec(dllexport) uint64_t __cdecl PlayLeaderboardCallBoard(uint64_t call) {
    if (call < 100 || call >= 100 + MAX_CALLS) return 0;
    return callBoard[call - 100];
}
// Test-only: what the provider read, so the guest can assert the seed landed
// before the first Find and that the rows are the ones Steam sent.
__declspec(dllexport) const char* __cdecl PlayLeaderboardTrace(void) { return traceBuf; }
// Test-only: how many times the provider's own persona calls ran, so the guest
// can prove the launcher replaced an answer instead of skipping the call.
__declspec(dllexport) int __cdecl PlayLeaderboardPersonaCalls(int which) {
    return which == 0 ? personaNameCalls : personaRequestCalls;
}
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, void* reserved) { (void)h; (void)reason; (void)reserved; return TRUE; }

// Play maps guest images at an address of its choosing, so the image must carry a
// relocation directory. Nothing here needs relocating on its own; this pointer
// gives the linker one, as a real provider's code and data always do.
__declspec(dllexport) int provider_anchor;
__declspec(dllexport) int* provider_relocation = &provider_anchor;
