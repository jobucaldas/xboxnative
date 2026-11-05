#pragma once
// Shared by the mapped EXE and DLL fixtures. A notification releases its own
// final explicit reference. Callback code must remain mapped until it returns.
#ifdef PLAY_DLL
static void observeLoaderThread(HMODULE image,DWORD reason) {
    using Observe=void(WINAPI*)(HMODULE,DWORD);
    auto observe=(Observe)GetProcAddress(GetModuleHandleW(nullptr),"loader_thread_observe");
    if(observe)observe(image,reason);
}
#else
static HMODULE threadNotifyModule;
static DWORD threadNotifyReason;
static volatile LONG threadNotifyCalls,threadNotifyDetached,threadNotifyBad,threadNotifyDepth;
__declspec(dllexport) void WINAPI loader_thread_observe(HMODULE image,DWORD reason) {
    if(image!=threadNotifyModule)return;
    if(reason==DLL_PROCESS_DETACH) {
        if(threadNotifyDepth)++threadNotifyBad;
        ++threadNotifyDetached;
    }
    if(reason!=threadNotifyReason)return;
    ++threadNotifyDepth;
    ++threadNotifyCalls;
    if(!FreeLibrary(image))++threadNotifyBad;
    HMODULE visible=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)image,&visible)||visible!=image)++threadNotifyBad;
    --threadNotifyDepth;
}
static DWORD WINAPI threadNotifyWorker(void*) {return 79;}
static DWORD testLoaderThreadNotifications() {
    const DWORD reasons[]={DLL_THREAD_ATTACH,DLL_THREAD_DETACH};
    for(DWORD reason:reasons) {
        threadNotifyCalls=threadNotifyDetached=threadNotifyBad=threadNotifyDepth=0;
        threadNotifyReason=reason;
        threadNotifyModule=LoadLibraryW(L"play-dynamic.dll");
        if(!threadNotifyModule)return 86;
        HANDLE thread=CreateThread(nullptr,0,threadNotifyWorker,nullptr,0,nullptr);
        if(!thread||WaitForSingleObject(thread,5000)!=WAIT_OBJECT_0)return 87;
        DWORD code=0;
        if(!GetExitCodeThread(thread,&code)||code!=79||!CloseHandle(thread))return 88;
        if(threadNotifyCalls!=1||threadNotifyDetached!=1||threadNotifyBad)return 89;
        if(GetModuleHandleW(L"play-dynamic.dll"))return 90;
        MEMORY_BASIC_INFORMATION memory{};
        if(!VirtualQuery(threadNotifyModule,&memory,sizeof memory)||memory.State!=MEM_FREE)return 91;
        threadNotifyModule=nullptr;
    }
    return 42;
}
#endif
