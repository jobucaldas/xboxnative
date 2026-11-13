// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// shell32 SHGetSpecialFolderPathW for guests. The UWP guest has no folder provider:
// no per-user Documents or AppData location is mapped for it, so no path is claimed.
// The caller's buffer is emptied and FALSE returned, which is what Windows reports
// when a folder cannot be located. Saves that need a folder require a real redirect
// (not provided here). shell32 program launches are refused in app/host/play_runtime.h.
#include <windows.h>
namespace shell_folder {
inline BOOL WINAPI getSpecialFolderPathW(HWND,LPWSTR path,int csidl,BOOL create){
 (void)csidl;(void)create;
 if(path) path[0]=0;
 return FALSE;
}
}
