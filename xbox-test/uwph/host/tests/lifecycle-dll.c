#include <windows.h>
// No CRT: exercise a real compiler-generated .pdata and DLL lifecycle.
static const char* volatile attach_message="LIFECYCLE attach";
BOOL WINAPI test_dll_entry(HINSTANCE module, DWORD reason, LPVOID reserved) {
    volatile DWORD frame[8];
    frame[0]=GetCurrentProcessId();
    if (!frame[0]) return FALSE;
    if (reason==DLL_PROCESS_ATTACH) {
        DWORD64 base=0;
        PRUNTIME_FUNCTION function=RtlLookupFunctionEntry((DWORD64)&test_dll_entry,&base,NULL);
        if (!function || base!=(DWORD64)module) return FALSE;
        OutputDebugStringA("LIFECYCLE unwind lookup ok");
        OutputDebugStringA(attach_message);
    }
    if (reason==DLL_PROCESS_DETACH) OutputDebugStringA("LIFECYCLE detach");
    return TRUE;
}
