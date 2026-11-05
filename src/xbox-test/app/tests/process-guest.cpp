// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixtures only. Process-creation imports resolve to Play's refusal; each
// call names no program, so even a regressed native binding creates nothing.
#include <windows.h>
extern "C" {
int process_anchor;
void* process_relocation=&process_anchor;
using Process=BOOL(WINAPI*)(const void*,void*,void*,void*,BOOL,DWORD,void*,const void*,void*,PROCESS_INFORMATION*);
// A refusal: FALSE, ERROR_NOT_SUPPORTED and no process/thread handles or IDs.
static bool refused(Process process) {
    if(!process)return false;
    PROCESS_INFORMATION info;
    info.hProcess=info.hThread=(HANDLE)1;info.dwProcessId=info.dwThreadId=1;
    SetLastError(0);
    return !process(nullptr,nullptr,nullptr,nullptr,FALSE,0,nullptr,nullptr,nullptr,&info) &&
        GetLastError()==ERROR_NOT_SUPPORTED && !info.hProcess && !info.hThread &&
        !info.dwProcessId && !info.dwThreadId;
}
#ifdef PROCESS_IMPORT
#ifdef PROCESS_FORWARD
__declspec(dllimport) void PROCESS_IMPORT();
#endif
// Read the bound IAT at run time: a static initializer would need CRT startup.
void* volatile process_reference;
// The static import resolves like any other, so the entry runs normally.
DWORD process_entry() {
    process_reference=(void*)&PROCESS_IMPORT;
    return refused((Process)process_reference)?42:96;
}
#else
static DWORD WINAPI thread_entry(void* value) { return *(DWORD*)value; }
DWORD process_entry() {
    const wchar_t* libraries[]={L"kernel32.dll",L"kernelbase.dll",
        L"api-ms-win-core-processthreads-l1-1-0.dll",L"api-ms-win-core-processthreads-l1-1-2.dll"};
    for(auto library:libraries) {
        HMODULE h=LoadLibraryW(library);if(!h)return 1;
        const char* processNames[]={"CreateProcessA","CreateProcessW"};
        for(auto name:processNames) if(!refused((Process)GetProcAddress(h,name)))return 2;
        if(!FreeLibrary(h))return 3;
    }
    HMODULE forward=LoadLibraryW(L"process-forward.dll");if(!forward)return 4;
    const char* names[]={"Kernel32A","Kernel32W","KernelbaseA","KernelbaseW","CoreA","CoreW","Core2A","Core2W"};
    for(WORD i=0;i<8;++i) {
        if(!refused((Process)GetProcAddress(forward,names[i])))return 5;
        if(!refused((Process)GetProcAddress(forward,MAKEINTRESOURCEA(i+1))))return 6;
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
