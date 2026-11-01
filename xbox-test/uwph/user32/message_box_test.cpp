#include <windows.h>
#include <cstdio>
#include <type_traits>
#include <initializer_list>
int main() {
    HMODULE module = LoadLibraryW(L"uwph-u32c.dll");
    if (!module) return 1;
    auto message = reinterpret_cast<decltype(&MessageBoxA)>(GetProcAddress(module, "MessageBoxA"));
    if (!message) return 2;
    unsigned count = 0;
    // Every button layout must fail without selecting a button. Poison guest
    // strings and HWNDs verify that no host dialog/resource access occurs.
    for (UINT flags : std::initializer_list<UINT>{MB_OK, MB_OKCANCEL, MB_ABORTRETRYIGNORE, MB_YESNOCANCEL,
                       MB_YESNO, MB_RETRYCANCEL, MB_CANCELTRYCONTINUE,
                       UINT(MB_OK | MB_HELP | MB_SYSTEMMODAL)}) {
        for (HWND owner : {HWND(nullptr), reinterpret_cast<HWND>(1)}) {
            SetLastError(0xdead);
            if (message(owner, reinterpret_cast<LPCSTR>(1),
                        reinterpret_cast<LPCSTR>(1), flags) != 0 ||
                GetLastError() != ERROR_CALL_NOT_IMPLEMENTED) return 3;
            ++count;
        }
    }
    SetLastError(0xdead);
    if (message(nullptr, nullptr, nullptr, MB_OK) ||
        GetLastError() != ERROR_CALL_NOT_IMPLEMENTED) return 4;
    std::printf("PASS: %u MessageBoxA failure cases; no synthetic button or host dialog\n", count + 1);
    FreeLibrary(module);
}
