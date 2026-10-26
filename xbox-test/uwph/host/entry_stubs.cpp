// Named x64 ABI stubs. Prototypes transcribed from mingw-w64 Windows headers.
// Every unsupported operation returns its API-specific failure/empty value.
#include <windows.h>
#include <mmsystem.h>
#include <imm.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <type_traits>
// Timing requests go to the documented system multimedia timer entry points.
// No claimed timer-resolution change when this partition lacks those exports.
static INIT_ONCE timer_once=INIT_ONCE_STATIC_INIT;
static HMODULE timer_module;
static BOOL CALLBACK init_timer(PINIT_ONCE,PVOID,PVOID*) {
 timer_module=LoadLibraryExW(L"winmm.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32); return TRUE;
}
static MMRESULT timer_period(bool begin,UINT period) {
 InitOnceExecuteOnce(&timer_once,init_timer,nullptr,nullptr);
 HMODULE module=timer_module;
 using Fn=MMRESULT (WINAPI*)(UINT);
 auto fn=module ? (Fn)GetProcAddress(module,begin?"timeBeginPeriod":"timeEndPeriod") : nullptr;
 return fn ? fn(period) : TIMERR_NOCANDO;
}
static void (*logger)(const char*);
extern "C" __declspec(dllexport) void entry_stubs_set_log(void (*fn)(const char*)) { logger=fn; }
static void note(const char* name,LONG* seen) { if(InterlockedCompareExchange(seen,1,0)==0 && logger) logger(name); }

static WINBOOL WINAPI s_AdjustWindowRectEx(LPRECT a0, DWORD a1, WINBOOL a2, DWORD a3) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!AdjustWindowRectEx",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_AdjustWindowRectEx),decltype(&AdjustWindowRectEx)>::value,"AdjustWindowRectEx signature");
static WINBOOL WINAPI s_AllowSetForegroundWindow(DWORD a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!AllowSetForegroundWindow",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_AllowSetForegroundWindow),decltype(&AllowSetForegroundWindow)>::value,"AllowSetForegroundWindow signature");
static WINBOOL WINAPI s_ClientToScreen(HWND a0, LPPOINT a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!ClientToScreen",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_ClientToScreen),decltype(&ClientToScreen)>::value,"ClientToScreen signature");
static WINBOOL WINAPI s_ClipCursor(const RECT * a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!ClipCursor",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_ClipCursor),decltype(&ClipCursor)>::value,"ClipCursor signature");
static WINBOOL WINAPI s_CloseClipboard(void) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!CloseClipboard",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_CloseClipboard),decltype(&CloseClipboard)>::value,"CloseClipboard signature");
static WINBOOL WINAPI s_CopyRect(LPRECT a0, const RECT * a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!CopyRect",&seen); if(!a0||!a1) return FALSE; *a0=*a1; return (WINBOOL)(TRUE); }
static_assert(std::is_same<decltype(&s_CopyRect),decltype(&CopyRect)>::value,"CopyRect signature");
static HWND WINAPI s_CreateWindowExW(DWORD a0, LPCWSTR a1, LPCWSTR a2, DWORD a3, int a4, int a5, int a6, int a7, HWND a8, HMENU a9, HINSTANCE a10, LPVOID a11) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!CreateWindowExW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HWND)(0); }
static_assert(std::is_same<decltype(&s_CreateWindowExW),decltype(&CreateWindowExW)>::value,"CreateWindowExW signature");
static LRESULT WINAPI s_DefWindowProcW(HWND a0, UINT a1, WPARAM a2, LPARAM a3) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!DefWindowProcW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (LRESULT)(0); }
static_assert(std::is_same<decltype(&s_DefWindowProcW),decltype(&DefWindowProcW)>::value,"DefWindowProcW signature");
static WINBOOL WINAPI s_DestroyCursor(HCURSOR a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!DestroyCursor",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_DestroyCursor),decltype(&DestroyCursor)>::value,"DestroyCursor signature");
static WINBOOL WINAPI s_DestroyIcon(HICON a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!DestroyIcon",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_DestroyIcon),decltype(&DestroyIcon)>::value,"DestroyIcon signature");
static WINBOOL WINAPI s_DestroyWindow(HWND a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!DestroyWindow",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_DestroyWindow),decltype(&DestroyWindow)>::value,"DestroyWindow signature");
static INT_PTR WINAPI s_DialogBoxParamW(HINSTANCE a0, LPCWSTR a1, HWND a2, DLGPROC a3, LPARAM a4) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!DialogBoxParamW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (INT_PTR)(-1); }
static_assert(std::is_same<decltype(&s_DialogBoxParamW),decltype(&DialogBoxParamW)>::value,"DialogBoxParamW signature");
static LRESULT WINAPI s_DispatchMessageA(const MSG * a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!DispatchMessageA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (LRESULT)(0); }
static_assert(std::is_same<decltype(&s_DispatchMessageA),decltype(&DispatchMessageA)>::value,"DispatchMessageA signature");
static LONG WINAPI s_DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_HEADER * a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!DisplayConfigGetDeviceInfo",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (LONG)(ERROR_NOT_SUPPORTED); }
static_assert(std::is_same<decltype(&s_DisplayConfigGetDeviceInfo),decltype(&DisplayConfigGetDeviceInfo)>::value,"DisplayConfigGetDeviceInfo signature");
static WINBOOL WINAPI s_DragDetect(HWND a0, POINT a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!DragDetect",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_DragDetect),decltype(&DragDetect)>::value,"DragDetect signature");
static WINBOOL WINAPI s_EmptyClipboard(void) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!EmptyClipboard",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_EmptyClipboard),decltype(&EmptyClipboard)>::value,"EmptyClipboard signature");
static WINBOOL WINAPI s_EndDialog(HWND a0, INT_PTR a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!EndDialog",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_EndDialog),decltype(&EndDialog)>::value,"EndDialog signature");
static WINBOOL WINAPI s_EnumDisplayDevicesA(LPCSTR a0, DWORD a1, PDISPLAY_DEVICEA a2, DWORD a3) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!EnumDisplayDevicesA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_EnumDisplayDevicesA),decltype(&EnumDisplayDevicesA)>::value,"EnumDisplayDevicesA signature");
static WINBOOL WINAPI s_EnumDisplayMonitors(HDC a0, LPCRECT a1, MONITORENUMPROC a2, LPARAM a3) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!EnumDisplayMonitors",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_EnumDisplayMonitors),decltype(&EnumDisplayMonitors)>::value,"EnumDisplayMonitors signature");
static WINBOOL WINAPI s_EnumDisplaySettingsA(LPCSTR a0, DWORD a1, LPDEVMODEA a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!EnumDisplaySettingsA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_EnumDisplaySettingsA),decltype(&EnumDisplaySettingsA)>::value,"EnumDisplaySettingsA signature");
static WINBOOL WINAPI s_EnumDisplaySettingsW(LPCWSTR a0, DWORD a1, LPDEVMODEW a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!EnumDisplaySettingsW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_EnumDisplaySettingsW),decltype(&EnumDisplaySettingsW)>::value,"EnumDisplaySettingsW signature");
static WINBOOL WINAPI s_EnumWindows(WNDENUMPROC a0, LPARAM a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!EnumWindows",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_EnumWindows),decltype(&EnumWindows)>::value,"EnumWindows signature");
static HWND WINAPI s_GetActiveWindow(void) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetActiveWindow",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HWND)(0); }
static_assert(std::is_same<decltype(&s_GetActiveWindow),decltype(&GetActiveWindow)>::value,"GetActiveWindow signature");
static SHORT WINAPI s_GetAsyncKeyState(int a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetAsyncKeyState",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (SHORT)(0); }
static_assert(std::is_same<decltype(&s_GetAsyncKeyState),decltype(&GetAsyncKeyState)>::value,"GetAsyncKeyState signature");
static UINT WINAPI s_GetCaretBlinkTime(void) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetCaretBlinkTime",&seen);  return (UINT)(INFINITE); }
static_assert(std::is_same<decltype(&s_GetCaretBlinkTime),decltype(&GetCaretBlinkTime)>::value,"GetCaretBlinkTime signature");
static WINBOOL WINAPI s_GetClientRect(HWND a0, LPRECT a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetClientRect",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_GetClientRect),decltype(&GetClientRect)>::value,"GetClientRect signature");
static HANDLE WINAPI s_GetClipboardData(UINT a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetClipboardData",&seen); SetLastError(ERROR_CLIPBOARD_NOT_OPEN); return (HANDLE)(0); }
static_assert(std::is_same<decltype(&s_GetClipboardData),decltype(&GetClipboardData)>::value,"GetClipboardData signature");
static WINBOOL WINAPI s_GetCursorPos(LPPOINT a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetCursorPos",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a0) *a0={0,0}; return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_GetCursorPos),decltype(&GetCursorPos)>::value,"GetCursorPos signature");
static HDC WINAPI s_GetDC(HWND a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetDC",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HDC)(0); }
static_assert(std::is_same<decltype(&s_GetDC),decltype(&GetDC)>::value,"GetDC signature");
static HWND WINAPI s_GetDesktopWindow(void) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetDesktopWindow",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HWND)(0); }
static_assert(std::is_same<decltype(&s_GetDesktopWindow),decltype(&GetDesktopWindow)>::value,"GetDesktopWindow signature");
static LONG WINAPI s_GetDisplayConfigBufferSizes(UINT32 a0, UINT32 * a1, UINT32 * a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetDisplayConfigBufferSizes",&seen); if(a1) *a1=0; if(a2) *a2=0; return (LONG)(ERROR_NOT_SUPPORTED); }
static_assert(std::is_same<decltype(&s_GetDisplayConfigBufferSizes),decltype(&GetDisplayConfigBufferSizes)>::value,"GetDisplayConfigBufferSizes signature");
static UINT WINAPI s_GetDoubleClickTime(void) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetDoubleClickTime",&seen);  return (UINT)(500); }
static_assert(std::is_same<decltype(&s_GetDoubleClickTime),decltype(&GetDoubleClickTime)>::value,"GetDoubleClickTime signature");
static HWND WINAPI s_GetFocus(void) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetFocus",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HWND)(0); }
static_assert(std::is_same<decltype(&s_GetFocus),decltype(&GetFocus)>::value,"GetFocus signature");
static HWND WINAPI s_GetForegroundWindow(void) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetForegroundWindow",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HWND)(0); }
static_assert(std::is_same<decltype(&s_GetForegroundWindow),decltype(&GetForegroundWindow)>::value,"GetForegroundWindow signature");
static int WINAPI s_GetKeyNameTextW(LONG a0, LPWSTR a1, int a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetKeyNameTextW",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a1 && a2>0) *a1=0; return (int)(0); }
static_assert(std::is_same<decltype(&s_GetKeyNameTextW),decltype(&GetKeyNameTextW)>::value,"GetKeyNameTextW signature");
static SHORT WINAPI s_GetKeyState(int a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetKeyState",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (SHORT)(0); }
static_assert(std::is_same<decltype(&s_GetKeyState),decltype(&GetKeyState)>::value,"GetKeyState signature");
static HKL WINAPI s_GetKeyboardLayout(DWORD a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetKeyboardLayout",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HKL)(0); }
static_assert(std::is_same<decltype(&s_GetKeyboardLayout),decltype(&GetKeyboardLayout)>::value,"GetKeyboardLayout signature");
static WINBOOL WINAPI s_GetKeyboardLayoutNameW(LPWSTR a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetKeyboardLayoutNameW",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a0) *a0=0; return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_GetKeyboardLayoutNameW),decltype(&GetKeyboardLayoutNameW)>::value,"GetKeyboardLayoutNameW signature");
static WINBOOL WINAPI s_GetMessageA(LPMSG a0, HWND a1, UINT a2, UINT a3) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetMessageA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(-1); }
static_assert(std::is_same<decltype(&s_GetMessageA),decltype(&GetMessageA)>::value,"GetMessageA signature");
static LPARAM WINAPI s_GetMessageExtraInfo(void) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetMessageExtraInfo",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (LPARAM)(0); }
static_assert(std::is_same<decltype(&s_GetMessageExtraInfo),decltype(&GetMessageExtraInfo)>::value,"GetMessageExtraInfo signature");
static WINBOOL WINAPI s_GetMonitorInfoA(HMONITOR a0, LPMONITORINFO a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetMonitorInfoA",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a0==(HMONITOR)(UINT_PTR)1 && a1 && a1->cbSize>=sizeof(MONITORINFO)) { a1->rcMonitor={0,0,1920,1080}; a1->rcWork=a1->rcMonitor; a1->dwFlags=MONITORINFOF_PRIMARY; if(a1->cbSize>=sizeof(MONITORINFOEXA)) { auto p=(MONITORINFOEXA*)a1; for(unsigned i=0;i<32;++i) p->szDevice[i]=0; const char name[]="\\\\.\\DISPLAY1"; for(unsigned i=0;i<sizeof(name)/sizeof(*name);++i) p->szDevice[i]=name[i]; } return TRUE; } return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_GetMonitorInfoA),decltype(&GetMonitorInfoA)>::value,"GetMonitorInfoA signature");
static WINBOOL WINAPI s_GetMonitorInfoW(HMONITOR a0, LPMONITORINFO a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetMonitorInfoW",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a0==(HMONITOR)(UINT_PTR)1 && a1 && a1->cbSize>=sizeof(MONITORINFO)) { a1->rcMonitor={0,0,1920,1080}; a1->rcWork=a1->rcMonitor; a1->dwFlags=MONITORINFOF_PRIMARY; if(a1->cbSize>=sizeof(MONITORINFOEXW)) { auto p=(MONITORINFOEXW*)a1; for(unsigned i=0;i<32;++i) p->szDevice[i]=0; const wchar_t name[]=L"\\\\.\\DISPLAY1"; for(unsigned i=0;i<sizeof(name)/sizeof(*name);++i) p->szDevice[i]=name[i]; } return TRUE; } return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_GetMonitorInfoW),decltype(&GetMonitorInfoW)>::value,"GetMonitorInfoW signature");
static HWND WINAPI s_GetParent(HWND a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetParent",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HWND)(0); }
static_assert(std::is_same<decltype(&s_GetParent),decltype(&GetParent)>::value,"GetParent signature");
static UINT WINAPI s_GetRawInputBuffer(PRAWINPUT a0, PUINT a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetRawInputBuffer",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (UINT)((UINT)-1); }
static_assert(std::is_same<decltype(&s_GetRawInputBuffer),decltype(&GetRawInputBuffer)>::value,"GetRawInputBuffer signature");
static UINT WINAPI s_GetRawInputData(HRAWINPUT a0, UINT a1, LPVOID a2, PUINT a3, UINT a4) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetRawInputData",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (UINT)((UINT)-1); }
static_assert(std::is_same<decltype(&s_GetRawInputData),decltype(&GetRawInputData)>::value,"GetRawInputData signature");
static UINT WINAPI s_GetRawInputDeviceInfoW(HANDLE a0, UINT a1, LPVOID a2, PUINT a3) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetRawInputDeviceInfoW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (UINT)((UINT)-1); }
static_assert(std::is_same<decltype(&s_GetRawInputDeviceInfoW),decltype(&GetRawInputDeviceInfoW)>::value,"GetRawInputDeviceInfoW signature");
static UINT WINAPI s_GetRawInputDeviceList(PRAWINPUTDEVICELIST a0, PUINT a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetRawInputDeviceList",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(!a1 || a2!=sizeof(RAWINPUTDEVICELIST)) {SetLastError(ERROR_INVALID_PARAMETER);return (UINT)-1;} *a1=0; return 0; }
static_assert(std::is_same<decltype(&s_GetRawInputDeviceList),decltype(&GetRawInputDeviceList)>::value,"GetRawInputDeviceList signature");
static int WINAPI s_GetSystemMetrics(int a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetSystemMetrics",&seen);  return (int)((a0==SM_CXSCREEN || a0==SM_CXVIRTUALSCREEN) ? 1920 : (a0==SM_CYSCREEN || a0==SM_CYVIRTUALSCREEN) ? 1080 : a0==SM_CMONITORS ? 1 : 0); }
static_assert(std::is_same<decltype(&s_GetSystemMetrics),decltype(&GetSystemMetrics)>::value,"GetSystemMetrics signature");
static HDESK WINAPI s_GetThreadDesktop(DWORD a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetThreadDesktop",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HDESK)(0); }
static_assert(std::is_same<decltype(&s_GetThreadDesktop),decltype(&GetThreadDesktop)>::value,"GetThreadDesktop signature");
static WINBOOL WINAPI s_GetUserObjectInformationA(HANDLE a0, int a1, PVOID a2, DWORD a3, LPDWORD a4) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetUserObjectInformationA",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a4) *a4=0; return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_GetUserObjectInformationA),decltype(&GetUserObjectInformationA)>::value,"GetUserObjectInformationA signature");
static LONG WINAPI s_GetWindowLongA(HWND a0, int a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetWindowLongA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (LONG)(0); }
static_assert(std::is_same<decltype(&s_GetWindowLongA),decltype(&GetWindowLongA)>::value,"GetWindowLongA signature");
static LONG_PTR WINAPI s_GetWindowLongPtrW(HWND a0, int a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetWindowLongPtrW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (LONG_PTR)(0); }
static_assert(std::is_same<decltype(&s_GetWindowLongPtrW),decltype(&GetWindowLongPtrW)>::value,"GetWindowLongPtrW signature");
static WINBOOL WINAPI s_GetWindowPlacement(HWND a0, WINDOWPLACEMENT * a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetWindowPlacement",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a1 && a1->length==sizeof(*a1)) { UINT n=a1->length; *a1={}; a1->length=n; } return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_GetWindowPlacement),decltype(&GetWindowPlacement)>::value,"GetWindowPlacement signature");
static WINBOOL WINAPI s_GetWindowRect(HWND a0, LPRECT a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!GetWindowRect",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_GetWindowRect),decltype(&GetWindowRect)>::value,"GetWindowRect signature");
static WINBOOL WINAPI s_IsClipboardFormatAvailable(UINT a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!IsClipboardFormatAvailable",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_IsClipboardFormatAvailable),decltype(&IsClipboardFormatAvailable)>::value,"IsClipboardFormatAvailable signature");
static WINBOOL WINAPI s_IsIconic(HWND a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!IsIconic",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_IsIconic),decltype(&IsIconic)>::value,"IsIconic signature");
static WINBOOL WINAPI s_IsWindowVisible(HWND a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!IsWindowVisible",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_IsWindowVisible),decltype(&IsWindowVisible)>::value,"IsWindowVisible signature");
static WINBOOL WINAPI s_KillTimer(HWND a0, UINT_PTR a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!KillTimer",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_KillTimer),decltype(&KillTimer)>::value,"KillTimer signature");
static HCURSOR WINAPI s_LoadCursorA(HINSTANCE a0, LPCSTR a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!LoadCursorA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HCURSOR)(0); }
static_assert(std::is_same<decltype(&s_LoadCursorA),decltype(&LoadCursorA)>::value,"LoadCursorA signature");
static HICON WINAPI s_LoadIconA(HINSTANCE a0, LPCSTR a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!LoadIconA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HICON)(0); }
static_assert(std::is_same<decltype(&s_LoadIconA),decltype(&LoadIconA)>::value,"LoadIconA signature");
static HICON WINAPI s_LoadIconW(HINSTANCE a0, LPCWSTR a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!LoadIconW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HICON)(0); }
static_assert(std::is_same<decltype(&s_LoadIconW),decltype(&LoadIconW)>::value,"LoadIconW signature");
static HANDLE WINAPI s_LoadImageW(HINSTANCE a0, LPCWSTR a1, UINT a2, int a3, int a4, UINT a5) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!LoadImageW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HANDLE)(0); }
static_assert(std::is_same<decltype(&s_LoadImageW),decltype(&LoadImageW)>::value,"LoadImageW signature");
static UINT WINAPI s_MapVirtualKeyA(UINT a0, UINT a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!MapVirtualKeyA",&seen); return (UINT)(0); }
static_assert(std::is_same<decltype(&s_MapVirtualKeyA),decltype(&MapVirtualKeyA)>::value,"MapVirtualKeyA signature");
static UINT WINAPI s_MapVirtualKeyExA(UINT a0, UINT a1, HKL a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!MapVirtualKeyExA",&seen); return (UINT)(0); }
static_assert(std::is_same<decltype(&s_MapVirtualKeyExA),decltype(&MapVirtualKeyExA)>::value,"MapVirtualKeyExA signature");
static UINT WINAPI s_MapVirtualKeyW(UINT a0, UINT a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!MapVirtualKeyW",&seen); return (UINT)(0); }
static_assert(std::is_same<decltype(&s_MapVirtualKeyW),decltype(&MapVirtualKeyW)>::value,"MapVirtualKeyW signature");
static int WINAPI s_MessageBoxA(HWND a0, LPCSTR a1, LPCSTR a2, UINT a3) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!MessageBoxA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (int)(0); }
static_assert(std::is_same<decltype(&s_MessageBoxA),decltype(&MessageBoxA)>::value,"MessageBoxA signature");
static HMONITOR WINAPI s_MonitorFromRect(LPCRECT a0, DWORD a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!MonitorFromRect",&seen);  return (HMONITOR)((HMONITOR)(UINT_PTR)1); }
static_assert(std::is_same<decltype(&s_MonitorFromRect),decltype(&MonitorFromRect)>::value,"MonitorFromRect signature");
static HMONITOR WINAPI s_MonitorFromWindow(HWND a0, DWORD a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!MonitorFromWindow",&seen);  return (HMONITOR)((HMONITOR)(UINT_PTR)1); }
static_assert(std::is_same<decltype(&s_MonitorFromWindow),decltype(&MonitorFromWindow)>::value,"MonitorFromWindow signature");
static WINBOOL WINAPI s_MoveWindow(HWND a0, int a1, int a2, int a3, int a4, WINBOOL a5) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!MoveWindow",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_MoveWindow),decltype(&MoveWindow)>::value,"MoveWindow signature");
static DWORD WINAPI s_MsgWaitForMultipleObjects(DWORD a0, const HANDLE * a1, WINBOOL a2, DWORD a3, DWORD a4) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!MsgWaitForMultipleObjects",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (DWORD)(0); }
static_assert(std::is_same<decltype(&s_MsgWaitForMultipleObjects),decltype(&MsgWaitForMultipleObjects)>::value,"MsgWaitForMultipleObjects signature");
static WINBOOL WINAPI s_OffsetRect(LPRECT a0, int a1, int a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!OffsetRect",&seen); if(!a0) return FALSE; a0->left+=a1; a0->right+=a1; a0->top+=a2; a0->bottom+=a2; return (WINBOOL)(TRUE); }
static_assert(std::is_same<decltype(&s_OffsetRect),decltype(&OffsetRect)>::value,"OffsetRect signature");
static WINBOOL WINAPI s_OpenClipboard(HWND a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!OpenClipboard",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_OpenClipboard),decltype(&OpenClipboard)>::value,"OpenClipboard signature");
static WINBOOL WINAPI s_PeekMessageA(LPMSG a0, HWND a1, UINT a2, UINT a3, UINT a4) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!PeekMessageA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_PeekMessageA),decltype(&PeekMessageA)>::value,"PeekMessageA signature");
static void WINAPI s_PostQuitMessage(int a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!PostQuitMessage",&seen); SetLastError(ERROR_NOT_SUPPORTED); }
static_assert(std::is_same<decltype(&s_PostQuitMessage),decltype(&PostQuitMessage)>::value,"PostQuitMessage signature");
static WINBOOL WINAPI s_PtInRect(const RECT * a0, POINT a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!PtInRect",&seen);  return (WINBOOL)(a0 && a1.x>=a0->left && a1.x<a0->right && a1.y>=a0->top && a1.y<a0->bottom); }
static_assert(std::is_same<decltype(&s_PtInRect),decltype(&PtInRect)>::value,"PtInRect signature");
static LONG WINAPI s_QueryDisplayConfig(UINT32 a0, UINT32 * a1, DISPLAYCONFIG_PATH_INFO * a2, UINT32 * a3, DISPLAYCONFIG_MODE_INFO * a4, DISPLAYCONFIG_TOPOLOGY_ID * a5) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!QueryDisplayConfig",&seen); if(a1) *a1=0; if(a3) *a3=0; return (LONG)(ERROR_NOT_SUPPORTED); }
static_assert(std::is_same<decltype(&s_QueryDisplayConfig),decltype(&QueryDisplayConfig)>::value,"QueryDisplayConfig signature");
static ATOM WINAPI s_RegisterClassExW(const WNDCLASSEXW * a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!RegisterClassExW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (ATOM)(0); }
static_assert(std::is_same<decltype(&s_RegisterClassExW),decltype(&RegisterClassExW)>::value,"RegisterClassExW signature");
static ATOM WINAPI s_RegisterClassW(const WNDCLASSW * a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!RegisterClassW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (ATOM)(0); }
static_assert(std::is_same<decltype(&s_RegisterClassW),decltype(&RegisterClassW)>::value,"RegisterClassW signature");
static HDEVNOTIFY WINAPI s_RegisterDeviceNotificationW(HANDLE a0, LPVOID a1, DWORD a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!RegisterDeviceNotificationW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HDEVNOTIFY)(0); }
static_assert(std::is_same<decltype(&s_RegisterDeviceNotificationW),decltype(&RegisterDeviceNotificationW)>::value,"RegisterDeviceNotificationW signature");
static WINBOOL WINAPI s_RegisterRawInputDevices(PCRAWINPUTDEVICE a0, UINT a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!RegisterRawInputDevices",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_RegisterRawInputDevices),decltype(&RegisterRawInputDevices)>::value,"RegisterRawInputDevices signature");
static UINT WINAPI s_RegisterWindowMessageA(LPCSTR a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!RegisterWindowMessageA",&seen); return (UINT)(0); }
static_assert(std::is_same<decltype(&s_RegisterWindowMessageA),decltype(&RegisterWindowMessageA)>::value,"RegisterWindowMessageA signature");
static WINBOOL WINAPI s_ReleaseCapture(void) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!ReleaseCapture",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_ReleaseCapture),decltype(&ReleaseCapture)>::value,"ReleaseCapture signature");
static int WINAPI s_ReleaseDC(HWND a0, HDC a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!ReleaseDC",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (int)(0); }
static_assert(std::is_same<decltype(&s_ReleaseDC),decltype(&ReleaseDC)>::value,"ReleaseDC signature");
static WINBOOL WINAPI s_ScreenToClient(HWND a0, LPPOINT a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!ScreenToClient",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_ScreenToClient),decltype(&ScreenToClient)>::value,"ScreenToClient signature");
static LRESULT WINAPI s_SendDlgItemMessageW(HWND a0, int a1, UINT a2, WPARAM a3, LPARAM a4) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SendDlgItemMessageW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (LRESULT)(0); }
static_assert(std::is_same<decltype(&s_SendDlgItemMessageW),decltype(&SendDlgItemMessageW)>::value,"SendDlgItemMessageW signature");
static LRESULT WINAPI s_SendMessageTimeoutA(HWND a0, UINT a1, WPARAM a2, LPARAM a3, UINT a4, UINT a5, PDWORD_PTR a6) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SendMessageTimeoutA",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a6) *a6=0; return (LRESULT)(0); }
static_assert(std::is_same<decltype(&s_SendMessageTimeoutA),decltype(&SendMessageTimeoutA)>::value,"SendMessageTimeoutA signature");
static LRESULT WINAPI s_SendMessageW(HWND a0, UINT a1, WPARAM a2, LPARAM a3) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SendMessageW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (LRESULT)(0); }
static_assert(std::is_same<decltype(&s_SendMessageW),decltype(&SendMessageW)>::value,"SendMessageW signature");
static HWND WINAPI s_SetCapture(HWND a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetCapture",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HWND)(0); }
static_assert(std::is_same<decltype(&s_SetCapture),decltype(&SetCapture)>::value,"SetCapture signature");
static HANDLE WINAPI s_SetClipboardData(UINT a0, HANDLE a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetClipboardData",&seen); SetLastError(ERROR_CLIPBOARD_NOT_OPEN); return (HANDLE)(0); }
static_assert(std::is_same<decltype(&s_SetClipboardData),decltype(&SetClipboardData)>::value,"SetClipboardData signature");
static HCURSOR WINAPI s_SetCursor(HCURSOR a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetCursor",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HCURSOR)(0); }
static_assert(std::is_same<decltype(&s_SetCursor),decltype(&SetCursor)>::value,"SetCursor signature");
static WINBOOL WINAPI s_SetCursorPos(int a0, int a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetCursorPos",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_SetCursorPos),decltype(&SetCursorPos)>::value,"SetCursorPos signature");
static WINBOOL WINAPI s_SetDlgItemTextA(HWND a0, int a1, LPCSTR a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetDlgItemTextA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_SetDlgItemTextA),decltype(&SetDlgItemTextA)>::value,"SetDlgItemTextA signature");
static WINBOOL WINAPI s_SetDlgItemTextW(HWND a0, int a1, LPCWSTR a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetDlgItemTextW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_SetDlgItemTextW),decltype(&SetDlgItemTextW)>::value,"SetDlgItemTextW signature");
static HWND WINAPI s_SetFocus(HWND a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetFocus",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HWND)(0); }
static_assert(std::is_same<decltype(&s_SetFocus),decltype(&SetFocus)>::value,"SetFocus signature");
static WINBOOL WINAPI s_SetForegroundWindow(HWND a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetForegroundWindow",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_SetForegroundWindow),decltype(&SetForegroundWindow)>::value,"SetForegroundWindow signature");
static UINT_PTR WINAPI s_SetTimer(HWND a0, UINT_PTR a1, UINT a2, TIMERPROC a3) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetTimer",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (UINT_PTR)(0); }
static_assert(std::is_same<decltype(&s_SetTimer),decltype(&SetTimer)>::value,"SetTimer signature");
static LONG WINAPI s_SetWindowLongA(HWND a0, int a1, LONG a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetWindowLongA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (LONG)(0); }
static_assert(std::is_same<decltype(&s_SetWindowLongA),decltype(&SetWindowLongA)>::value,"SetWindowLongA signature");
static LONG_PTR WINAPI s_SetWindowLongPtrA(HWND a0, int a1, LONG_PTR a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetWindowLongPtrA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (LONG_PTR)(0); }
static_assert(std::is_same<decltype(&s_SetWindowLongPtrA),decltype(&SetWindowLongPtrA)>::value,"SetWindowLongPtrA signature");
static LONG_PTR WINAPI s_SetWindowLongPtrW(HWND a0, int a1, LONG_PTR a2) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetWindowLongPtrW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (LONG_PTR)(0); }
static_assert(std::is_same<decltype(&s_SetWindowLongPtrW),decltype(&SetWindowLongPtrW)>::value,"SetWindowLongPtrW signature");
static WINBOOL WINAPI s_SetWindowPos(HWND a0, HWND a1, int a2, int a3, int a4, int a5, UINT a6) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetWindowPos",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_SetWindowPos),decltype(&SetWindowPos)>::value,"SetWindowPos signature");
static WINBOOL WINAPI s_SetWindowTextW(HWND a0, LPCWSTR a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SetWindowTextW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_SetWindowTextW),decltype(&SetWindowTextW)>::value,"SetWindowTextW signature");
static int WINAPI s_ShowCursor(WINBOOL a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!ShowCursor",&seen); static LONG count=0; return (int)(a0 ? InterlockedIncrement(&count) : InterlockedDecrement(&count)); }
static_assert(std::is_same<decltype(&s_ShowCursor),decltype(&ShowCursor)>::value,"ShowCursor signature");
static WINBOOL WINAPI s_ShowWindow(HWND a0, int a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!ShowWindow",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_ShowWindow),decltype(&ShowWindow)>::value,"ShowWindow signature");
static WINBOOL WINAPI s_SystemParametersInfoW(UINT a0, UINT a1, PVOID a2, UINT a3) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!SystemParametersInfoW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_SystemParametersInfoW),decltype(&SystemParametersInfoW)>::value,"SystemParametersInfoW signature");
static int WINAPI s_ToUnicode(UINT a0, UINT a1, const BYTE * a2, LPWSTR a3, int a4, UINT a5) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!ToUnicode",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a3 && a4>0) *a3=0; return (int)(0); }
static_assert(std::is_same<decltype(&s_ToUnicode),decltype(&ToUnicode)>::value,"ToUnicode signature");
static WINBOOL WINAPI s_TrackMouseEvent(LPTRACKMOUSEEVENT a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!TrackMouseEvent",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_TrackMouseEvent),decltype(&TrackMouseEvent)>::value,"TrackMouseEvent signature");
static WINBOOL WINAPI s_TranslateMessage(const MSG * a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!TranslateMessage",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_TranslateMessage),decltype(&TranslateMessage)>::value,"TranslateMessage signature");
static WINBOOL WINAPI s_UnregisterClassW(LPCWSTR a0, HINSTANCE a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!UnregisterClassW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_UnregisterClassW),decltype(&UnregisterClassW)>::value,"UnregisterClassW signature");
static WINBOOL WINAPI s_UnregisterDeviceNotification(HDEVNOTIFY a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!UnregisterDeviceNotification",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_UnregisterDeviceNotification),decltype(&UnregisterDeviceNotification)>::value,"UnregisterDeviceNotification signature");
static WINBOOL WINAPI s_UpdateWindow(HWND a0) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!UpdateWindow",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_UpdateWindow),decltype(&UpdateWindow)>::value,"UpdateWindow signature");
static WINBOOL WINAPI s_ValidateRect(HWND a0, const RECT * a1) { static LONG seen=0; note("ENTRY-STUB CALL user32.dll!ValidateRect",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_ValidateRect),decltype(&ValidateRect)>::value,"ValidateRect signature");
static int WINAPI s_ChoosePixelFormat(HDC a0, const PIXELFORMATDESCRIPTOR * a1) { static LONG seen=0; note("ENTRY-STUB CALL gdi32.dll!ChoosePixelFormat",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (int)(0); }
static_assert(std::is_same<decltype(&s_ChoosePixelFormat),decltype(&ChoosePixelFormat)>::value,"ChoosePixelFormat signature");
static int WINAPI s_GetDeviceCaps(HDC a0, int a1) { static LONG seen=0; note("ENTRY-STUB CALL gdi32.dll!GetDeviceCaps",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (int)(0); }
static_assert(std::is_same<decltype(&s_GetDeviceCaps),decltype(&GetDeviceCaps)>::value,"GetDeviceCaps signature");
static WINBOOL WINAPI s_SetPixelFormat(HDC a0, int a1, const PIXELFORMATDESCRIPTOR * a2) { static LONG seen=0; note("ENTRY-STUB CALL gdi32.dll!SetPixelFormat",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_SetPixelFormat),decltype(&SetPixelFormat)>::value,"SetPixelFormat signature");
static WINBOOL WINAPI s_SwapBuffers(HDC a0) { static LONG seen=0; note("ENTRY-STUB CALL gdi32.dll!SwapBuffers",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_SwapBuffers),decltype(&SwapBuffers)>::value,"SwapBuffers signature");
static HIMC WINAPI s_ImmAssociateContext(HWND a0, HIMC a1) { static LONG seen=0; note("ENTRY-STUB CALL imm32.dll!ImmAssociateContext",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HIMC)(0); }
static_assert(std::is_same<decltype(&s_ImmAssociateContext),decltype(&ImmAssociateContext)>::value,"ImmAssociateContext signature");
static WINBOOL WINAPI s_ImmAssociateContextEx(HWND a0, HIMC a1, DWORD a2) { static LONG seen=0; note("ENTRY-STUB CALL imm32.dll!ImmAssociateContextEx",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_ImmAssociateContextEx),decltype(&ImmAssociateContextEx)>::value,"ImmAssociateContextEx signature");
static LONG WINAPI s_ImmGetCompositionStringW(HIMC a0, DWORD a1, LPVOID a2, DWORD a3) { static LONG seen=0; note("ENTRY-STUB CALL imm32.dll!ImmGetCompositionStringW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (LONG)(IMM_ERROR_NODATA); }
static_assert(std::is_same<decltype(&s_ImmGetCompositionStringW),decltype(&ImmGetCompositionStringW)>::value,"ImmGetCompositionStringW signature");
static HIMC WINAPI s_ImmGetContext(HWND a0) { static LONG seen=0; note("ENTRY-STUB CALL imm32.dll!ImmGetContext",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HIMC)(0); }
static_assert(std::is_same<decltype(&s_ImmGetContext),decltype(&ImmGetContext)>::value,"ImmGetContext signature");
static WINBOOL WINAPI s_ImmGetConversionStatus(HIMC a0, LPDWORD a1, LPDWORD a2) { static LONG seen=0; note("ENTRY-STUB CALL imm32.dll!ImmGetConversionStatus",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a1) *a1=0; if(a2) *a2=0; return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_ImmGetConversionStatus),decltype(&ImmGetConversionStatus)>::value,"ImmGetConversionStatus signature");
static WINBOOL WINAPI s_ImmNotifyIME(HIMC a0, DWORD a1, DWORD a2, DWORD a3) { static LONG seen=0; note("ENTRY-STUB CALL imm32.dll!ImmNotifyIME",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_ImmNotifyIME),decltype(&ImmNotifyIME)>::value,"ImmNotifyIME signature");
static WINBOOL WINAPI s_ImmReleaseContext(HWND a0, HIMC a1) { static LONG seen=0; note("ENTRY-STUB CALL imm32.dll!ImmReleaseContext",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_ImmReleaseContext),decltype(&ImmReleaseContext)>::value,"ImmReleaseContext signature");
static WINBOOL WINAPI s_ImmSetCompositionStringW(HIMC a0, DWORD a1, LPVOID a2, DWORD a3, LPVOID a4, DWORD a5) { static LONG seen=0; note("ENTRY-STUB CALL imm32.dll!ImmSetCompositionStringW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_ImmSetCompositionStringW),decltype(&ImmSetCompositionStringW)>::value,"ImmSetCompositionStringW signature");
static HGLRC WINAPI s_wglCreateContext(HDC a0) { static LONG seen=0; note("ENTRY-STUB CALL opengl32.dll!wglCreateContext",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HGLRC)(0); }
static_assert(std::is_same<decltype(&s_wglCreateContext),decltype(&wglCreateContext)>::value,"wglCreateContext signature");
static WINBOOL WINAPI s_wglDeleteContext(HGLRC a0) { static LONG seen=0; note("ENTRY-STUB CALL opengl32.dll!wglDeleteContext",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_wglDeleteContext),decltype(&wglDeleteContext)>::value,"wglDeleteContext signature");
static HGLRC WINAPI s_wglGetCurrentContext(void) { static LONG seen=0; note("ENTRY-STUB CALL opengl32.dll!wglGetCurrentContext",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HGLRC)(0); }
static_assert(std::is_same<decltype(&s_wglGetCurrentContext),decltype(&wglGetCurrentContext)>::value,"wglGetCurrentContext signature");
static HDC WINAPI s_wglGetCurrentDC(void) { static LONG seen=0; note("ENTRY-STUB CALL opengl32.dll!wglGetCurrentDC",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HDC)(0); }
static_assert(std::is_same<decltype(&s_wglGetCurrentDC),decltype(&wglGetCurrentDC)>::value,"wglGetCurrentDC signature");
static PROC WINAPI s_wglGetProcAddress(LPCSTR a0) { static LONG seen=0; note("ENTRY-STUB CALL opengl32.dll!wglGetProcAddress",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (PROC)(0); }
static_assert(std::is_same<decltype(&s_wglGetProcAddress),decltype(&wglGetProcAddress)>::value,"wglGetProcAddress signature");
static WINBOOL WINAPI s_wglMakeCurrent(HDC a0, HGLRC a1) { static LONG seen=0; note("ENTRY-STUB CALL opengl32.dll!wglMakeCurrent",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_wglMakeCurrent),decltype(&wglMakeCurrent)>::value,"wglMakeCurrent signature");
static LPWSTR * WINAPI s_CommandLineToArgvW(LPCWSTR a0, int * a1) { static LONG seen=0; note("ENTRY-STUB CALL shell32.dll!CommandLineToArgvW",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a1) *a1=0; return (LPWSTR *)(0); }
static_assert(std::is_same<decltype(&s_CommandLineToArgvW),decltype(&CommandLineToArgvW)>::value,"CommandLineToArgvW signature");
static int WINAPI s_SHFileOperationW(LPSHFILEOPSTRUCTW a0) { static LONG seen=0; note("ENTRY-STUB CALL shell32.dll!SHFileOperationW",&seen); SetLastError(ERROR_ACCESS_DENIED); if(a0) a0->fAnyOperationsAborted=TRUE; return (int)(ERROR_ACCESS_DENIED); }
static_assert(std::is_same<decltype(&s_SHFileOperationW),decltype(&SHFileOperationW)>::value,"SHFileOperationW signature");
static HRESULT WINAPI s_SHGetFolderPathW(HWND a0, int a1, HANDLE a2, DWORD a3, LPWSTR a4) { static LONG seen=0; note("ENTRY-STUB CALL shell32.dll!SHGetFolderPathW",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a4) *a4=0; return (HRESULT)(E_NOTIMPL); }
static_assert(std::is_same<decltype(&s_SHGetFolderPathW),decltype(&SHGetFolderPathW)>::value,"SHGetFolderPathW signature");
static HRESULT WINAPI s_SHGetKnownFolderPath(REFKNOWNFOLDERID a0, DWORD a1, HANDLE a2, PWSTR * a3) { static LONG seen=0; note("ENTRY-STUB CALL shell32.dll!SHGetKnownFolderPath",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a3) *a3=nullptr; return (HRESULT)(E_NOTIMPL); }
static_assert(std::is_same<decltype(&s_SHGetKnownFolderPath),decltype(&SHGetKnownFolderPath)>::value,"SHGetKnownFolderPath signature");
static HINSTANCE WINAPI s_ShellExecuteW(HWND a0, LPCWSTR a1, LPCWSTR a2, LPCWSTR a3, LPCWSTR a4, INT a5) { static LONG seen=0; note("ENTRY-STUB CALL shell32.dll!ShellExecuteW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (HINSTANCE)((HINSTANCE)(INT_PTR)SE_ERR_ACCESSDENIED); }
static_assert(std::is_same<decltype(&s_ShellExecuteW),decltype(&ShellExecuteW)>::value,"ShellExecuteW signature");
static WINBOOL WINAPI s_PathCanonicalizeW(LPWSTR a0, LPCWSTR a1) { static LONG seen=0; note("ENTRY-STUB CALL shlwapi.dll!PathCanonicalizeW",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a0) *a0=0; return (WINBOOL)(0); }
static_assert(std::is_same<decltype(&s_PathCanonicalizeW),decltype(&PathCanonicalizeW)>::value,"PathCanonicalizeW signature");
static DWORD WINAPI s_SHDeleteKeyW(HKEY a0, LPCWSTR a1) { static LONG seen=0; note("ENTRY-STUB CALL shlwapi.dll!SHDeleteKeyW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (DWORD)(ERROR_ACCESS_DENIED); }
static_assert(std::is_same<decltype(&s_SHDeleteKeyW),decltype(&SHDeleteKeyW)>::value,"SHDeleteKeyW signature");
static MMRESULT WINAPI s_timeBeginPeriod(UINT a0) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!timeBeginPeriod",&seen);  return (MMRESULT)(timer_period(true,a0)); }
static_assert(std::is_same<decltype(&s_timeBeginPeriod),decltype(&timeBeginPeriod)>::value,"timeBeginPeriod signature");
static MMRESULT WINAPI s_timeEndPeriod(UINT a0) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!timeEndPeriod",&seen);  return (MMRESULT)(timer_period(false,a0)); }
static_assert(std::is_same<decltype(&s_timeEndPeriod),decltype(&timeEndPeriod)>::value,"timeEndPeriod signature");
static DWORD WINAPI s_timeGetTime(void) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!timeGetTime",&seen);  return (DWORD)((DWORD)GetTickCount64()); }
static_assert(std::is_same<decltype(&s_timeGetTime),decltype(&timeGetTime)>::value,"timeGetTime signature");
static MMRESULT WINAPI s_waveInAddBuffer(HWAVEIN a0, LPWAVEHDR a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveInAddBuffer",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_INVALHANDLE); }
static_assert(std::is_same<decltype(&s_waveInAddBuffer),decltype(&waveInAddBuffer)>::value,"waveInAddBuffer signature");
static MMRESULT WINAPI s_waveInClose(HWAVEIN a0) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveInClose",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_INVALHANDLE); }
static_assert(std::is_same<decltype(&s_waveInClose),decltype(&waveInClose)>::value,"waveInClose signature");
static MMRESULT WINAPI s_waveInGetDevCapsA(UINT_PTR a0, LPWAVEINCAPSA a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveInGetDevCapsA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_BADDEVICEID); }
static_assert(std::is_same<decltype(&s_waveInGetDevCapsA),decltype(&waveInGetDevCapsA)>::value,"waveInGetDevCapsA signature");
static MMRESULT WINAPI s_waveInGetDevCapsW(UINT_PTR a0, LPWAVEINCAPSW a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveInGetDevCapsW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_BADDEVICEID); }
static_assert(std::is_same<decltype(&s_waveInGetDevCapsW),decltype(&waveInGetDevCapsW)>::value,"waveInGetDevCapsW signature");
static UINT WINAPI s_waveInGetNumDevs(void) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveInGetNumDevs",&seen); return (UINT)(0); }
static_assert(std::is_same<decltype(&s_waveInGetNumDevs),decltype(&waveInGetNumDevs)>::value,"waveInGetNumDevs signature");
static MMRESULT WINAPI s_waveInOpen(LPHWAVEIN a0, UINT a1, LPCWAVEFORMATEX a2, DWORD_PTR a3, DWORD_PTR a4, DWORD a5) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveInOpen",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a0) *a0=nullptr; return (MMRESULT)(MMSYSERR_BADDEVICEID); }
static_assert(std::is_same<decltype(&s_waveInOpen),decltype(&waveInOpen)>::value,"waveInOpen signature");
static MMRESULT WINAPI s_waveInPrepareHeader(HWAVEIN a0, LPWAVEHDR a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveInPrepareHeader",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_INVALHANDLE); }
static_assert(std::is_same<decltype(&s_waveInPrepareHeader),decltype(&waveInPrepareHeader)>::value,"waveInPrepareHeader signature");
static MMRESULT WINAPI s_waveInReset(HWAVEIN a0) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveInReset",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_INVALHANDLE); }
static_assert(std::is_same<decltype(&s_waveInReset),decltype(&waveInReset)>::value,"waveInReset signature");
static MMRESULT WINAPI s_waveInStart(HWAVEIN a0) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveInStart",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_INVALHANDLE); }
static_assert(std::is_same<decltype(&s_waveInStart),decltype(&waveInStart)>::value,"waveInStart signature");
static MMRESULT WINAPI s_waveInUnprepareHeader(HWAVEIN a0, LPWAVEHDR a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveInUnprepareHeader",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_INVALHANDLE); }
static_assert(std::is_same<decltype(&s_waveInUnprepareHeader),decltype(&waveInUnprepareHeader)>::value,"waveInUnprepareHeader signature");
static MMRESULT WINAPI s_waveOutClose(HWAVEOUT a0) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveOutClose",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_INVALHANDLE); }
static_assert(std::is_same<decltype(&s_waveOutClose),decltype(&waveOutClose)>::value,"waveOutClose signature");
static MMRESULT WINAPI s_waveOutGetDevCapsA(UINT_PTR a0, LPWAVEOUTCAPSA a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveOutGetDevCapsA",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_BADDEVICEID); }
static_assert(std::is_same<decltype(&s_waveOutGetDevCapsA),decltype(&waveOutGetDevCapsA)>::value,"waveOutGetDevCapsA signature");
static MMRESULT WINAPI s_waveOutGetDevCapsW(UINT_PTR a0, LPWAVEOUTCAPSW a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveOutGetDevCapsW",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_BADDEVICEID); }
static_assert(std::is_same<decltype(&s_waveOutGetDevCapsW),decltype(&waveOutGetDevCapsW)>::value,"waveOutGetDevCapsW signature");
static UINT WINAPI s_waveOutGetNumDevs(void) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveOutGetNumDevs",&seen); return (UINT)(0); }
static_assert(std::is_same<decltype(&s_waveOutGetNumDevs),decltype(&waveOutGetNumDevs)>::value,"waveOutGetNumDevs signature");
static MMRESULT WINAPI s_waveOutGetPosition(HWAVEOUT a0, LPMMTIME a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveOutGetPosition",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_INVALHANDLE); }
static_assert(std::is_same<decltype(&s_waveOutGetPosition),decltype(&waveOutGetPosition)>::value,"waveOutGetPosition signature");
static MMRESULT WINAPI s_waveOutOpen(LPHWAVEOUT a0, UINT a1, LPCWAVEFORMATEX a2, DWORD_PTR a3, DWORD_PTR a4, DWORD a5) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveOutOpen",&seen); SetLastError(ERROR_NOT_SUPPORTED); if(a0) *a0=nullptr; return (MMRESULT)(MMSYSERR_BADDEVICEID); }
static_assert(std::is_same<decltype(&s_waveOutOpen),decltype(&waveOutOpen)>::value,"waveOutOpen signature");
static MMRESULT WINAPI s_waveOutPrepareHeader(HWAVEOUT a0, LPWAVEHDR a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveOutPrepareHeader",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_INVALHANDLE); }
static_assert(std::is_same<decltype(&s_waveOutPrepareHeader),decltype(&waveOutPrepareHeader)>::value,"waveOutPrepareHeader signature");
static MMRESULT WINAPI s_waveOutReset(HWAVEOUT a0) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveOutReset",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_INVALHANDLE); }
static_assert(std::is_same<decltype(&s_waveOutReset),decltype(&waveOutReset)>::value,"waveOutReset signature");
static MMRESULT WINAPI s_waveOutUnprepareHeader(HWAVEOUT a0, LPWAVEHDR a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveOutUnprepareHeader",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_INVALHANDLE); }
static_assert(std::is_same<decltype(&s_waveOutUnprepareHeader),decltype(&waveOutUnprepareHeader)>::value,"waveOutUnprepareHeader signature");
static MMRESULT WINAPI s_waveOutWrite(HWAVEOUT a0, LPWAVEHDR a1, UINT a2) { static LONG seen=0; note("ENTRY-STUB CALL winmm.dll!waveOutWrite",&seen); SetLastError(ERROR_NOT_SUPPORTED); return (MMRESULT)(MMSYSERR_INVALHANDLE); }
static_assert(std::is_same<decltype(&s_waveOutWrite),decltype(&waveOutWrite)>::value,"waveOutWrite signature");
struct Entry { const char* dll; const char* name; UINT_PTR address; };
static const Entry entries[]={
 {"user32.dll","AdjustWindowRectEx",(UINT_PTR)&s_AdjustWindowRectEx},
 {"user32.dll","AllowSetForegroundWindow",(UINT_PTR)&s_AllowSetForegroundWindow},
 {"user32.dll","ClientToScreen",(UINT_PTR)&s_ClientToScreen},
 {"user32.dll","ClipCursor",(UINT_PTR)&s_ClipCursor},
 {"user32.dll","CloseClipboard",(UINT_PTR)&s_CloseClipboard},
 {"user32.dll","CopyRect",(UINT_PTR)&s_CopyRect},
 {"user32.dll","CreateWindowExW",(UINT_PTR)&s_CreateWindowExW},
 {"user32.dll","DefWindowProcW",(UINT_PTR)&s_DefWindowProcW},
 {"user32.dll","DestroyCursor",(UINT_PTR)&s_DestroyCursor},
 {"user32.dll","DestroyIcon",(UINT_PTR)&s_DestroyIcon},
 {"user32.dll","DestroyWindow",(UINT_PTR)&s_DestroyWindow},
 {"user32.dll","DialogBoxParamW",(UINT_PTR)&s_DialogBoxParamW},
 {"user32.dll","DispatchMessageA",(UINT_PTR)&s_DispatchMessageA},
 {"user32.dll","DisplayConfigGetDeviceInfo",(UINT_PTR)&s_DisplayConfigGetDeviceInfo},
 {"user32.dll","DragDetect",(UINT_PTR)&s_DragDetect},
 {"user32.dll","EmptyClipboard",(UINT_PTR)&s_EmptyClipboard},
 {"user32.dll","EndDialog",(UINT_PTR)&s_EndDialog},
 {"user32.dll","EnumDisplayDevicesA",(UINT_PTR)&s_EnumDisplayDevicesA},
 {"user32.dll","EnumDisplayMonitors",(UINT_PTR)&s_EnumDisplayMonitors},
 {"user32.dll","EnumDisplaySettingsA",(UINT_PTR)&s_EnumDisplaySettingsA},
 {"user32.dll","EnumDisplaySettingsW",(UINT_PTR)&s_EnumDisplaySettingsW},
 {"user32.dll","EnumWindows",(UINT_PTR)&s_EnumWindows},
 {"user32.dll","GetActiveWindow",(UINT_PTR)&s_GetActiveWindow},
 {"user32.dll","GetAsyncKeyState",(UINT_PTR)&s_GetAsyncKeyState},
 {"user32.dll","GetCaretBlinkTime",(UINT_PTR)&s_GetCaretBlinkTime},
 {"user32.dll","GetClientRect",(UINT_PTR)&s_GetClientRect},
 {"user32.dll","GetClipboardData",(UINT_PTR)&s_GetClipboardData},
 {"user32.dll","GetCursorPos",(UINT_PTR)&s_GetCursorPos},
 {"user32.dll","GetDC",(UINT_PTR)&s_GetDC},
 {"user32.dll","GetDesktopWindow",(UINT_PTR)&s_GetDesktopWindow},
 {"user32.dll","GetDisplayConfigBufferSizes",(UINT_PTR)&s_GetDisplayConfigBufferSizes},
 {"user32.dll","GetDoubleClickTime",(UINT_PTR)&s_GetDoubleClickTime},
 {"user32.dll","GetFocus",(UINT_PTR)&s_GetFocus},
 {"user32.dll","GetForegroundWindow",(UINT_PTR)&s_GetForegroundWindow},
 {"user32.dll","GetKeyNameTextW",(UINT_PTR)&s_GetKeyNameTextW},
 {"user32.dll","GetKeyState",(UINT_PTR)&s_GetKeyState},
 {"user32.dll","GetKeyboardLayout",(UINT_PTR)&s_GetKeyboardLayout},
 {"user32.dll","GetKeyboardLayoutNameW",(UINT_PTR)&s_GetKeyboardLayoutNameW},
 {"user32.dll","GetMessageA",(UINT_PTR)&s_GetMessageA},
 {"user32.dll","GetMessageExtraInfo",(UINT_PTR)&s_GetMessageExtraInfo},
 {"user32.dll","GetMonitorInfoA",(UINT_PTR)&s_GetMonitorInfoA},
 {"user32.dll","GetMonitorInfoW",(UINT_PTR)&s_GetMonitorInfoW},
 {"user32.dll","GetParent",(UINT_PTR)&s_GetParent},
 {"user32.dll","GetRawInputBuffer",(UINT_PTR)&s_GetRawInputBuffer},
 {"user32.dll","GetRawInputData",(UINT_PTR)&s_GetRawInputData},
 {"user32.dll","GetRawInputDeviceInfoW",(UINT_PTR)&s_GetRawInputDeviceInfoW},
 {"user32.dll","GetRawInputDeviceList",(UINT_PTR)&s_GetRawInputDeviceList},
 {"user32.dll","GetSystemMetrics",(UINT_PTR)&s_GetSystemMetrics},
 {"user32.dll","GetThreadDesktop",(UINT_PTR)&s_GetThreadDesktop},
 {"user32.dll","GetUserObjectInformationA",(UINT_PTR)&s_GetUserObjectInformationA},
 {"user32.dll","GetWindowLongA",(UINT_PTR)&s_GetWindowLongA},
 {"user32.dll","GetWindowLongPtrW",(UINT_PTR)&s_GetWindowLongPtrW},
 {"user32.dll","GetWindowPlacement",(UINT_PTR)&s_GetWindowPlacement},
 {"user32.dll","GetWindowRect",(UINT_PTR)&s_GetWindowRect},
 {"user32.dll","IsClipboardFormatAvailable",(UINT_PTR)&s_IsClipboardFormatAvailable},
 {"user32.dll","IsIconic",(UINT_PTR)&s_IsIconic},
 {"user32.dll","IsWindowVisible",(UINT_PTR)&s_IsWindowVisible},
 {"user32.dll","KillTimer",(UINT_PTR)&s_KillTimer},
 {"user32.dll","LoadCursorA",(UINT_PTR)&s_LoadCursorA},
 {"user32.dll","LoadIconA",(UINT_PTR)&s_LoadIconA},
 {"user32.dll","LoadIconW",(UINT_PTR)&s_LoadIconW},
 {"user32.dll","LoadImageW",(UINT_PTR)&s_LoadImageW},
 {"user32.dll","MapVirtualKeyA",(UINT_PTR)&s_MapVirtualKeyA},
 {"user32.dll","MapVirtualKeyExA",(UINT_PTR)&s_MapVirtualKeyExA},
 {"user32.dll","MapVirtualKeyW",(UINT_PTR)&s_MapVirtualKeyW},
 {"user32.dll","MessageBoxA",(UINT_PTR)&s_MessageBoxA},
 {"user32.dll","MonitorFromRect",(UINT_PTR)&s_MonitorFromRect},
 {"user32.dll","MonitorFromWindow",(UINT_PTR)&s_MonitorFromWindow},
 {"user32.dll","MoveWindow",(UINT_PTR)&s_MoveWindow},
 {"user32.dll","MsgWaitForMultipleObjects",(UINT_PTR)&s_MsgWaitForMultipleObjects},
 {"user32.dll","OffsetRect",(UINT_PTR)&s_OffsetRect},
 {"user32.dll","OpenClipboard",(UINT_PTR)&s_OpenClipboard},
 {"user32.dll","PeekMessageA",(UINT_PTR)&s_PeekMessageA},
 {"user32.dll","PostQuitMessage",(UINT_PTR)&s_PostQuitMessage},
 {"user32.dll","PtInRect",(UINT_PTR)&s_PtInRect},
 {"user32.dll","QueryDisplayConfig",(UINT_PTR)&s_QueryDisplayConfig},
 {"user32.dll","RegisterClassExW",(UINT_PTR)&s_RegisterClassExW},
 {"user32.dll","RegisterClassW",(UINT_PTR)&s_RegisterClassW},
 {"user32.dll","RegisterDeviceNotificationW",(UINT_PTR)&s_RegisterDeviceNotificationW},
 {"user32.dll","RegisterRawInputDevices",(UINT_PTR)&s_RegisterRawInputDevices},
 {"user32.dll","RegisterWindowMessageA",(UINT_PTR)&s_RegisterWindowMessageA},
 {"user32.dll","ReleaseCapture",(UINT_PTR)&s_ReleaseCapture},
 {"user32.dll","ReleaseDC",(UINT_PTR)&s_ReleaseDC},
 {"user32.dll","ScreenToClient",(UINT_PTR)&s_ScreenToClient},
 {"user32.dll","SendDlgItemMessageW",(UINT_PTR)&s_SendDlgItemMessageW},
 {"user32.dll","SendMessageTimeoutA",(UINT_PTR)&s_SendMessageTimeoutA},
 {"user32.dll","SendMessageW",(UINT_PTR)&s_SendMessageW},
 {"user32.dll","SetCapture",(UINT_PTR)&s_SetCapture},
 {"user32.dll","SetClipboardData",(UINT_PTR)&s_SetClipboardData},
 {"user32.dll","SetCursor",(UINT_PTR)&s_SetCursor},
 {"user32.dll","SetCursorPos",(UINT_PTR)&s_SetCursorPos},
 {"user32.dll","SetDlgItemTextA",(UINT_PTR)&s_SetDlgItemTextA},
 {"user32.dll","SetDlgItemTextW",(UINT_PTR)&s_SetDlgItemTextW},
 {"user32.dll","SetFocus",(UINT_PTR)&s_SetFocus},
 {"user32.dll","SetForegroundWindow",(UINT_PTR)&s_SetForegroundWindow},
 {"user32.dll","SetTimer",(UINT_PTR)&s_SetTimer},
 {"user32.dll","SetWindowLongA",(UINT_PTR)&s_SetWindowLongA},
 {"user32.dll","SetWindowLongPtrA",(UINT_PTR)&s_SetWindowLongPtrA},
 {"user32.dll","SetWindowLongPtrW",(UINT_PTR)&s_SetWindowLongPtrW},
 {"user32.dll","SetWindowPos",(UINT_PTR)&s_SetWindowPos},
 {"user32.dll","SetWindowTextW",(UINT_PTR)&s_SetWindowTextW},
 {"user32.dll","ShowCursor",(UINT_PTR)&s_ShowCursor},
 {"user32.dll","ShowWindow",(UINT_PTR)&s_ShowWindow},
 {"user32.dll","SystemParametersInfoW",(UINT_PTR)&s_SystemParametersInfoW},
 {"user32.dll","ToUnicode",(UINT_PTR)&s_ToUnicode},
 {"user32.dll","TrackMouseEvent",(UINT_PTR)&s_TrackMouseEvent},
 {"user32.dll","TranslateMessage",(UINT_PTR)&s_TranslateMessage},
 {"user32.dll","UnregisterClassW",(UINT_PTR)&s_UnregisterClassW},
 {"user32.dll","UnregisterDeviceNotification",(UINT_PTR)&s_UnregisterDeviceNotification},
 {"user32.dll","UpdateWindow",(UINT_PTR)&s_UpdateWindow},
 {"user32.dll","ValidateRect",(UINT_PTR)&s_ValidateRect},
 {"gdi32.dll","ChoosePixelFormat",(UINT_PTR)&s_ChoosePixelFormat},
 {"gdi32.dll","GetDeviceCaps",(UINT_PTR)&s_GetDeviceCaps},
 {"gdi32.dll","SetPixelFormat",(UINT_PTR)&s_SetPixelFormat},
 {"gdi32.dll","SwapBuffers",(UINT_PTR)&s_SwapBuffers},
 {"imm32.dll","ImmAssociateContext",(UINT_PTR)&s_ImmAssociateContext},
 {"imm32.dll","ImmAssociateContextEx",(UINT_PTR)&s_ImmAssociateContextEx},
 {"imm32.dll","ImmGetCompositionStringW",(UINT_PTR)&s_ImmGetCompositionStringW},
 {"imm32.dll","ImmGetContext",(UINT_PTR)&s_ImmGetContext},
 {"imm32.dll","ImmGetConversionStatus",(UINT_PTR)&s_ImmGetConversionStatus},
 {"imm32.dll","ImmNotifyIME",(UINT_PTR)&s_ImmNotifyIME},
 {"imm32.dll","ImmReleaseContext",(UINT_PTR)&s_ImmReleaseContext},
 {"imm32.dll","ImmSetCompositionStringW",(UINT_PTR)&s_ImmSetCompositionStringW},
 {"opengl32.dll","wglCreateContext",(UINT_PTR)&s_wglCreateContext},
 {"opengl32.dll","wglDeleteContext",(UINT_PTR)&s_wglDeleteContext},
 {"opengl32.dll","wglGetCurrentContext",(UINT_PTR)&s_wglGetCurrentContext},
 {"opengl32.dll","wglGetCurrentDC",(UINT_PTR)&s_wglGetCurrentDC},
 {"opengl32.dll","wglGetProcAddress",(UINT_PTR)&s_wglGetProcAddress},
 {"opengl32.dll","wglMakeCurrent",(UINT_PTR)&s_wglMakeCurrent},
 {"shell32.dll","CommandLineToArgvW",(UINT_PTR)&s_CommandLineToArgvW},
 {"shell32.dll","SHFileOperationW",(UINT_PTR)&s_SHFileOperationW},
 {"shell32.dll","SHGetFolderPathW",(UINT_PTR)&s_SHGetFolderPathW},
 {"shell32.dll","SHGetKnownFolderPath",(UINT_PTR)&s_SHGetKnownFolderPath},
 {"shell32.dll","ShellExecuteW",(UINT_PTR)&s_ShellExecuteW},
 {"shlwapi.dll","PathCanonicalizeW",(UINT_PTR)&s_PathCanonicalizeW},
 {"shlwapi.dll","SHDeleteKeyW",(UINT_PTR)&s_SHDeleteKeyW},
 {"winmm.dll","timeBeginPeriod",(UINT_PTR)&s_timeBeginPeriod},
 {"winmm.dll","timeEndPeriod",(UINT_PTR)&s_timeEndPeriod},
 {"winmm.dll","timeGetTime",(UINT_PTR)&s_timeGetTime},
 {"winmm.dll","waveInAddBuffer",(UINT_PTR)&s_waveInAddBuffer},
 {"winmm.dll","waveInClose",(UINT_PTR)&s_waveInClose},
 {"winmm.dll","waveInGetDevCapsA",(UINT_PTR)&s_waveInGetDevCapsA},
 {"winmm.dll","waveInGetDevCapsW",(UINT_PTR)&s_waveInGetDevCapsW},
 {"winmm.dll","waveInGetNumDevs",(UINT_PTR)&s_waveInGetNumDevs},
 {"winmm.dll","waveInOpen",(UINT_PTR)&s_waveInOpen},
 {"winmm.dll","waveInPrepareHeader",(UINT_PTR)&s_waveInPrepareHeader},
 {"winmm.dll","waveInReset",(UINT_PTR)&s_waveInReset},
 {"winmm.dll","waveInStart",(UINT_PTR)&s_waveInStart},
 {"winmm.dll","waveInUnprepareHeader",(UINT_PTR)&s_waveInUnprepareHeader},
 {"winmm.dll","waveOutClose",(UINT_PTR)&s_waveOutClose},
 {"winmm.dll","waveOutGetDevCapsA",(UINT_PTR)&s_waveOutGetDevCapsA},
 {"winmm.dll","waveOutGetDevCapsW",(UINT_PTR)&s_waveOutGetDevCapsW},
 {"winmm.dll","waveOutGetNumDevs",(UINT_PTR)&s_waveOutGetNumDevs},
 {"winmm.dll","waveOutGetPosition",(UINT_PTR)&s_waveOutGetPosition},
 {"winmm.dll","waveOutOpen",(UINT_PTR)&s_waveOutOpen},
 {"winmm.dll","waveOutPrepareHeader",(UINT_PTR)&s_waveOutPrepareHeader},
 {"winmm.dll","waveOutReset",(UINT_PTR)&s_waveOutReset},
 {"winmm.dll","waveOutUnprepareHeader",(UINT_PTR)&s_waveOutUnprepareHeader},
 {"winmm.dll","waveOutWrite",(UINT_PTR)&s_waveOutWrite},
};
static bool same(const char* a,const char* b) { while(*a && *a==*b) {++a;++b;} return *a==*b; }
extern "C" __declspec(dllexport) UINT_PTR entry_stubs_resolve(const char* dll,const char* name) { for(const auto& e:entries) if(same(dll,e.dll)&&same(name,e.name)) return e.address; return 0; }
