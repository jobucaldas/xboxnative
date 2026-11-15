// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device probe for Valve's genuine client. It stops at the first account
// store/login gate; it does not persist, print, or transmit credentials.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../steam_token_store.h"

__declspec(dllexport) int __cdecl GetBootstrapperVersion(void) { return 1; }
__declspec(dllexport) int __cdecl SteamBootstrapper_GetEUniverse(void) { return 1; }
__declspec(dllexport) const char* __cdecl SteamBootstrapper_GetLoggingDir(void) { return "."; }

typedef int (__cdecl *CreateGlobalUser)(int*);
typedef int (__cdecl *CreateSteamPipe)(void);
typedef unsigned (__cdecl *ConnectToGlobalUser)(int);
typedef int (__cdecl *ReleaseSteamPipe)(int);

static bool ascii_arg(const wchar_t* value, char* out, size_t capacity) {
    size_t n=0;
    for (const wchar_t* p=value; *p; ++p) {
        if (*p < 0x20 || *p > 0x7e || n+1>=capacity) return false;
        out[n++]=char(*p);
    }
    if(!n) return false;
    out[n]=0;
    return true;
}
static bool write_token_config(const wchar_t* token_path, const wchar_t* login_arg, const wchar_t* steam_id_arg) {
    char login[256], steam_id[64], token[8193]={}, config[32768];
    if (!ascii_arg(login_arg,login,sizeof login) || !ascii_arg(steam_id_arg,steam_id,sizeof steam_id)) return false;
    HANDLE file=CreateFileW(token_path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size)||size.QuadPart<1||size.QuadPart>8192){CloseHandle(file);return false;}
    DWORD read=0;
    bool ok=ReadFile(file,token,DWORD(size.QuadPart),&read,nullptr)&&read==size.QuadPart;
    CloseHandle(file);
    size_t token_size=(size_t)read;
    while(token_size&&(token[token_size-1]=='\n'||token[token_size-1]=='\r')) token[--token_size]=0;
    for(size_t i=0;i<token_size;++i) if(!token[i]) ok=false;
    size_t config_size=ok?steam_token_store_config_vdf(token,login,steam_id,1500000000ULL,config,sizeof config):0;
    SecureZeroMemory(token,sizeof token);
    if(!config_size||(!CreateDirectoryW(L"config",nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)) return false;
    HANDLE output=CreateFileW(L"config\\config.vdf",GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(output==INVALID_HANDLE_VALUE) return false;
    DWORD written=0;
    ok=WriteFile(output,config,(DWORD)config_size,&written,nullptr)&&written==config_size&&FlushFileBuffers(output);
    CloseHandle(output);
    SecureZeroMemory(config,sizeof config);
    if(!ok) DeleteFileW(L"config\\config.vdf");
    return ok;
}
struct ConfigCleanup {
    bool created=false;
    ~ConfigCleanup() { if(created){DeleteFileW(L"config\\config.vdf");RemoveDirectoryW(L"config");} }
};

int wmain(int argc, wchar_t** argv) {
    const bool token_requested=argc==8&&wcscmp(argv[2],L"--token-file")==0&&wcscmp(argv[4],L"--login")==0&&wcscmp(argv[6],L"--steamid")==0;
    if(!token_requested&&argc!=2) {
        puts("usage: steam_host_probe.exe <verified-client-directory> [--token-file <path> --login <name> --steamid <id>]");
        return 2;
    }
    if(!SetCurrentDirectoryW(argv[1])) { puts("BLOCKED: could not set the verified client working directory"); return 3; }
    bool have_token=false;
    if(token_requested) {
        if(!write_token_config(argv[3],argv[5],argv[7])) { puts("BLOCKED: could not prepare the local refresh-token config"); return 2; }
        have_token=true;
    }
    ConfigCleanup config_cleanup{have_token};
    CreateDirectoryW(L"logs",nullptr);
    wchar_t path[MAX_PATH * 4];
    const wchar_t* required[] = {L"steamclient64.dll", L"tier0_s64.dll", L"vstdlib_s64.dll", L"SDL3.dll", L"video64.dll",
        L"libavcodec-62.dll", L"libavfilter-11.dll", L"libavformat-62.dll", L"libavutil-60.dll",
        L"libswresample-6.dll", L"libswscale-9.dll", L"bin\\gldriverquery64.exe"};
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
    if (!have_token) {
        puts("NOT LOGGED ON: client loaded; no token supplied, user/pipe APIs were not called");
        return 0;
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
    if (!create_global_user || !create_pipe || !connect_user || !release_pipe) {
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
    if (!connected || !have_token) {
        puts("NOT LOGGED ON: a global-user handle alone does not establish an authenticated account");
    } else {
        puts("HANDLE ATTACHED: client returned a global-user handle; authentication state remains unverified");
    }
    release_pipe(pipe);
    FreeLibrary(client);
    return 0;
}
