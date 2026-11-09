// SPDX-License-Identifier: GPL-3.0-or-later
// Owned stand-in for Discord's SDKs, used only by the off-device Play test.
// It is not Discord code and never ships: it reproduces the part of both SDKs
// that matters here, the connect to the Discord client's local IPC pipe
// \?\pipe\discord-ipc-N, so Play's behaviour can be observed without the real
// (uncommittable) discord_game_sdk.dll or discord-rpc.dll. Built and removed by
// play-runtime.sh; never committed as a binary.
//
// The real SDKs, when no client is running (discord-rpc BaseConnection::Open,
// and the Game SDK's DiscordCreate), walk discord-ipc-0..9, see
// ERROR_FILE_NOT_FOUND on each, and carry on without presence: discord-rpc
// returns from Discord_Initialize without an error callback, and the Game SDK
// returns DiscordResult_InternalError (4) with no device. This stand-in does the
// same walk and reports what it saw.
#include <windows.h>

namespace {
constexpr int kInternalError = 4;   // EDiscordResult, discord_game_sdk.h

// discord-rpc's BaseConnection::Open, reduced to the part that decides the
// outcome: the same path form, the same access, the same error handling.
DWORD connectDiscordPipe() {
    wchar_t name[] = L"\\\\?\\pipe\\discord-ipc-0";
    const size_t digit = sizeof name / sizeof name[0] - 2;
    for (;;) {
        HANDLE pipe = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) { CloseHandle(pipe); return ERROR_SUCCESS; }
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            if (name[digit] < L'9') { ++name[digit]; continue; }
        }
        return error;
    }
}

DWORD lastPipeError = ERROR_SUCCESS;
}

extern "C" {
// An absolute address in the image, so this DLL carries a relocation directory
// the way the real SDKs do; Play maps only images it can relocate.
const char* sdkName = "discord_game_sdk";
__declspec(dllexport) void* sdkRelocation = &sdkName;

// The Game SDK's one entry point. Play refuses the discord:// launch the real
// SDK attempts when the pipe is missing, so this must return at once.
__declspec(dllexport) int __cdecl DiscordCreate(int, void*, void** device) {
    lastPipeError = connectDiscordPipe();
    if (device) *device = nullptr;   // no client, no core: the real SDK leaves it untouched
    return kInternalError;
}
__declspec(dllexport) void __cdecl DiscordVersion(void*) {}

// The legacy Rich Presence C API (discord-rpc), the flavour Slime Rancher's
// Assembly-CSharp P/Invokes.
__declspec(dllexport) void __cdecl Discord_Initialize(const char*, void*, int, const char*) {
    lastPipeError = connectDiscordPipe();   // returns without an error callback, as on desktop
}
__declspec(dllexport) void __cdecl Discord_Shutdown() {}
__declspec(dllexport) void __cdecl Discord_RunCallbacks() {}
__declspec(dllexport) void __cdecl Discord_UpdatePresence(const void*) {}
__declspec(dllexport) void __cdecl Discord_ClearPresence() {}
__declspec(dllexport) void __cdecl Discord_Respond(const char*, int) {}
__declspec(dllexport) void __cdecl Discord_UpdateHandlers(void*) {}

// Test-only: what the last connect saw, so the guest can assert Play answered
// with the error the real SDKs handle rather than something unexpected.
__declspec(dllexport) DWORD __cdecl PlayDiscordLastPipeError() { return lastPipeError; }
__declspec(dllexport) const char* __cdecl PlayDiscordSdkName() { return sdkName; }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, void*) { return TRUE; }
