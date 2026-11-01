#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <initializer_list>
static HMODULE test_packaged_library(LPCWSTR name, DWORD) { return LoadLibraryW(name); }
#define LoadPackagedLibrary test_packaged_library
#include "route.h"
#undef LoadPackagedLibrary
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main() {
    HMODULE module=LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
    auto drag=reinterpret_cast<decltype(&DragDetect)>(GetProcAddress(module,"DragDetect"));
    auto key=reinterpret_cast<void (WINAPI*)(int,BOOL)>(GetProcAddress(module,"uwph_u32c_key_event"));
    auto state=reinterpret_cast<decltype(&GetAsyncKeyState)>(GetProcAddress(module,"GetAsyncKeyState"));
    auto reg=reinterpret_cast<decltype(&RegisterClassExW)>(GetProcAddress(module,"RegisterClassExW"));
    auto create=reinterpret_cast<decltype(&CreateWindowExW)>(GetProcAddress(module,"CreateWindowExW"));
    auto destroy=reinterpret_cast<decltype(&DestroyWindow)>(GetProcAddress(module,"DestroyWindow"));
    auto capture=reinterpret_cast<decltype(&SetCapture)>(GetProcAddress(module,"SetCapture"));
    auto release=reinterpret_cast<decltype(&ReleaseCapture)>(GetProcAddress(module,"ReleaseCapture"));
    auto proc=reinterpret_cast<WNDPROC>(GetProcAddress(module,"DefWindowProcW"));
    CHECK(drag && key && state && reg && create && destroy && capture && release && proc);
    CHECK(user32_core::resolve("USER32.DLL","DragDetect")==reinterpret_cast<uintptr_t>(drag));
    CHECK(!user32_core::resolve("shell32.dll","DragDetect"));
    CHECK(!user32_core::resolve("user32.dll","dragdetect"));
    // Wine's reference fast path is bounded only when its left button is up.
    CHECK(!(GetKeyState(VK_LBUTTON)&0x8000));
    WNDCLASSEXW cls{}; cls.cbSize=sizeof(cls); cls.lpfnWndProc=proc; cls.lpszClassName=L"drag-test";
    CHECK(reg(&cls));
    HWND guest=create(0,cls.lpszClassName,L"",0,0,0,100,100,nullptr,nullptr,nullptr,nullptr); CHECK(guest);
    CHECK(!capture(guest));
    HWND hostCapture=GetCapture();
    unsigned count=0;
    for (HWND owner : {HWND(nullptr),reinterpret_cast<HWND>(1),reinterpret_cast<HWND>(~uintptr_t(0))}) {
        for (POINT point : {POINT{0,0},POINT{-1,1},POINT{LONG_MIN,LONG_MAX},POINT{LONG_MAX,LONG_MIN}}) {
            key(VK_LBUTTON,FALSE);
            SetLastError(0xdead);
            BOOL expected=DragDetect(owner,point); DWORD error=GetLastError();
            SetLastError(0xdead);
            CHECK(drag(owner,point)==expected && GetLastError()==error);
            CHECK(capture(guest)==guest && GetCapture()==hostCapture);
            key(VK_LBUTTON,TRUE);
            SetLastError(0xdead);
            CHECK(!drag(owner,point) && GetLastError()==ERROR_CALL_NOT_IMPLEMENTED);
            CHECK(capture(guest)==guest && GetCapture()==hostCapture);
            // DragDetect must neither release the key nor consume its async
            // transition bit. No host capture or modal message loop is used.
            CHECK(state(VK_LBUTTON)==static_cast<SHORT>(0x8001));
            CHECK(state(VK_LBUTTON)==static_cast<SHORT>(0x8000));
            key(VK_LBUTTON,FALSE);
            ++count;
        }
    }
    CHECK(release() && destroy(guest));
    std::printf("PASS: %u DragDetect Wine no-button comparisons and held-button failures; resolver ABI\n",count);
}
