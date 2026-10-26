#include <windows.h>
static const char* volatile attach_message="WXL DLL fixture ATTACH";
static const char* volatile detach_message="WXL DLL fixture DETACH";
BOOL WINAPI fixture_entry(HINSTANCE module,DWORD reason,LPVOID reserved) {
    (void)module; (void)reserved;
    if (reason==DLL_PROCESS_ATTACH) {
        OutputDebugStringA(attach_message);
        return GetCurrentProcessId()!=0;
    }
    if (reason==DLL_PROCESS_DETACH) OutputDebugStringA(detach_message);
    return TRUE;
}
