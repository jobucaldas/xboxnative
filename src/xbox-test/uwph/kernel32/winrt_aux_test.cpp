#include "winrt_route.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
template<class F> F api(const char* dll,const char* name,F) {
    auto address=kernel32_core::resolve_winrt(dll,name);CHECK(address);
    return reinterpret_cast<F>(address);
}
// Wine 10 exports RoGetBufferMarshaler as a stub which raises this exception.
// Preserve that reference result explicitly; never accept other exceptions.
template<class Function,class Argument>
static HRESULT sdkCall(Function function,Argument out,DWORD* exception) {
    __try {return function(out);}
    __except(GetExceptionCode()==0x80000100?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {
        *exception=GetExceptionCode();return E_NOTIMPL;
    }
}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int) {
    const char* errors="api-ms-win-core-winrt-error-l1-1-0.dll";
    const char* buffers="api-ms-win-core-winrt-robuffer-l1-1-0.dll";
    auto restricted=api(errors,"GetRestrictedErrorInfo",&::GetRestrictedErrorInfo);
    auto flags=api(errors,"RoSetErrorReportingFlags",&::RoSetErrorReportingFlags);
    auto marshaler=api(buffers,"RoGetBufferMarshaler",&::RoGetBufferMarshaler);
    CHECK(!kernel32_core::resolve_winrt(buffers,"GetRestrictedErrorInfo"));
    CHECK(!kernel32_core::resolve_winrt(errors,"RoGetBufferMarshaler"));
    CHECK(!kernel32_core::resolve_winrt("kernel32.dll","RoSetErrorReportingFlags"));
    CHECK(RoInitialize(RO_INIT_MULTITHREADED)==S_OK);
    IRestrictedErrorInfo* expected=nullptr;IRestrictedErrorInfo* actual=nullptr;
    HRESULT reference=GetRestrictedErrorInfo(&expected);
    CHECK(restricted(&actual)==reference);CHECK(actual==expected);
    if(actual)actual->Release();if(expected)expected->Release();
    UINT32 saved=0;DWORD queryException=0;
    HRESULT query=sdkCall(&RoGetErrorReportingFlags,&saved,&queryException);
    bool stateVerified=SUCCEEDED(query);
    for(UINT32 value : {UINT32(0),UINT32(1),UINT32(4)}) {
        CHECK(flags(value)==RoSetErrorReportingFlags(value));
        if(stateVerified) {
            UINT32 observed=~value;CHECK(SUCCEEDED(RoGetErrorReportingFlags(&observed)));
            CHECK(observed==value);
        }
    }
    if(stateVerified)CHECK(SUCCEEDED(flags(saved)));
    IMarshal *expectedMarshaler=nullptr,*actualMarshaler=nullptr;
    DWORD referenceException=0,actualException=0;
    HRESULT expectedResult=sdkCall(&::RoGetBufferMarshaler,&expectedMarshaler,&referenceException);
    HRESULT actualResult=sdkCall(marshaler,&actualMarshaler,&actualException);
    CHECK(actualResult==expectedResult);CHECK(actualException==referenceException);
    CHECK(bool(actualMarshaler)==bool(expectedMarshaler));
    if(SUCCEEDED(actualResult)) {
        CHECK(actualMarshaler);CHECK(expectedMarshaler);
        IUnknown* identity=nullptr;
        CHECK(actualMarshaler->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&identity))==S_OK);
        CHECK(identity);identity->Release();
    }
    if(actualMarshaler)actualMarshaler->Release();
    if(expectedMarshaler)expectedMarshaler->Release();
    RoUninitialize();
    bool supported=SUCCEEDED(reference)&&stateVerified&&SUCCEEDED(actualResult)&&!actualException;
    std::printf("kernel32 WinRT auxiliary: PASS (%u checks; off-device native differential; restricted=0x%08lx flags-query=0x%08lx marshaler-exception=0x%08lx; full-support=%d)\n",checks,reference,query,actualException,int(supported));
    if(std::getenv("K32_REQUIRE_WINRT_AUX"))CHECK(supported);
    return 0;
}
