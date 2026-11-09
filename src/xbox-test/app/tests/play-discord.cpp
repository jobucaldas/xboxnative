// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that uses a Discord SDK the way a game does, so
// Play's Discord behaviour can be observed off-device. It loads the stand-in
// play-discord-sdk.dll (never Discord's own binary), calls the Game SDK's
// DiscordCreate and the legacy Rich Presence entry points, and fails if either
// blocks or returns something other than what the real SDKs return with no
// Discord client running. Exit code 42 = every call returned the documented
// no-client answer; the log line Play must emit is checked by play-runtime.sh.
#include <windows.h>
extern "C" {
int discord_anchor;
void* discord_relocation = &discord_anchor;
DWORD discord_entry() {
    HMODULE sdk = LoadLibraryW(L"play-discord-sdk.dll");
    if (!sdk) return 10;
    using Create = int(__cdecl*)(int, void*, void**);
    using Init = void(__cdecl*)(const char*, void*, int, const char*);
    using LastError = DWORD(__cdecl*)();
    auto create = (Create)GetProcAddress(sdk, "DiscordCreate");
    auto init = (Init)GetProcAddress(sdk, "Discord_Initialize");
    auto lastError = (LastError)GetProcAddress(sdk, "PlayDiscordLastPipeError");
    auto version = GetProcAddress(sdk, "DiscordVersion");
    auto run = GetProcAddress(sdk, "Discord_RunCallbacks");
    auto presence = GetProcAddress(sdk, "Discord_UpdatePresence");
    auto shutdown = GetProcAddress(sdk, "Discord_Shutdown");
    if (!create || !init || !lastError || !version || !run || !presence || !shutdown) return 11;
    // Game SDK: no client means DiscordResult_InternalError and no device.
    void* device = (void*)1;
    if (create(1, nullptr, &device) != 4 || device != nullptr) return 12;
    if (lastError() != ERROR_FILE_NOT_FOUND) return 13;   // the error the SDK handles
    // Legacy Rich Presence: initialize, run callbacks, publish, shut down.
    init("123456789012345678", nullptr, 0, nullptr);
    if (lastError() != ERROR_FILE_NOT_FOUND) return 14;
    ((void(__cdecl*)())run)();
    ((void(__cdecl*)(const void*))presence)(nullptr);
    ((void(__cdecl*)())shutdown)();
    return 42;
}
}
