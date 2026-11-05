// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixtures only. Never call a process-creation function, even on failure.
#include <windows.h>
extern "C" {
int process_anchor;
void* process_relocation=&process_anchor;
#ifdef PROCESS_IMPORT
#ifdef PROCESS_FORWARD
__declspec(dllimport) void PROCESS_IMPORT();
#endif
auto volatile process_reference=&PROCESS_IMPORT;
// A static import must fail before this TLS callback or the entry runs.
DWORD _tls_index;
BYTE tls_raw[4];
static void NTAPI callback(void*,DWORD,void*) { RaiseException(0xe0425052,EXCEPTION_NONCONTINUABLE,0,nullptr); }
PIMAGE_TLS_CALLBACK callbacks[]={callback,nullptr};
extern const IMAGE_TLS_DIRECTORY64 _tls_used={
    (ULONGLONG)tls_raw,(ULONGLONG)(tls_raw+4),(ULONGLONG)&_tls_index,
    (ULONGLONG)callbacks,0,IMAGE_SCN_ALIGN_4BYTES};
DWORD process_entry() { return process_reference?99:98; }
#else
static DWORD WINAPI thread_entry(void* value) { return *(DWORD*)value; }
DWORD process_entry() {
    const wchar_t* libraries[]={L"kernel32.dll",L"kernelbase.dll",
        L"api-ms-win-core-processthreads-l1-1-0.dll",L"api-ms-win-core-processthreads-l1-1-2.dll"};
    for(auto library:libraries) {
        HMODULE h=LoadLibraryW(library);if(!h)return 1;
        const char* processNames[]={"CreateProcessA","CreateProcessW"};
        for(auto name:processNames) {
            SetLastError(0);
            if(GetProcAddress(h,name) || GetLastError()!=ERROR_PROC_NOT_FOUND)return 2;
        }
        if(!FreeLibrary(h))return 3;
    }
    HMODULE forward=LoadLibraryW(L"process-forward.dll");if(!forward)return 4;
    const char* names[]={"Kernel32A","Kernel32W","KernelbaseA","KernelbaseW","CoreA","CoreW","Core2A","Core2W"};
    for(WORD i=0;i<8;++i) {
        SetLastError(0);
        if(GetProcAddress(forward,names[i]) || GetLastError()!=ERROR_PROC_NOT_FOUND)return 5;
        SetLastError(0);
        if(GetProcAddress(forward,MAKEINTRESOURCEA(i+1)) || GetLastError()!=ERROR_PROC_NOT_FOUND)return 6;
    }
    // Thread creation remains supported, with its real exit code and thread ID.
    DWORD value=42,id=0,code=0;
    HANDLE thread=CreateThread(nullptr,0,thread_entry,&value,0,&id);
    if(!thread || !id || WaitForSingleObject(thread,10000)!=WAIT_OBJECT_0 ||
       !GetExitCodeThread(thread,&code) || code!=42)return 7;
    CloseHandle(thread);return 42;
}
#endif
}
