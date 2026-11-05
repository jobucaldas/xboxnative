#pragma once
// Included by both mapped test images. The main thread observes these counters
// only after waiting for the native worker handle to become signaled.
static volatile LONG threadDetachTls, threadDetachDll, threadDetachBad;
static void observeThreadTlsDetach(DWORD reason) {
    if(reason==DLL_THREAD_DETACH) {
        if(tls_value!=74||tls_count!=1)InterlockedIncrement(&threadDetachBad);
        InterlockedIncrement(&threadDetachTls);
    }
}
#ifdef PLAY_DLL
__declspec(dllexport) int dependency_detach_check() {
    return threadDetachTls==2&&threadDetachDll==2&&!threadDetachBad;
}
static void observeThreadDllDetach(DWORD reason) {
    if(reason==DLL_THREAD_DETACH) {
        if(tls_value!=74||tls_count!=1||threadDetachDll!=threadDetachTls)InterlockedIncrement(&threadDetachBad);
        InterlockedIncrement(&threadDetachDll);
    }
}
#else
__declspec(dllimport) int dependency_detach_check();
static DWORD threadExitFlsSlot;
static volatile LONG threadExitFlsCount;
static void WINAPI threadExitFlsCleanup(void* value) {
    if(value!=(void*)73||threadDetachTls!=threadExitFlsCount+1)InterlockedIncrement(&threadDetachBad);
    InterlockedIncrement(&threadExitFlsCount);
}
static DWORD WINAPI explicitThreadExit(void* dynamic) {
    if(!FlsSetValue(threadExitFlsSlot,(void*)73))return 47;
    if(dynamic) {
        auto exit=(void(WINAPI*)(DWORD))GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"ExitThread");
        if(!exit)return 40;
        exit(73);
    } else ExitThread(73);
    return 41;
}
static DWORD testExplicitThreadExit() {
    threadExitFlsSlot=FlsAlloc(threadExitFlsCleanup);
    if(threadExitFlsSlot==FLS_OUT_OF_INDEXES)return 48;
    for(int i=0;i<2;++i) {
        HANDLE h=CreateThread(nullptr,0,explicitThreadExit,(void*)(ULONG_PTR)i,0,nullptr);
        if(!h)return 43;
        DWORD code=0;
        if(WaitForSingleObject(h,10000)!=WAIT_OBJECT_0||!GetExitCodeThread(h,&code)||code!=73)return 44;
        if(!CloseHandle(h))return 45;
    }
    if(threadDetachTls!=2||threadDetachBad||!dependency_detach_check())return 46;
    if(threadExitFlsCount!=2||!FlsFree(threadExitFlsSlot))return 49;
    return 42;
}
#endif
