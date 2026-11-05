#pragma once
// Wine 10 user32 DPI internals that shcore (donor) delay-imports. The record
// core has one virtual display at USER_DEFAULT_SCREEN_DPI (96), so unaware,
// system and per-monitor DPI coincide. Mirrors win32u NtUserGetDpiForMonitor
// and NtUserSet/GetProcessDpiAwarenessContext argument and error handling.
static LONG processDpiAwareness=-1; // unset until the process chooses one
extern "C" BOOL WINAPI c_GetDpiForMonitorInternal(HMONITOR,UINT type,UINT* x,UINT* y) {
 if(type>2){SetLastError(ERROR_BAD_ARGUMENTS);return FALSE;}
 if(!x||!y){SetLastError(ERROR_INVALID_ADDRESS);return FALSE;}
 *x=*y=96;return TRUE;
}
extern "C" BOOL WINAPI c_SetProcessDpiAwarenessInternal(int awareness) {
 if(awareness<0||awareness>2){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 // The first choice wins for the process lifetime, as on Windows and Wine.
 if(InterlockedCompareExchange(&processDpiAwareness,awareness,-1)!=-1){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 return TRUE;
}
extern "C" BOOL WINAPI c_GetProcessDpiAwarenessInternal(HANDLE process,int* awareness) {
 if(!awareness){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 LONG value=(process&&process!=GetCurrentProcess())?-1:InterlockedCompareExchange(&processDpiAwareness,-1,-1);
 *awareness=value<0?0:int(value); // DPI_AWARENESS_UNAWARE when unset or another process
 return TRUE;
}
