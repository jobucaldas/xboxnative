#include "route.h"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
    // Wine's raw-buffer server request assumes an existing GUI thread queue.
    // Establish that queue before testing the no-pending-input contract.
    MSG warmup{};PeekMessageW(&warmup,nullptr,0,0,PM_NOREMOVE);
    unsigned cases=0;
    CHECK(sizeof(RAWINPUTHEADER)==24);
    for(HANDLE handle:{HANDLE(nullptr),INVALID_HANDLE_VALUE,HANDLE(0x1234)})
    for(UINT command:std::initializer_list<UINT>{RID_INPUT,RID_HEADER,0u})for(UINT header:{0u,24u,48u})for(bool query:{false,true}) {
        UINT a=73,b=73;char actual[128],expected[128];memset(actual,0xa5,128);memset(expected,0xa5,128);
        SetLastError(0);UINT ref=GetRawInputData(HRAWINPUT(handle),command,query?nullptr:expected,&a,header);DWORD error=GetLastError();
        SetLastError(0);CHECK(rawinput_core::data(handle,command,query?nullptr:actual,&b,header)==ref);
        CHECK(GetLastError()==error && a==b && !memcmp(actual,expected,128));++cases;
    }
    for(UINT command:std::initializer_list<UINT>{RIDI_DEVICEINFO,RIDI_DEVICENAME,RIDI_PREPARSEDDATA,0u})for(bool noSize:{false,true}) {
        UINT a=73,b=73;char actual[128],expected[128];memset(actual,0xa5,128);memset(expected,0xa5,128);
        SetLastError(0);UINT ref=GetRawInputDeviceInfoW(nullptr,command,expected,noSize?nullptr:&a);DWORD error=GetLastError();
        SetLastError(0);CHECK(rawinput_core::device_info(nullptr,command,actual,noSize?nullptr:&b)==ref);
        CHECK(GetLastError()==error && a==b && !memcmp(actual,expected,128));++cases;
    }
    for(UINT header:{0u,24u,48u})for(bool query:{false,true})for(bool noSize:{false,true}) {
        UINT a=128,b=128;RAWINPUT actual[2]{},expected[2]{};
        SetLastError(0);UINT ref=GetRawInputBuffer(query?nullptr:expected,noSize?nullptr:&a,header);DWORD error=GetLastError();
        SetLastError(0);CHECK(rawinput_core::buffer(query?nullptr:actual,noSize?nullptr:&b,header)==ref);
        CHECK(GetLastError()==error && a==b && !memcmp(actual,expected,sizeof actual));++cases;
    }
    CHECK(rawinput_core::resolve("USER32.DLL","GetRawInputData"));
    CHECK(!rawinput_core::resolve("hid.dll","GetRawInputData"));
    CHECK(!rawinput_core::resolve(nullptr,"GetRawInputData"));
    printf("RawInput: %u Wine unknown-handle, size/error precedence, output and empty-buffer comparisons PASS\n",cases);
}
