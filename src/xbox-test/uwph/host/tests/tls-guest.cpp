#include <windows.h>
extern "C" {
thread_local int tls_value=73;
thread_local int tls_count=0;
DWORD _tls_index;
extern BYTE tls_raw[8];
#pragma comment(linker,"/alternatename:tls_raw=tls_value")
static void NTAPI callback(void*,DWORD reason,void*) {
    if (reason==DLL_PROCESS_ATTACH || reason==DLL_THREAD_ATTACH) {
        ++tls_count; ++tls_value;
    }
}
PIMAGE_TLS_CALLBACK callbacks[]={callback,nullptr};
extern const IMAGE_TLS_DIRECTORY64 _tls_used={
    (ULONGLONG)tls_raw,(ULONGLONG)(tls_raw+4),(ULONGLONG)&_tls_index,
    (ULONGLONG)callbacks,4,IMAGE_SCN_ALIGN_4BYTES
};
#ifdef TLS_DLL
__declspec(dllexport) int tls_dll_check() {
    if (tls_value!=74 || tls_count!=1) return -1;
    tls_value+=100;
    return tls_value;
}
BOOL WINAPI tls_dll_entry(HINSTANCE,DWORD reason,void*) {
    if (reason==DLL_PROCESS_ATTACH || reason==DLL_THREAD_ATTACH) {
        if (tls_value!=74 || tls_count!=1) return FALSE;
        OutputDebugStringA("WXL TLS DllMain attach follows callback");
    }
    return GetCurrentProcessId()!=0;
}
#else
__declspec(dllimport) int tls_dll_check();
int cxx_catch();
__declspec(dllexport) int tls_exe_check() {
    if (tls_value!=74 || tls_count!=1 || tls_dll_check()!=174) return -1;
    if (!cxx_catch()) return -2;
    tls_value+=200;
    OutputDebugStringA("WXL TLS native template and callback correct on current thread");
    return 42;
}
DWORD tls_exe_entry() { return tls_exe_check(); }
__declspec(dllexport) int tls_exe_after_worker() {
    return tls_value==274 && tls_count==1;
}
#endif
}
