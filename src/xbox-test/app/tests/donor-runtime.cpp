// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device donor lifetime regression; does not load or execute a game.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <vector>
static BOOL WINAPI hostProtect(void* p,SIZE_T n,DWORD flags,DWORD* old) {
    using Fn=BOOL(WINAPI*)(void*,SIZE_T,DWORD,DWORD*);
    return ((Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"VirtualProtect"))(p,n,flags,old);
}
static HMODULE WINAPI hostPackaged(LPCWSTR name,DWORD) {
    using Fn=HMODULE(WINAPI*)(LPCWSTR);
    return ((Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"))(name);
}
#define VirtualProtectFromApp hostProtect
#define LoadPackagedLibrary hostPackaged
#include "../host/wxl_mapper.h"
#include "../host/mlp_probe.h"
#include "../donors/probe.h"
static void logline(const char* line) {puts(line);fflush(stdout);}
static DWORD WINAPI probeThread(void*) {return donor_pe::probe(logline)?0:1;}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int) {
    HANDLE thread=CreateThread(nullptr,0,probeThread,nullptr,0,nullptr);
    if(!thread || WaitForSingleObject(thread,30000)!=WAIT_OBJECT_0)return 1;
    DWORD code=1;
    if(!GetExitCodeThread(thread,&code) || code)return 2;
    CloseHandle(thread);
    auto address=donor_pe::exported("hid.dll","HidD_GetHidGuid",0);
    if(!address)return 3;
    GUID guid{};
    ((void(WINAPI*)(GUID*))address)(&guid);
    const GUID expected={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0,0x11,0x11,0,0,0x30}};
    if(memcmp(&guid,&expected,sizeof guid))return 4;
    // A later probe must not unload code still held by a retained guest.
    if(!donor_pe::probe(logline) || address!=donor_pe::exported("hid.dll","HidD_GetHidGuid",0))return 5;
    puts("DONOR-RUNTIME PASS: behavior gate, cross-thread publication, retained addresses; no game code");
    return 0;
}
