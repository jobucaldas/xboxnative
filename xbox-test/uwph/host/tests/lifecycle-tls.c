// No CRT: explicit PE TLS directory, as in tls-guest.cpp.
#include <windows.h>
DWORD _tls_index;
BYTE tls_raw[4]={1,2,3,4};
static void NTAPI callback(void* module,DWORD reason,void* reserved) {
    (void)module; (void)reserved;
    if (reason==DLL_PROCESS_ATTACH) OutputDebugStringA("TLS callback attach");
    if (reason==DLL_PROCESS_DETACH) OutputDebugStringA("TLS callback detach");
}
PIMAGE_TLS_CALLBACK callbacks[]={callback,0};
const IMAGE_TLS_DIRECTORY64 _tls_used={
    (ULONGLONG)tls_raw,(ULONGLONG)(tls_raw+sizeof tls_raw),
    (ULONGLONG)&_tls_index,(ULONGLONG)callbacks,4,IMAGE_SCN_ALIGN_4BYTES
};
BOOL WINAPI test_dll_entry(HINSTANCE module,DWORD reason,LPVOID reserved) {
    (void)module; (void)reserved;
    if (reason==DLL_PROCESS_ATTACH) {
        OutputDebugStringA("TLS DllMain attach");
#ifdef FAIL_ATTACH
        return FALSE;
#endif
    }
    if (reason==DLL_PROCESS_DETACH) OutputDebugStringA("TLS DllMain detach");
    return TRUE;
}
