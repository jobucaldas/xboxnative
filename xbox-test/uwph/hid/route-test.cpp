#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include "route.h"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
    using namespace hid_core;
    HMODULE wine=LoadLibraryW(L"hid.dll");CHECK(wine);
    auto guid=reinterpret_cast<decltype(&c_HidD_GetHidGuid)>(GetProcAddress(wine,"HidD_GetHidGuid"));CHECK(guid);
    GUID expected{},actual{};guid(&expected);SetLastError(12345);c_HidD_GetHidGuid(&actual);
    CHECK(!memcmp(&expected,&actual,sizeof actual) && GetLastError()==12345);
    auto attrs=reinterpret_cast<decltype(&c_HidD_GetAttributes)>(GetProcAddress(wine,"HidD_GetAttributes"));CHECK(attrs);
    for(HANDLE handle:{HANDLE(nullptr),INVALID_HANDLE_VALUE}) {
        HIDD_ATTRIBUTES a{},b{};a.Size=b.Size=sizeof a;
        SetLastError(0);auto ref=attrs(handle,&a);DWORD error=GetLastError();
        SetLastError(0);CHECK(c_HidD_GetAttributes(handle,&b)==ref && GetLastError()==error);
        CHECK(!memcmp(&a,&b,sizeof a));
    }
    auto caps=reinterpret_cast<decltype(&c_HidP_GetCaps)>(GetProcAddress(wine,"HidP_GetCaps"));CHECK(caps);
    HIDP_CAPS a{},b{};
    CHECK(caps(nullptr,&a)==c_HidP_GetCaps(nullptr,&b));CHECK(!memcmp(&a,&b,sizeof a));
    char invalid[128]{};auto prep=reinterpret_cast<PHIDP_PREPARSED_DATA>(invalid);
    CHECK(caps(prep,&a)==c_HidP_GetCaps(prep,&b));
    auto buttons=reinterpret_cast<decltype(&c_HidP_GetButtonCaps)>(GetProcAddress(wine,"HidP_GetButtonCaps"));CHECK(buttons);
    HIDP_BUTTON_CAPS ac{},bc{};USHORT an=1,bn=1;
    CHECK(buttons(HidP_Input,&ac,&an,prep)==c_HidP_GetButtonCaps(HidP_Input,&bc,&bn,prep));
    CHECK(an==bn && !memcmp(&ac,&bc,sizeof ac));
    PHIDP_PREPARSED_DATA untouched=reinterpret_cast<PHIDP_PREPARSED_DATA>(0x1234);
    CHECK(!c_HidD_GetPreparsedData(INVALID_HANDLE_VALUE,&untouched) && untouched==reinterpret_cast<PHIDP_PREPARSED_DATA>(0x1234));
    CHECK(GetLastError()==ERROR_INVALID_HANDLE);
    auto freePrep=reinterpret_cast<decltype(&c_HidD_FreePreparsedData)>(GetProcAddress(wine,"HidD_FreePreparsedData"));CHECK(freePrep);
    CHECK(c_HidD_FreePreparsedData(nullptr)==freePrep(nullptr));
    auto getPrep=reinterpret_cast<decltype(&c_HidD_GetPreparsedData)>(GetProcAddress(wine,"HidD_GetPreparsedData"));CHECK(getPrep);
    auto referencePrep=untouched;
    SetLastError(0);CHECK(!getPrep(INVALID_HANDLE_VALUE,&referencePrep));CHECK(referencePrep==untouched && GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(!c_HidD_FreePreparsedData(prep) && GetLastError()==ERROR_INVALID_PARAMETER);
#define DIFF(name,args) do { auto reference=reinterpret_cast<decltype(&c_##name)>(GetProcAddress(wine,#name));CHECK(reference);CHECK(reference args == c_##name args); }while(0)
    // Wine reads length pointers before validating the preparsed block. Supply
    // valid in/out storage; never invoke its documented pointer preconditions
    // with null pointers in the differential oracle.
    HIDP_DATA dataA{},dataB{};char reportA[8]{},reportB[8]{};
    for(ULONG length:{0ul,1ul,8ul}) {
        ULONG countA=1,countB=1;
        auto getData=reinterpret_cast<decltype(&c_HidP_GetData)>(GetProcAddress(wine,"HidP_GetData"));CHECK(getData);
        CHECK(getData(HidP_Input,&dataA,&countA,prep,reportA,length)==c_HidP_GetData(HidP_Input,&dataB,&countB,prep,reportB,length));
        CHECK(countA==countB && !memcmp(&dataA,&dataB,sizeof dataA));
        DIFF(HidP_SetUsageValue,(HidP_Input,0,0,0,0,prep,reportA,length));
        USAGE usage=1;ULONG count=1;
        DIFF(HidP_SetUsages,(HidP_Input,0,0,&usage,&count,prep,reportA,length));
    }
    HIDP_VALUE_CAPS valueA{},valueB{};USHORT countA=1,countB=1;
    auto values=reinterpret_cast<decltype(&c_HidP_GetValueCaps)>(GetProcAddress(wine,"HidP_GetValueCaps"));CHECK(values);
    CHECK(values(HidP_Input,&valueA,&countA,prep)==c_HidP_GetValueCaps(HidP_Input,&valueB,&countB,prep));
    CHECK(countA==countB && !memcmp(&valueA,&valueB,sizeof valueA));
    DIFF(HidP_MaxDataListLength,(HidP_Input,prep));
    for(auto name:{"HidD_GetManufacturerString","HidD_GetProductString","HidD_GetSerialNumberString"}) {
        using String=BOOLEAN(WINAPI*)(HANDLE,void*,ULONG);
        auto fn=reinterpret_cast<String>(resolve("hid.dll",name));CHECK(fn);
        auto oracle=reinterpret_cast<String>(GetProcAddress(wine,name));CHECK(oracle);
        char buffer[16],expectedBuffer[16];memset(buffer,0xa5,sizeof buffer);memset(expectedBuffer,0xa5,sizeof expectedBuffer);
        SetLastError(0);CHECK(!oracle(INVALID_HANDLE_VALUE,expectedBuffer,sizeof expectedBuffer));CHECK(GetLastError()==ERROR_INVALID_HANDLE);
        CHECK(!fn(INVALID_HANDLE_VALUE,buffer,sizeof buffer) && GetLastError()==ERROR_INVALID_HANDLE);
        CHECK(!memcmp(buffer,expectedBuffer,sizeof buffer));
        for(auto ch:buffer)CHECK((unsigned char)ch==0xa5);
    }
    CHECK(!resolve(nullptr,"HidD_GetHidGuid"));CHECK(!resolve("hid.dll",nullptr));
    CHECK(!resolve("user32.dll","HidD_GetHidGuid"));
    puts("HID routes: Wine GUID/invalid-handle/invalid-preparsed differential; outputs, no-endpoint isolation and resolver PASS");
}
