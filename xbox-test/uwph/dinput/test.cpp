#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "route.h"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
    HMODULE wine=LoadLibraryW(L"dinput8.dll");CHECK(wine);
    auto reference=reinterpret_cast<decltype(&dinput_core::create)>(GetProcAddress(wine,"DirectInput8Create"));CHECK(reference);
    auto instance=GetModuleHandleW(nullptr);
    CHECK(dinput_core::input8a==IID_IDirectInput8A && dinput_core::input8w==IID_IDirectInput8W);
    for(auto iid:{IID_IDirectInput8A,IID_IDirectInput8W,IID_IUnknown}) {
        CHECK(reference(instance,0x800,iid,nullptr,nullptr)==dinput_core::create(instance,0x800,iid,nullptr,nullptr));
        for(DWORD version:{0ul,0x700ul,0x801ul}) {
            void* expected=reinterpret_cast<void*>(1);void* actual=expected;
            HRESULT hr=reference(instance,version,iid,&expected,nullptr);
            CHECK(FAILED(hr) && hr==dinput_core::create(instance,version,iid,&actual,nullptr));
            CHECK(!expected && !actual);
        }
        void* expected=reinterpret_cast<void*>(1);void* actual=expected;
        CHECK(reference(nullptr,0x800,iid,&expected,nullptr)==dinput_core::create(nullptr,0x800,iid,&actual,nullptr));
        CHECK(!expected && !actual);
        actual=reinterpret_cast<void*>(1);
        CHECK(dinput_core::create(instance,0x800,iid,&actual,nullptr)==DIERR_UNSUPPORTED && !actual);
        CHECK(dinput_core::create(instance,0x800,iid,&actual,reinterpret_cast<IUnknown*>(1))==CLASS_E_NOAGGREGATION && !actual);
    }
    void* expected=reinterpret_cast<void*>(1);void* actual=expected;
    GUID unsupported{};
    CHECK(reference(instance,0x800,unsupported,&expected,nullptr)==dinput_core::create(instance,0x800,unsupported,&actual,nullptr));
    CHECK(!actual && !expected);
    CHECK(dinput_core::resolve("DINPUT8.DLL","DirectInput8Create"));
    CHECK(!dinput_core::resolve("user32.dll","DirectInput8Create"));
    CHECK(!dinput_core::resolve("dinput8.dll","DllGetClassObject"));
    puts("DirectInput: Wine version/argument/IID validation; explicit unsupported backend and no aggregation PASS");
}
