// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// shell32 SHGetSpecialFolderPathW for guests, answered the way the guest's known-folder
// route answers. launch.h resolves the game's folders with the system shell32
// SHGetKnownFolderPath inside the packaged process (K32T system route, issue #171), and
// cloud_roots relies on it. This maps CSIDL -> KNOWNFOLDERID and asks that same function,
// with KF_FLAG_CREATE for fCreate (the same bit as CSIDL_FLAG_CREATE). No LocalState path
// is invented. CSIDLs outside the table (including virtual ones with no file path, such as
// CSIDL_DESKTOP) fail with FALSE and an empty buffer.
// Constants mirror shlobj.h / knownfolders.h (the UWP family hides them); the desktop test
// checks every value against the SDK symbols.
#include <windows.h>
#include <cwchar>
#include <objbase.h>
namespace shell_folder {
constexpr int csidl_personal=0x0005;        // CSIDL_PERSONAL (= CSIDL_MYDOCUMENTS)
constexpr int csidl_appdata=0x001a;         // CSIDL_APPDATA
constexpr int csidl_local_appdata=0x001c;   // CSIDL_LOCAL_APPDATA
constexpr int csidl_desktopdirectory=0x0010; // CSIDL_DESKTOPDIRECTORY
constexpr int csidl_common_appdata=0x0023;  // CSIDL_COMMON_APPDATA
constexpr int csidl_desktop=0x0000;         // CSIDL_DESKTOP: virtual shell root, no file path
constexpr DWORD kf_flag_create=0x00008000;  // KF_FLAG_CREATE (= CSIDL_FLAG_CREATE)
struct Mapping { int csidl; GUID folder; };
inline const Mapping* mappings(size_t& count){
 // GUIDs from knownfolders.h: RoamingAppData, LocalAppData, Documents, Desktop, ProgramData.
 static const Mapping rows[]={
  {csidl_personal,{0xFDD39AD0,0x238F,0x46AF,{0xAD,0xB4,0x6C,0x85,0x48,0x03,0x69,0xC7}}},
  {csidl_appdata,{0x3EB685DB,0x65F9,0x4CF6,{0xA0,0x3A,0xE3,0xEF,0x65,0x72,0x9F,0x3D}}},
  {csidl_local_appdata,{0xF1B32785,0x6FBA,0x4FCF,{0x9D,0x55,0x7B,0x8E,0x7F,0x15,0x70,0x91}}},
  {csidl_desktopdirectory,{0xB4BFCC3A,0xDB2C,0x424C,{0xB0,0x29,0x7F,0xE9,0x9A,0x87,0xC6,0x41}}},
  {csidl_common_appdata,{0x62AB5D82,0xFDC1,0x4DC3,{0xA9,0xDD,0x07,0x0D,0x1D,0x49,0x5D,0x97}}},
 };
 count=sizeof rows/sizeof rows[0];
 return rows;
}
// The system known-folder route the guest's folder calls already use (launch.h).
using KnownFolderPath=HRESULT(WINAPI*)(const GUID&,DWORD,HANDLE,PWSTR*);
inline KnownFolderPath systemKnownFolderPath(){
 static HMODULE shell=LoadLibraryExW(L"shell32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
 FARPROC proc=shell?GetProcAddress(shell,"SHGetKnownFolderPath"):nullptr;
 return proc?reinterpret_cast<KnownFolderPath>(reinterpret_cast<void*>(proc)):nullptr;
}
inline BOOL WINAPI getSpecialFolderPathW(HWND,LPWSTR path,int csidl,BOOL create){
 if(!path) return FALSE;
 path[0]=0;
 if(create) csidl|=0x8000;   // CSIDL_FLAG_CREATE as the caller may pass it
 const int folder=csidl&0x00ff;
 size_t count=0;
 const Mapping* rows=mappings(count);
 const GUID* id=nullptr;
 for(size_t i=0;i<count;++i) if(rows[i].csidl==folder) id=&rows[i].folder;
 KnownFolderPath known=systemKnownFolderPath();
 if(!id||!known) return FALSE;
 PWSTR text=nullptr;
 if(FAILED(known(*id,(csidl&0x8000)?kf_flag_create:0,nullptr,&text))||!text){ if(text) CoTaskMemFree(text); return FALSE; }
 BOOL ok=wcslen(text)<MAX_PATH;   // the caller's buffer is MAX_PATH wide
 if(ok) wcscpy(path,text);
 CoTaskMemFree(text);
 if(!ok) path[0]=0;
 return ok;
}
}
