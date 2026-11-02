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
inline bool runTls(const wchar_t* directory,Log log,HMODULE carrierA,HMODULE carrierB) {
    Image dll,exe;
    struct Reset { ~Reset() { tlsDependency=nullptr; } } reset;
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
    struct Work { Image* dll; Image* exe; DWORD result; } work{&dll,&exe,0};
    HANDLE thread=CreateThread(nullptr,0,[](void* p)->DWORD {
        auto& w=*(Work*)p;
        if (!w.dll->attachThread(DLL_THREAD_ATTACH)) return 1;
        if (!w.exe->attachThread(DLL_THREAD_ATTACH)) { w.dll->detachThread(DLL_THREAD_DETACH); return 1; }
        auto check=(int(*)())w.exe->symbol("tls_exe_check");
        w.result=check?check():0;
        w.exe->detachThread(DLL_THREAD_DETACH);
        w.dll->detachThread(DLL_THREAD_DETACH);
        return 0;
    },&work,0,nullptr);
    if (!thread) return false;
    DWORD wait=WaitForSingleObject(thread,INFINITE),exit=1;
    GetExitCodeThread(thread,&exit); CloseHandle(thread);
    auto after=(int(*)())exe.symbol("tls_exe_after_worker");
    bool ok=main==42 && wait==WAIT_OBJECT_0 && exit==0 && work.result==42 && after && after();
    log(ok?"WXL-TLS-OK: TLS value correct on two threads, callback ran, exception caught":"WXL FAIL TLS two threads");
    return ok;
}
}
