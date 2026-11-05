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
// DPI-aware variants (Windows 10 1607+). The one display is 96 DPI, so the
// per-DPI answers equal the classic ones; other DPIs scale the classic
// nonclient geometry like Windows does for per-monitor-aware callers.
extern "C" UINT WINAPI c_GetDpiForSystem(){return 96;}
extern "C" UINT WINAPI c_GetDpiForWindow(HWND hwnd){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(!window(hwnd)){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return 0;}
 return 96;
}
// Only screen-level metrics exist here, and Windows does not DPI-scale those.
extern "C" int WINAPI c_GetSystemMetricsForDpi(int index,UINT dpi){
 if(!dpi){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
 return c_GetSystemMetrics(index);
}
// Windows accepts only these three actions here; there is no font/icon
// metrics backend for them in this record module.
extern "C" BOOL WINAPI c_SystemParametersInfoForDpi(UINT action,UINT,void*,UINT,UINT){
 if(action==SPI_GETICONTITLELOGFONT||action==SPI_GETICONMETRICS||action==SPI_GETNONCLIENTMETRICS){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
 SetLastError(ERROR_INVALID_PARAMETER);return FALSE;
}
extern "C" BOOL WINAPI c_AdjustWindowRectExForDpi(RECT* r,DWORD style,BOOL menu,DWORD ex,UINT dpi){
 if(!r){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 RECT at96=*r;if(!c_AdjustWindowRectEx(&at96,style,menu,ex))return FALSE;
 r->left-=MulDiv(r->left-at96.left,int(dpi),96);r->top-=MulDiv(r->top-at96.top,int(dpi),96);
 r->right+=MulDiv(at96.right-r->right,int(dpi),96);r->bottom+=MulDiv(at96.bottom-r->bottom,int(dpi),96);
 return TRUE;
}
// No nonclient area is drawn for record windows; scaling it is a no-op.
extern "C" BOOL WINAPI c_EnableNonClientDpiScaling(HWND hwnd){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(!window(hwnd)){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}
 return TRUE;
}
// ---- Pointer/touch input: the console exposes no pointer devices to the
// guest, so no pointer id is ever valid (Windows: ERROR_INVALID_PARAMETER).
extern "C" BOOL WINAPI c_GetPointerType(UINT32,void* type){SetLastError(ERROR_INVALID_PARAMETER);(void)type;return FALSE;}
extern "C" BOOL WINAPI c_GetPointerInfoById(UINT32,void*){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
extern "C" BOOL WINAPI c_GetPointerHistoryById(UINT32,UINT32*,void*){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
extern "C" BOOL WINAPI c_GetPointerDevices(UINT32* count,void*){
 if(!count){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 *count=0;return TRUE;
}
// Registration succeeds for a valid window; no WM_TOUCH can ever arrive.
extern "C" BOOL WINAPI c_RegisterTouchWindow(HWND hwnd,ULONG){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(!window(hwnd)){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}
 return TRUE;
}
extern "C" BOOL WINAPI c_UnregisterTouchWindow(HWND hwnd){return c_RegisterTouchWindow(hwnd,0);}
