// SPDX-License-Identifier: GPL-3.0-or-later
// Owned, CRT-free executable: exercise real donor IAT calls, not a game.
#include <windows.h>
#include <setupapi.h>
#include <shellapi.h>
#include <avrt.h>
extern "C" __declspec(dllimport) void WINAPI HidD_GetHidGuid(GUID*);
#ifdef DONOR_MISSING
extern "C" __declspec(dllimport) void WINAPI DeliberatelyAbsentDonorExport();
#endif
// Keep a genuine absolute-VA relocation; the production mapper requires a
// relocatable image even when the executable's ordinary code is RIP-relative.
extern "C" {
int donor_relocation_anchor;
void* donor_relocation=&donor_relocation_anchor;
}
static BYTE versionBytes[1024*1024];
extern "C" DWORD donor_guest_entry() {
#ifdef DONOR_MISSING
    DeliberatelyAbsentDonorExport(); // Must be rejected before entry.
#endif
    GUID guid{};HidD_GetHidGuid(&guid);
    const GUID expected={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0,0x11,0x11,0,0,0x30}};
    for(unsigned i=0;i<sizeof guid;++i)if(((BYTE*)&guid)[i]!=((const BYTE*)&expected)[i])return 1;
    HANDLE set=SetupDiCreateDeviceInfoList(nullptr,nullptr);
    if(set==INVALID_HANDLE_VALUE||!SetupDiDestroyDeviceInfoList(set))return 2;
    int count=0;auto argv=CommandLineToArgvW(L"\"alpha beta\" tail",&count);
    if(!argv)return 3;
    bool parsed=count==2;
    const wchar_t* words[]={L"alpha beta",L"tail"};
    if(parsed)for(unsigned i=0;i<2;++i)for(unsigned j=0;;++j) {
        if(argv[i][j]!=words[i][j]){parsed=false;break;}
        if(!words[i][j])break;
    }
    LocalFree(argv);if(!parsed)return 3;
    DWORD index=0;SetLastError(0);
    if(AvSetMmThreadCharacteristicsW(nullptr,&index)||GetLastError()!=1550)return 4;
    static wchar_t path[2048];DWORD n=GetModuleFileNameW(nullptr,path,2048);
    if(!n||n>=2048)return 5;
    while(n&&path[n-1]!=L'\\'&&path[n-1]!=L'/')--n;
    const wchar_t tail[]=L"donors\\version.dll";
    if(!n||n+sizeof tail/sizeof *tail>2048)return 5;
    for(unsigned i=0;i<sizeof tail/sizeof *tail;++i)path[n+i]=tail[i];
    DWORD ignored=0,size=GetFileVersionInfoSizeW(path,&ignored);
    if(!size||size>sizeof versionBytes||!GetFileVersionInfoW(path,0,size,versionBytes))return 6;
    DWORD* fixed=nullptr;UINT length=0;
    if(!VerQueryValueW(versionBytes,L"\\",(void**)&fixed,&length)||!fixed||
       length<13*sizeof(DWORD)||fixed[0]!=0xfeef04bd)return 7;
    return 42;
}
