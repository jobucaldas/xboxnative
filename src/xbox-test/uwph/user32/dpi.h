#pragma once
// Wine 10 user32 DPI internals that shcore (donor) delay-imports, plus the
// DPI_AWARENESS_CONTEXT family SDL3/Unity probe at runtime. The record core has
// one virtual display at USER_DEFAULT_SCREEN_DPI (96), so unaware, system and
// per-monitor DPI coincide. Mirrors win32u NtUserGetDpiForMonitor and
// NtUserSet/GetProcessDpiAwarenessContext argument and error handling, and
// user32's get_ntuser_dpi_context/is_valid_dpi_awareness_context bit layout.
// The APP family this DLL compiles under hides the DPI_AWARENESS types and the
// context constants in windef.h; mirror the documented values locally (this
// module defines them, it never calls the real ones). The values come from
// dpi_context.h so the encoding rules and the public constants cannot drift.
#include "dpi_context.h"
#ifndef _DPI_AWARENESS_CONTEXTS_
#define _DPI_AWARENESS_CONTEXTS_
typedef HANDLE DPI_AWARENESS_CONTEXT;
typedef enum DPI_AWARENESS {
 DPI_AWARENESS_INVALID=-1,DPI_AWARENESS_UNAWARE=0,DPI_AWARENESS_SYSTEM_AWARE=1,DPI_AWARENESS_PER_MONITOR_AWARE=2
} DPI_AWARENESS;
#define DPI_AWARENESS_CONTEXT_UNAWARE ((DPI_AWARENESS_CONTEXT)(LONG_PTR)u32c_dpi::context_unaware)
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE ((DPI_AWARENESS_CONTEXT)(LONG_PTR)u32c_dpi::context_system_aware)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((DPI_AWARENESS_CONTEXT)(LONG_PTR)u32c_dpi::context_per_monitor_aware)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)(LONG_PTR)u32c_dpi::context_per_monitor_aware_v2)
#define DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED ((DPI_AWARENESS_CONTEXT)(LONG_PTR)u32c_dpi::context_unaware_gdiscaled)
#endif
static_assert(DPI_AWARENESS_INVALID==int(u32c_dpi::awareness_invalid)&&DPI_AWARENESS_UNAWARE==int(u32c_dpi::awareness_unaware)
 &&DPI_AWARENESS_SYSTEM_AWARE==int(u32c_dpi::awareness_system_aware)&&DPI_AWARENESS_PER_MONITOR_AWARE==int(u32c_dpi::awareness_per_monitor_aware),"DPI_AWARENESS values");
static const UINT systemDpi=u32c_dpi::default_screen_dpi;
// NTUSER_DPI_CONTEXT_* spellings, so the entry points below read like Wine's.
#define NTUSER_DPI_UNAWARE u32c_dpi::unaware
#define NTUSER_DPI_SYSTEM_AWARE u32c_dpi::system_aware
#define NTUSER_DPI_CONTEXT_GET_AWARENESS(ctx) u32c_dpi::get_awareness(ctx)
#define NTUSER_DPI_CONTEXT_GET_DPI(ctx) u32c_dpi::get_dpi(ctx)
#define NTUSER_DPI_CONTEXT_GET_FLAGS(ctx) u32c_dpi::get_flags(ctx)
#define NTUSER_DPI_CONTEXT_FLAG_PROCESS u32c_dpi::flag_process
static LONG processDpiContext=0; // full NTUSER_DPI_CONTEXT_*, unset until the process chooses one
static thread_local UINT threadDpiContext=0; // 0: follow the process context, as win32u's thread info
static UINT dpiContextValue(DPI_AWARENESS_CONTEXT context) {
 return u32c_dpi::value((unsigned long long)(UINT_PTR)context);
}
static bool validDpiContext(UINT context,UINT dpi) {
 return u32c_dpi::valid(context,dpi);
}
// NtUserGetProcessDpiAwarenessContext: unaware when unset or for another process.
static UINT processDpiContextValue(HANDLE process) {
 if(process&&process!=GetCurrentProcess())return NTUSER_DPI_UNAWARE;
 LONG value=InterlockedCompareExchange(&processDpiContext,0,0);
 return value?UINT(value):UINT(NTUSER_DPI_UNAWARE);
}
extern "C" BOOL WINAPI c_GetDpiForMonitorInternal(HMONITOR,UINT type,UINT* x,UINT* y) {
 if(type>2){SetLastError(ERROR_BAD_ARGUMENTS);return FALSE;}
 if(!x||!y){SetLastError(ERROR_INVALID_ADDRESS);return FALSE;}
 *x=*y=96;return TRUE;
}
// NtUserSetProcessDpiAwarenessContext: the first choice wins for the process
// lifetime, as on Windows and Wine.
extern "C" BOOL WINAPI c_SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT context) {
 UINT value=dpiContextValue(context);
 if(!validDpiContext(value,systemDpi)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 if(InterlockedCompareExchange(&processDpiContext,LONG(value),0)){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 return TRUE;
}
extern "C" BOOL WINAPI c_SetProcessDpiAwarenessInternal(int awareness) {
 static const DPI_AWARENESS_CONTEXT contexts[3]={DPI_AWARENESS_CONTEXT_UNAWARE,DPI_AWARENESS_CONTEXT_SYSTEM_AWARE,DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE};
 if(awareness<0||awareness>2){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 return c_SetProcessDpiAwarenessContext(contexts[awareness]);
}
extern "C" BOOL WINAPI c_GetProcessDpiAwarenessInternal(HANDLE process,int* awareness) {
 if(!awareness){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 *awareness=int(NTUSER_DPI_CONTEXT_GET_AWARENESS(processDpiContextValue(process)));
 return TRUE;
}
// set_thread_dpi_awareness_context: a context carrying FLAG_PROCESS clears the
// thread override; the returned previous value carries FLAG_PROCESS when the
// thread was following the process.
static UINT setThreadDpiContext(UINT context) {
 if(!validDpiContext(context,systemDpi)){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
 UINT previous=threadDpiContext;
 if(!previous)previous=processDpiContextValue(GetCurrentProcess())|NTUSER_DPI_CONTEXT_FLAG_PROCESS;
 threadDpiContext=(NTUSER_DPI_CONTEXT_GET_FLAGS(context)&NTUSER_DPI_CONTEXT_FLAG_PROCESS)?0:context;
 return previous;
}
extern "C" DPI_AWARENESS_CONTEXT WINAPI c_GetThreadDpiAwarenessContext() {
 return (DPI_AWARENESS_CONTEXT)(UINT_PTR)(threadDpiContext?threadDpiContext:processDpiContextValue(GetCurrentProcess()));
}
extern "C" DPI_AWARENESS_CONTEXT WINAPI c_SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT context) {
 return (DPI_AWARENESS_CONTEXT)(UINT_PTR)setThreadDpiContext(dpiContextValue(context));
}
extern "C" BOOL WINAPI c_AreDpiAwarenessContextsEqual(DPI_AWARENESS_CONTEXT a,DPI_AWARENESS_CONTEXT b) {
 UINT value1=dpiContextValue(a),value2=dpiContextValue(b);
 if(!value1||!value2)return FALSE;
 return (value1&~NTUSER_DPI_CONTEXT_FLAG_PROCESS)==(value2&~NTUSER_DPI_CONTEXT_FLAG_PROCESS);
}
extern "C" DPI_AWARENESS WINAPI c_GetAwarenessFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT context) {
 UINT value=dpiContextValue(context);
 if(!validDpiContext(value,0))return DPI_AWARENESS_INVALID;
 return DPI_AWARENESS(NTUSER_DPI_CONTEXT_GET_AWARENESS(value));
}
extern "C" UINT WINAPI c_GetDpiFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT context) {
 return NTUSER_DPI_CONTEXT_GET_DPI(dpiContextValue(context));
}
extern "C" BOOL WINAPI c_IsValidDpiAwarenessContext(DPI_AWARENESS_CONTEXT context) {
 return validDpiContext(dpiContextValue(context),0);
}
extern "C" BOOL WINAPI c_IsProcessDPIAware() {
 return c_GetAwarenessFromDpiAwarenessContext(c_GetThreadDpiAwarenessContext())!=DPI_AWARENESS_UNAWARE;
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
extern "C" BOOL WINAPI c_GetPointerDeviceRects(HANDLE,RECT*,RECT*){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
// Registration succeeds for a valid window; no WM_TOUCH can ever arrive, so
// the touch-input handle APIs answer as Wine's unimplemented stubs do.
extern "C" BOOL WINAPI c_RegisterTouchWindow(HWND hwnd,ULONG){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(!window(hwnd)){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}
 return TRUE;
}
extern "C" BOOL WINAPI c_UnregisterTouchWindow(HWND hwnd){return c_RegisterTouchWindow(hwnd,0);}
extern "C" BOOL WINAPI c_GetTouchInputInfo(HANDLE,UINT,void*,int){SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}
extern "C" BOOL WINAPI c_CloseTouchInputHandle(HANDLE){SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}
