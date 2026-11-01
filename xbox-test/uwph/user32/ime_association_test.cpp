#include <windows.h>
#include <imm.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
// Desktop Wine has no package identity. Substitute only library acquisition;
// exercise the exact IMM resolver and helper exports used by the package.
static HMODULE test_packaged_library(LPCWSTR name, DWORD) { return LoadLibraryW(name); }
#define LoadPackagedLibrary test_packaged_library
#include "../imm32/route.h"
#undef LoadPackagedLibrary
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
int main() {
    HMODULE module=LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
    auto associate=reinterpret_cast<decltype(&ImmAssociateContext)>(GetProcAddress(module,"ImmAssociateContext"));
    auto extended=reinterpret_cast<decltype(&ImmAssociateContextEx)>(GetProcAddress(module,"ImmAssociateContextEx"));
    auto reg=reinterpret_cast<decltype(&RegisterClassExW)>(GetProcAddress(module,"RegisterClassExW"));
    auto create=reinterpret_cast<decltype(&CreateWindowExW)>(GetProcAddress(module,"CreateWindowExW"));
    auto destroy=reinterpret_cast<decltype(&DestroyWindow)>(GetProcAddress(module,"DestroyWindow"));
    auto proc=reinterpret_cast<WNDPROC>(GetProcAddress(module,"DefWindowProcW"));
    CHECK(associate && extended && reg && create && destroy && proc);
    CHECK(imm32_core::resolve("IMM32.DLL","ImmAssociateContext")==reinterpret_cast<uintptr_t>(associate));
    CHECK(imm32_core::resolve("imm32.dll","ImmAssociateContextEx")==reinterpret_cast<uintptr_t>(extended));
    CHECK(!imm32_core::resolve("user32.dll","ImmAssociateContext"));
    CHECK(!imm32_core::resolve("imm32.dll","immassociatecontext"));
    WNDCLASSEXW cls{}; cls.cbSize=sizeof(cls); cls.lpfnWndProc=proc; cls.lpszClassName=L"ime-no-context";
    CHECK(reg(&cls));
    HWND guest=create(0,cls.lpszClassName,L"",0,0,0,100,100,nullptr,nullptr,nullptr,nullptr); CHECK(guest);
    HWND host=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,100,100,nullptr,nullptr,nullptr,nullptr); CHECK(host);
    ImmAssociateContext(host,nullptr); // Establish the same no-context state.
    unsigned count=0;
    for (int kind=0;kind<3;++kind) {
        HWND h=kind==0?nullptr:kind==1?reinterpret_cast<HWND>(0x12345678):host;
        HWND g=kind==2?guest:h;
        for (HIMC context : {HIMC(nullptr),reinterpret_cast<HIMC>(0x12345678)}) {
            SetLastError(0xdead); HIMC expected=ImmAssociateContext(h,context); DWORD error=GetLastError();
            SetLastError(0xdead); CHECK(associate(g,context)==expected); DWORD got=GetLastError(); if(got!=error) std::printf("associate kind=%d context=%p actual=%lu Wine=%lu\n",kind,context,got,error); CHECK(got==error); ++count;
            for (DWORD flags : {DWORD(0),DWORD(IACE_CHILDREN),DWORD(IACE_IGNORENOCONTEXT),DWORD(3),DWORD(0xffffffff)}) {
                SetLastError(0xdead); BOOL result=ImmAssociateContextEx(h,context,flags); error=GetLastError();
                SetLastError(0xdead); BOOL actual=extended(g,context,flags); DWORD actualError=GetLastError();
                if(actual!=result || actualError!=error) std::printf("kind=%d context=%p flags=%lu actual=%d/%lu Wine=%d/%lu\n",kind,context,flags,actual,actualError,result,error);
                CHECK(actual==result); CHECK(actualError==error); ++count;
            }
        }
    }
    // Default association cannot succeed without an IME backend.
    SetLastError(0xdead); CHECK(!extended(guest,nullptr,IACE_DEFAULT)); CHECK(GetLastError()==ERROR_CALL_NOT_IMPLEMENTED);
    CHECK(destroy(guest));
    SetLastError(0xdead); CHECK(!extended(guest,nullptr,0)); CHECK(GetLastError()==ERROR_INVALID_WINDOW_HANDLE);
    CHECK(DestroyWindow(host));
    std::printf("IME ASSOCIATION OFFDEVICE PASS %u Wine differential cases, unsupported default, destroyed HWND\n",count);
}
