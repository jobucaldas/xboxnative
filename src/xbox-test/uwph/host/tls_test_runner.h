#pragma once
#include <string>
#include <new>
// Trusted test bundle: one Windows-managed native carrier for each guest image.
namespace wxl {
extern "C" void __stdcall _CxxThrowException(void*,void*);
extern "C" EXCEPTION_DISPOSITION __cdecl __CxxFrameHandler3(EXCEPTION_RECORD*,void*,CONTEXT*,void*);
extern "C" void __cdecl __std_terminate();
static void cxxDelete(void* p) { ::operator delete(p); }
static Image* tlsDependency;
// The static CRT's _CxxThrowException uses RtlPcToFileHeader, which cannot
// discover VirtualAlloc images. Supply the registered image base explicitly
// in the standard AMD64 C++ exception record; native CRT frame handlers perform
// the search, unwind, destructor calls and catch. Restricted to this test ABI.
static void __stdcall mappedThrow(void* object,void* info) {
    auto image=tlsDependency;
    uintptr_t address=(uintptr_t)info;
    if (!image || address<(uintptr_t)image->base ||
        !image->valid(address-(uintptr_t)image->base,16) || *(DWORD*)info!=0) {
        __std_terminate(); return;
    }
    ULONG_PTR args[]={0x19930520,(ULONG_PTR)object,(ULONG_PTR)info,(ULONG_PTR)image->base};
    RaiseException(0xe06d7363,EXCEPTION_NONCONTINUABLE,4,args);
    __std_terminate();
}
static uintptr_t tlsResolve(const char* dll,const char* name, WORD ordinal) {
    if (ordinal || !name) return 0;
    if (!_stricmp(dll,"uwph-cxx.dll")) {
        if (!strcmp(name,"_CxxThrowException")) return (uintptr_t)&mappedThrow;
        if (!strcmp(name,"__CxxFrameHandler3")) return (uintptr_t)&__CxxFrameHandler3;
        if (!strcmp(name,"__std_terminate")) return (uintptr_t)&__std_terminate;
        if (!strcmp(name,"wxl_delete")) return (uintptr_t)&cxxDelete;
    }
    if (!_stricmp(dll,"tls-test.dll") && tlsDependency) return tlsDependency->symbol(name);
    return 0;
}
inline Carrier getCarrier(HMODULE module) {
    Carrier c;
    if (module) {
        c.index=(DWORD(*)())GetProcAddress(module,"carrier_index");
        c.block=(BYTE*(*)())GetProcAddress(module,"carrier_block");
    }
    return c;
}
// workerLive is set when the worker's termination could not be established.
// The mapped images, their TLS dependency and the worker state are then leaked,
// and the caller must not release the carriers either: the worker may still
// be using all of them.
inline bool runTls(const wchar_t* directory,Log log,HMODULE carrierA,HMODULE carrierB,bool& workerLive) {
    workerLive=false;
    LogScope logging(log);
    struct State { Image dll,exe; DWORD result=0; };
    struct Owner {
        State* state;
        ~Owner() {
            if (!state) return;
            // Retry the consumer first, with its dependency binding intact.
            // If retry still fails, retain the entire graph: member destructors
            // must not release a dependency of pending consumer callbacks.
            if (!state->exe.close()) return;
            delete state;
            tlsDependency=nullptr;
        }
    } owner{new State};
    Image& dll=owner.state->dll;
    Image& exe=owner.state->exe;
    auto ca=getCarrier(carrierA),cb=getCarrier(carrierB);
    char diagnostic[256];
    snprintf(diagnostic,sizeof diagnostic,"WXL carrier A indexfn=%p blockfn=%p index=%lu B indexfn=%p blockfn=%p index=%lu",
        ca.index,ca.block,ca.index?ca.index():~0UL,cb.index,cb.block,cb.index?cb.index():~0UL);
    log(diagnostic);
    if (!ca.index || !ca.block || !cb.index || !cb.block || ca.index()==cb.index()) {
        log("WXL FAIL separate native TLS carriers"); return false;
    }
    if (!load(dll,(std::wstring(directory)+L"\\tls-test.dll").c_str(),log,ca,tlsResolve)) return false;
    tlsDependency=&dll;
    if (!load(exe,(std::wstring(directory)+L"\\tls-test.exe").c_str(),log,cb,tlsResolve)) return false;
    DWORD main=((DWORD(*)())(exe.base+exe.entry))();
    HANDLE thread=CreateThread(nullptr,0,[](void* p)->DWORD {
        auto& w=*(State*)p;
        LogScope logging(w.exe.log);
        if (!w.dll.attachThread(DLL_THREAD_ATTACH)) return 1;
        if (!w.exe.attachThread(DLL_THREAD_ATTACH)) { w.dll.detachThread(DLL_THREAD_DETACH); return 1; }
        auto check=(int(*)())w.exe.symbol("tls_exe_check");
        w.result=check?check():0;
        bool exeDetached=w.exe.detachThread(DLL_THREAD_DETACH);
        bool dllDetached=w.dll.detachThread(DLL_THREAD_DETACH);
        return exeDetached && dllDetached ? 0 : 1;
    },owner.state,0,nullptr);
    if (!thread) return false;
    DWORD wait=WaitForSingleObject(thread,INFINITE),exit=1;
    if (wait!=WAIT_OBJECT_0) {
        // Termination is unproven: never release memory a live worker may use.
        // Closing the handle does not affect the thread itself.
        CloseHandle(thread);
        owner.state=nullptr; workerLive=true;
        log("WXL FAIL TLS worker wait; images and carriers left live");
        return false;
    }
    GetExitCodeThread(thread,&exit); CloseHandle(thread);
    auto after=(int(*)())exe.symbol("tls_exe_after_worker");
    bool ok=main==42 && exit==0 && owner.state->result==42 && after && after();
    // Destructors are only best effort. Report either explicit teardown failure,
    // even if a later destructor retry succeeds. The dependency stays mapped
    // until its consumer has closed: a consumer retry may still call into it.
    bool exeClosed=exe.close();
    bool dllClosed=exeClosed && dll.close();
    ok=ok && exeClosed && dllClosed;
    log(ok?"WXL-TLS-OK: TLS value correct on two threads, callback ran, exception caught":"WXL FAIL TLS two threads or cleanup");
    return ok;
}
}
