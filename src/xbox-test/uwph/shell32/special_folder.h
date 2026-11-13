// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// shell32 SHGetSpecialFolderPathW for guests. Each folder CSIDL is answered by the
// shared chain in folder_chain.h: the system known folder first (the call the guest's
// known-folder route and cloud_roots already use, issue #171), then the environment or
// package-folder fallback for that folder. fCreate becomes KF_FLAG_CREATE for the known
// folder and a CreateDirectory for a fallback path. CSIDL_DESKTOP (a virtual root with no
// file path) and unknown ids fail with FALSE and an empty buffer, as does a folder no
// step can answer.
// Constants mirror shlobj.h / knownfolders.h (the UWP family hides them); the desktop test
// checks every value against the SDK symbols.
#include <windows.h>
#include <cwchar>
#include "folder_chain.h"
namespace shell_folder {
constexpr int csidl_personal=0x0005;         // CSIDL_PERSONAL (= CSIDL_MYDOCUMENTS)
constexpr int csidl_appdata=0x001a;          // CSIDL_APPDATA
constexpr int csidl_local_appdata=0x001c;    // CSIDL_LOCAL_APPDATA
constexpr int csidl_desktopdirectory=0x0010; // CSIDL_DESKTOPDIRECTORY
constexpr int csidl_common_appdata=0x0023;   // CSIDL_COMMON_APPDATA
constexpr int csidl_desktop=0x0000;          // CSIDL_DESKTOP: virtual shell root, no file path
constexpr DWORD kf_flag_create=0x00008000;   // KF_FLAG_CREATE (= CSIDL_FLAG_CREATE)
struct Mapping { int csidl; GUID folder; const folder_chain::Chain* chain; };
inline const Mapping* mappings(size_t& count){
    // GUIDs from knownfolders.h: Documents, RoamingAppData, LocalAppData, Desktop, ProgramData.
    static const Mapping rows[]={
        {csidl_personal,{0xFDD39AD0,0x238F,0x46AF,{0xAD,0xB4,0x6C,0x85,0x48,0x03,0x69,0xC7}},&folder_chain::documentsChain()},
        {csidl_appdata,{0x3EB685DB,0x65F9,0x4CF6,{0xA0,0x3A,0xE3,0xEF,0x65,0x72,0x9F,0x3D}},&folder_chain::roamingChain()},
        {csidl_local_appdata,{0xF1B32785,0x6FBA,0x4FCF,{0x9D,0x55,0x7B,0x8E,0x7F,0x15,0x70,0x91}},&folder_chain::localChain()},
        {csidl_desktopdirectory,{0xB4BFCC3A,0xDB2C,0x424C,{0xB0,0x29,0x7F,0xE9,0x9A,0x87,0xC6,0x41}},&folder_chain::desktopChain()},
        {csidl_common_appdata,{0x62AB5D82,0xFDC1,0x4DC3,{0xA9,0xDD,0x07,0x0D,0x1D,0x49,0x5D,0x97}},&folder_chain::programDataChain()},
    };
    count=sizeof rows/sizeof rows[0];
    return rows;
}
// The answer for one CSIDL, from the given providers. BOOL like the SDK function.
inline BOOL getSpecialFolderPathWith(const folder_chain::Providers& providers,LPWSTR path,int csidl,BOOL create){
    if(!path) return FALSE;
    path[0]=0;
    if(csidl&0x8000) create=TRUE;   // CSIDL_FLAG_CREATE as the caller may pass it
    const int folder=csidl&0x00ff;
    size_t count=0;
    const Mapping* rows=mappings(count);
    const Mapping* row=nullptr;
    for(size_t i=0;i<count;++i) if(rows[i].csidl==folder) row=&rows[i];
    if(!row) return FALSE;
    const folder_chain::Found found=folder_chain::resolve(providers,row->folder,create?kf_flag_create:0,*row->chain);
    if(found.path.empty()||found.path.size()>=MAX_PATH) return FALSE;
    if(!found.known&&create&&!CreateDirectoryW(found.path.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS) return FALSE;
    wcscpy(path,found.path.c_str());
    return TRUE;
}
// The SDK-shaped entry point, with the system known folder and the process environment.
// The app's route (play_runtime.h) uses the same chain with its package hook installed.
inline BOOL WINAPI getSpecialFolderPathW(HWND,LPWSTR path,int csidl,BOOL create){
    using KnownFolderPath=HRESULT(WINAPI*)(const GUID&,DWORD,HANDLE,PWSTR*);
    static HMODULE shell=LoadLibraryExW(L"shell32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    FARPROC proc=shell?GetProcAddress(shell,"SHGetKnownFolderPath"):nullptr;
    KnownFolderPath known=proc?reinterpret_cast<KnownFolderPath>(reinterpret_cast<void*>(proc)):nullptr;
    return getSpecialFolderPathWith(folder_chain::systemProviders(known),path,csidl,create);
}
}
