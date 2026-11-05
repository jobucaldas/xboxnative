#include "exception_route.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static unsigned checks;
static volatile LONG cleanup,abnormal;
static DWORD unwindFlags,unwindCode;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
extern "C" ULONG_PTR k32UnwindTarget(ULONG_PTR useEx);
extern "C" __declspec(noinline) void k32UnwindNested(void* frame,void* target,ULONG_PTR useEx) {
    CONTEXT context{};
    EXCEPTION_RECORD record{};record.ExceptionCode=0xe0425555;
    __try {
        if(useEx&1) {
            auto unwind=reinterpret_cast<decltype(&::RtlUnwindEx)>(kernel32_core::resolve_exception("kernel32.dll","RtlUnwindEx"));
            unwind(frame,target,(useEx&2)?nullptr:&record,reinterpret_cast<void*>(42),&context,nullptr);
        } else {
            auto unwind=reinterpret_cast<decltype(&::RtlUnwind)>(kernel32_core::resolve_exception("kernel32.dll","RtlUnwind"));
            unwind(frame,target,(useEx&2)?nullptr:&record,reinterpret_cast<void*>(42));
        }
    } __finally {
        unwindFlags=record.ExceptionFlags;unwindCode=record.ExceptionCode;
        InterlockedIncrement(&cleanup);
        if(AbnormalTermination())InterlockedIncrement(&abnormal);
    }
}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int) {
    for(const char* name : {"RtlUnwind","RtlUnwindEx"}) {
        auto p=kernel32_core::resolve_exception("kernel32.dll",name);CHECK(p);
        CHECK(p==kernel32_core::resolve_exception("ntdll.dll",name));
        CHECK(p==kernel32_core::resolve_exception("kernelbase.dll",name));
        CHECK(p==kernel32_core::resolve_exception("api-ms-win-core-rtlsupport-l1-1-0.dll",name));
        CHECK(p==kernel32_core::resolve_exception("API-MS-WIN-CORE-RTLSUPPORT-L1-2-0.DLL",name));
    }
    for(const char* name : {"RtlVirtualUnwind","RtlCaptureContext","RtlLookupFunctionEntry"})
        CHECK(kernel32_core::resolve_exception("api-ms-win-core-rtlsupport-l1-1-0.dll",name));
    CHECK(kernel32_core::resolve_exception("ntdll.dll","RtlLookupFunctionEntry")==
          kernel32_core::resolve_exception("api-ms-win-core-rtlsupport-l1-1-0.dll","RtlLookupFunctionEntry"));
    CHECK(!kernel32_core::resolve_exception("api-ms-win-core-rtlsupport-l1-1-0.dll","RaiseException"));
    CHECK(!kernel32_core::resolve_exception("api-ms-win-core-rtlsupport-l1-3-0.dll","RtlUnwindEx"));
    for(ULONG_PTR useEx : {ULONG_PTR(0),ULONG_PTR(1),ULONG_PTR(2),ULONG_PTR(3)}) {
        cleanup=0;abnormal=0;
        CHECK(k32UnwindTarget(useEx)==42);CHECK(cleanup==1);CHECK(abnormal==1);
        if(!(useEx&2)) {CHECK(unwindFlags&EXCEPTION_UNWINDING);CHECK(unwindCode==0xe0425555);}
    }
    CHECK(!kernel32_core::resolve_exception("api-ms-win-core-debug-l1-1-0.dll","RtlUnwind"));
    std::printf("kernel32 direct unwind: PASS (%u checks; native stack transfer and finally cleanup, off-device)\n",checks);
}
