// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device probe for Valve's genuine client. It stops at the first account
// store/login gate; it does not persist, print, or transmit credentials.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

__declspec(dllexport) int __cdecl GetBootstrapperVersion(void) { return 1; }
__declspec(dllexport) int __cdecl SteamBootstrapper_GetEUniverse(void) { return 1; }
__declspec(dllexport) const char* __cdecl SteamBootstrapper_GetLoggingDir(void) { return "."; }

typedef int (__cdecl *CreateGlobalUser)(int*);
typedef int (__cdecl *CreateSteamPipe)(void);
typedef unsigned (__cdecl *ConnectToGlobalUser)(int);
typedef int (__cdecl *ReleaseSteamPipe)(int);

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        puts("usage: steam_host_probe.exe <verified-client-directory>");
        return 2;
    }
    wchar_t path[MAX_PATH * 4];
    const wchar_t* required[] = {L"steamclient64.dll", L"tier0_s64.dll", L"vstdlib_s64.dll", L"SDL3.dll", L"video64.dll"};
    for (unsigned i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        if (swprintf(path, sizeof(path) / sizeof(path[0]), L"%ls\\%ls", argv[1], required[i]) < 0 ||
            GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            puts("BLOCKED: required Valve runtime DLL is absent");
            return 3;
        }
    }
    if (!SetDllDirectoryW(argv[1])) {
        puts("BLOCKED: could not set the client dependency directory");
        return 4;
    }
    if (swprintf(path, sizeof(path) / sizeof(path[0]), L"%ls\\steamclient64.dll", argv[1]) < 0) {
        puts("BLOCKED: client path is too long");
        return 4;
    }
    HMODULE client = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!client) {
        printf("BLOCKED: LoadLibraryExW failed with Win32 error %lu\n", GetLastError());
        return 5;
    }
    FARPROC proc = GetProcAddress(client, "Steam_CreateGlobalUser");
    CreateGlobalUser create_global_user = NULL;
    memcpy(&create_global_user, &proc, sizeof(create_global_user));
    proc = GetProcAddress(client, "Steam_CreateSteamPipe");
    CreateSteamPipe create_pipe = NULL;
    memcpy(&create_pipe, &proc, sizeof(create_pipe));
    proc = GetProcAddress(client, "Steam_ConnectToGlobalUser");
    ConnectToGlobalUser connect_user = NULL;
    memcpy(&connect_user, &proc, sizeof(connect_user));
    proc = GetProcAddress(client, "Steam_BReleaseSteamPipe");
    ReleaseSteamPipe release_pipe = NULL;
    memcpy(&release_pipe, &proc, sizeof(release_pipe));
    if (!create_global_user || !create_pipe || !connect_user) {
        puts("BLOCKED: client engine entry point is absent");
        FreeLibrary(client);
        return 6;
    }
    int user = 0;
    if (!create_global_user(&user) || !user) {
        puts("BLOCKED: Steam_CreateGlobalUser refused");
        FreeLibrary(client);
        return 7;
    }
    int pipe = create_pipe();
    if (!pipe) {
        printf("BLOCKED: Steam_CreateSteamPipe refused (Win32 error %lu)\n", GetLastError());
        FreeLibrary(client);
        return 8;
    }
    unsigned connected = 0;
    for (int attempt = 0; attempt < 12 && !connected; ++attempt) {
        connected = connect_user(pipe);
        if (!connected && attempt < 11) Sleep(2000);
    }
    if (!connected) {
        if (release_pipe) release_pipe(pipe);
        puts("BLOCKED: Steam_ConnectToGlobalUser returned zero after 12 attempts; next gate is the client's authenticated account store");
        FreeLibrary(client);
        return 10;
    }
    puts("CONNECTED: genuine client returned a user handle; ownership and Steamworks callbacks remain unverified");
    if (release_pipe) release_pipe(pipe);
    FreeLibrary(client);
    return 0;
}
