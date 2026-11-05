#include "winrt_route.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
template<class F> F api(const char* dll,const char* name,F) {
    auto p=kernel32_core::resolve_winrt(dll,name);CHECK(p);return reinterpret_cast<F>(p);
}
#define STRING(n) api("api-ms-win-core-winrt-string-l1-1-0.dll",#n,&::n)
#define ACTIVATE(n) api("api-ms-win-core-winrt-l1-1-0.dll",#n,&::n)
int main() {
    auto create=STRING(WindowsCreateString);auto destroy=STRING(WindowsDeleteString);
    auto raw=STRING(WindowsGetStringRawBuffer);auto duplicate=STRING(WindowsDuplicateString);
    auto compare=STRING(WindowsCompareStringOrdinal);auto reference=STRING(WindowsCreateStringReference);
    HSTRING empty=nullptr;CHECK(create(nullptr,0,&empty)==S_OK);CHECK(empty==nullptr);
    UINT32 length=99;CHECK(raw(empty,&length));CHECK(length==0);CHECK(*raw(empty,nullptr)==0);
    CHECK(destroy(empty)==S_OK);
    wchar_t source[]={L'A',0,L'\u00e7',0xd83d,0xde00,0};
    HSTRING owned=nullptr;CHECK(create(source,5,&owned)==S_OK);CHECK(owned);
    auto bytes=raw(owned,&length);CHECK(length==5);CHECK(bytes[0]==L'A');CHECK(bytes[1]==0);CHECK(bytes[2]==0xe7);CHECK(bytes[3]==0xd83d);CHECK(bytes[4]==0xde00);CHECK(bytes[5]==0);
    source[0]=L'Z';CHECK(raw(owned,nullptr)[0]==L'A');
    HSTRING alias=nullptr;CHECK(duplicate(owned,&alias)==S_OK);CHECK(destroy(owned)==S_OK);CHECK(raw(alias,&length)[0]==L'A');CHECK(length==5);
    HSTRING_HEADER header{};HSTRING borrowed=nullptr;
    CHECK(reference(source,5,&header,&borrowed)==S_OK);CHECK(raw(borrowed,&length)==source);CHECK(length==5);
    HSTRING copied=nullptr;CHECK(duplicate(borrowed,&copied)==S_OK);CHECK(raw(copied,nullptr)!=source);
    CHECK(destroy(borrowed)==S_OK);CHECK(raw(copied,&length)[0]==L'Z');CHECK(length==5);
    INT32 result=99;CHECK(compare(alias,copied,&result)==S_OK);CHECK(result<0);
    CHECK(compare(copied,copied,&result)==S_OK);CHECK(result==0);
    CHECK(compare(nullptr,copied,&result)==S_OK);CHECK(result<0);
    CHECK(compare(copied,nullptr,&result)==S_OK);CHECK(result>0);
    CHECK(compare(copied,alias,nullptr)==WindowsCompareStringOrdinal(copied,alias,nullptr));
    CHECK(create(L"x",1,nullptr)==WindowsCreateString(L"x",1,nullptr));
    HSTRING a=nullptr,b=nullptr;
    CHECK(create(nullptr,1,&a)==WindowsCreateString(nullptr,1,&b));CHECK(a==b);
    wchar_t unterminated[]={L'x',L'y'};HSTRING_HEADER h1{},h2{};
    CHECK(reference(unterminated,1,&h1,&a)==WindowsCreateStringReference(unterminated,1,&h2,&b));CHECK(a==b);
    CHECK(destroy(copied)==S_OK);CHECK(destroy(alias)==S_OK);
    auto initialize=ACTIVATE(RoInitialize);auto uninitialize=ACTIVATE(RoUninitialize);
    CHECK(initialize(RO_INIT_MULTITHREADED)==S_OK);CHECK(initialize(RO_INIT_MULTITHREADED)==S_FALSE);
    uninitialize(); // balance the second successful initialization
    auto activate=ACTIVATE(RoActivateInstance);auto factory=ACTIVATE(RoGetActivationFactory);
    HSTRING missing=nullptr;const wchar_t className[]=L"K32x.DefinitelyMissing.ActivationClass";
    CHECK(create(className,UINT32(sizeof(className)/sizeof(wchar_t)-1),&missing)==S_OK);
    IInspectable *expected=nullptr,*actual=nullptr;
    HRESULT hr=RoActivateInstance(missing,&expected);CHECK(FAILED(hr));CHECK(activate(missing,&actual)==hr);CHECK(actual==expected);CHECK(!actual);
    // IUnknown's IID avoids compiler-specific UUID support in the host test.
    const IID iid={0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
    void *expectedFactory=nullptr,*actualFactory=nullptr;
    hr=RoGetActivationFactory(missing,iid,&expectedFactory);CHECK(FAILED(hr));CHECK(factory(missing,iid,&actualFactory)==hr);CHECK(actualFactory==expectedFactory);CHECK(!actualFactory);
    CHECK(destroy(missing)==S_OK);uninitialize();
    CHECK(!kernel32_core::resolve_winrt("api-ms-win-core-winrt-string-l1-1-0.dll","RoInitialize"));
    CHECK(!kernel32_core::resolve_winrt("kernel32.dll","WindowsCreateString"));
    std::printf("kernel32 WinRT contracts: PASS (%u checks; off-device strings and activation failures)\n",checks);
}
