#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cstdint>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
static unsigned callbacks;
static INT_PTR CALLBACK dialogProc(HWND, UINT, WPARAM, LPARAM) { ++callbacks; return TRUE; }
int main() {
    HMODULE module=LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
    auto ansi=reinterpret_cast<decltype(&DialogBoxParamA)>(GetProcAddress(module,"DialogBoxParamA"));
    auto wide=reinterpret_cast<decltype(&DialogBoxParamW)>(GetProcAddress(module,"DialogBoxParamW"));
    auto end=reinterpret_cast<decltype(&EndDialog)>(GetProcAddress(module,"EndDialog"));
    CHECK(ansi && wide && end);
    unsigned count=0;
    for (auto token : {uintptr_t(1),uintptr_t(0x12345678),UINTPTR_MAX}) {
        HWND owner=reinterpret_cast<HWND>(token);
        SetLastError(0xdead); INT_PTR expected=DialogBoxParamW(nullptr,MAKEINTRESOURCEW(1),owner,dialogProc,99); DWORD error=GetLastError();
        SetLastError(0xdead); CHECK(wide(nullptr,MAKEINTRESOURCEW(1),owner,dialogProc,99)==expected); CHECK(GetLastError()==error); ++count;
        SetLastError(0xdead); expected=DialogBoxParamA(nullptr,MAKEINTRESOURCEA(1),owner,dialogProc,99); error=GetLastError();
        SetLastError(0xdead); CHECK(ansi(nullptr,MAKEINTRESOURCEA(1),owner,dialogProc,99)==expected); CHECK(GetLastError()==error); ++count;
    }
    for (auto token : {uintptr_t(0),uintptr_t(1),uintptr_t(0x12345678),UINTPTR_MAX}) {
        HWND hwnd=reinterpret_cast<HWND>(token);
        SetLastError(0xdead); BOOL expected=EndDialog(hwnd,42); DWORD error=GetLastError();
        SetLastError(0xdead); CHECK(end(hwnd,42)==expected); CHECK(GetLastError()==error); ++count;
    }
    auto reg=reinterpret_cast<decltype(&RegisterClassExW)>(GetProcAddress(module,"RegisterClassExW"));
    auto create=reinterpret_cast<decltype(&CreateWindowExW)>(GetProcAddress(module,"CreateWindowExW"));
    auto destroy=reinterpret_cast<decltype(&DestroyWindow)>(GetProcAddress(module,"DestroyWindow"));
    auto valid=reinterpret_cast<decltype(&IsWindow)>(GetProcAddress(module,"IsWindow"));
    auto defaultProc=reinterpret_cast<WNDPROC>(GetProcAddress(module,"DefWindowProcW"));
    CHECK(reg && create && destroy && valid && defaultProc);
    WNDCLASSEXW cls{}; cls.cbSize=sizeof(cls); cls.lpfnWndProc=defaultProc; cls.lpszClassName=L"dialog-failure-contract";
    CHECK(reg(&cls));
    HWND guest=create(0,cls.lpszClassName,L"",0,0,0,100,100,nullptr,nullptr,nullptr,nullptr); CHECK(guest);
    // Compare EndDialog on ordinary (non-dialog) windows with host Wine.
    HWND host=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,100,100,nullptr,nullptr,nullptr,nullptr); CHECK(host);
    SetLastError(0xdead); BOOL expected=EndDialog(host,42); DWORD error=GetLastError();
    SetLastError(0xdead); CHECK(end(guest,42)==expected); CHECK(GetLastError()==error); ++count;
    CHECK(valid(guest)); CHECK(DestroyWindow(host));
    for (HWND owner : {HWND(nullptr),guest}) {
        // Poison module/template values prove unsupported creation never forwards
        // guest resource handles to host APIs or dereferences template pointers.
        SetLastError(0xdead); CHECK(wide((HINSTANCE)1,(LPCWSTR)1,owner,dialogProc,99)==-1); CHECK(GetLastError()==ERROR_CALL_NOT_IMPLEMENTED); ++count;
        SetLastError(0xdead); CHECK(ansi((HINSTANCE)1,(LPCSTR)1,owner,dialogProc,99)==-1); CHECK(GetLastError()==ERROR_CALL_NOT_IMPLEMENTED); ++count;
    }
    CHECK(destroy(guest));
    SetLastError(0xdead); CHECK(!end(guest,0)); CHECK(GetLastError()==ERROR_INVALID_WINDOW_HANDLE); ++count;
    CHECK(!callbacks);
    std::printf("PASS: %u dialog failure cases (11 Wine comparisons), no callback or host resource forwarding\n",count);
}
