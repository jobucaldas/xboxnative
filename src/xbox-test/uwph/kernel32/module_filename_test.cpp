#include <windows.h>
#include <algorithm>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
struct Module { std::wstring path; };
struct State { Module* exe; } storage;
static State* state=&storage;
static Module* find(HMODULE handle) { return handle==reinterpret_cast<HMODULE>(state->exe)?state->exe:nullptr; }
namespace kernel_tail {
static uintptr_t native(const char* name) { return reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),name)); }
}
namespace xinput_core {
static void* module(HMODULE) { return nullptr; }
}
#include "guest_module_filename.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"FAIL %d: %s error=%lu\n",__LINE__,#x,GetLastError());std::exit(1); } } while(0)
int main() {
    wchar_t path[32768];CHECK(GetModuleFileNameW(nullptr,path,32768));
    Module module{path};state->exe=&module;
    for(DWORD size : {0ul,1ul,2ul,8ul,static_cast<DWORD>(module.path.size()),static_cast<DWORD>(module.path.size()+1),32768ul}) {
        wchar_t a[32768],b[32768];std::wmemset(a,L'!',32768);std::wmemset(b,L'!',32768);
        SetLastError(73);DWORD expected=GetModuleFileNameW(nullptr,a,size),error=GetLastError();
        SetLastError(73);CHECK(filenameW(nullptr,b,size)==expected);CHECK(GetLastError()==error);if(size && size<=module.path.size()) {
            // Wine 10 retains the last truncated character (XP behavior).
            // Current Windows requires a terminator; assert both explicitly.
            CHECK(!memcmp(a,b,(size-1)*sizeof(wchar_t)));
            CHECK(a[size-1]==module.path[size-1]);CHECK(b[size-1]==0);
            CHECK(b[size]==L'!');
        } else CHECK(!memcmp(a,b,sizeof(a)));
        char c[32768],d[32768];memset(c,'!',sizeof(c));memset(d,'!',sizeof(d));
        SetLastError(73);expected=GetModuleFileNameA(nullptr,c,size);error=GetLastError();
        SetLastError(73);CHECK(filenameA(nullptr,d,size)==expected);CHECK(GetLastError()==error);if(size && size<=module.path.size()) {
            CHECK(!memcmp(c,d,size-1));CHECK(c[size-1]!=0);CHECK(d[size-1]==0);CHECK(d[size]=='!');
        } else CHECK(!memcmp(c,d,sizeof(c)));
    }
    // Native module handles must retain the OS's own encoding and errors.
    HMODULE native=GetModuleHandleW(L"kernel32.dll");char a[32768],b[32768];
    SetLastError(73);DWORD expected=GetModuleFileNameA(native,a,sizeof(a)),error=GetLastError();
    SetLastError(73);CHECK(filenameA(native,b,sizeof(b))==expected);CHECK(GetLastError()==error);CHECK(!strcmp(a,b));
    HMODULE invalid=reinterpret_cast<HMODULE>(static_cast<uintptr_t>(0x12345));
    for(DWORD size : {0ul,1ul,256ul}) {
        char reference[256],actual[256];memset(reference,'!',sizeof(reference));memset(actual,'!',sizeof(actual));
        SetLastError(73);DWORD result=GetModuleFileNameA(invalid,reference,size),last=GetLastError();
        SetLastError(73);CHECK(filenameA(invalid,actual,size)==result);CHECK(GetLastError()==last);CHECK(!memcmp(reference,actual,sizeof(actual)));
    }
    module.path=L"C:\\Jo\u00e3o\\\u6e38\u620f.dll";
    for(bool ansi : {true,false}) {
        if(ansi)SetFileApisToANSI();else SetFileApisToOEM();
        char encoded[256],actual[256];
        int bytes=WideCharToMultiByte(ansi?CP_ACP:CP_OEMCP,0,module.path.c_str(),-1,encoded,sizeof(encoded),nullptr,nullptr);CHECK(bytes>0);
        SetLastError(73);CHECK(filenameA(nullptr,actual,sizeof(actual))==DWORD(bytes-1));CHECK(!strcmp(encoded,actual));CHECK(GetLastError()==ERROR_SUCCESS);
        memset(actual,'!',sizeof(actual));SetLastError(73);
        CHECK(filenameA(nullptr,actual,1)==1);CHECK(actual[0]==0);CHECK(actual[1]=='!');CHECK(GetLastError()==ERROR_INSUFFICIENT_BUFFER);
    }
    SetFileApisToANSI();
    std::printf("kernel32 module filenames: PASS (%u checks; off-device Wine differential)\n",checks);
}
