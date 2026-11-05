#pragma once
#include <windows.h>
#include <imm.h>
#include <cstring>
#include <cstdint>

// No IME/input contexts exist in this backend. Do not pass guest HWND/HIMC
// values to host IMM32. These are the no-context paths in Wine 10 imm.c;
// signatures follow imm32.spec. Association/creation remain unresolved.
namespace imm32_core {
inline HIMC WINAPI get_context(HWND window) {
    if (!window) SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return nullptr;
}
inline LONG WINAPI get_composition(HIMC, DWORD, void*, DWORD) { return 0; }
inline BOOL WINAPI get_conversion(HIMC, DWORD*, DWORD*) { return FALSE; }
inline BOOL WINAPI set_composition(HIMC, DWORD, const void*, DWORD, const void*, DWORD) {
    return FALSE;
}
inline BOOL WINAPI notify(HIMC context, DWORD, DWORD, DWORD) {
    if (!context) SetLastError(ERROR_SUCCESS);
    return FALSE;
}
// Wine releases no resource here, even for a null context or window.
inline BOOL WINAPI release_context(HWND, HIMC) { return TRUE; }
inline uintptr_t resolve(const char* dll, const char* name) {
    if (!dll || !name || _stricmp(dll, "imm32.dll")) return 0;
    if (!strcmp(name, "ImmGetContext")) return reinterpret_cast<uintptr_t>(&get_context);
    if (!strcmp(name, "ImmGetCompositionStringW")) return reinterpret_cast<uintptr_t>(&get_composition);
    if (!strcmp(name, "ImmGetConversionStatus")) return reinterpret_cast<uintptr_t>(&get_conversion);
    if (!strcmp(name, "ImmSetCompositionStringW")) return reinterpret_cast<uintptr_t>(&set_composition);
    if (!strcmp(name, "ImmNotifyIME")) return reinterpret_cast<uintptr_t>(&notify);
    if (!strcmp(name, "ImmReleaseContext")) return reinterpret_cast<uintptr_t>(&release_context);
    return 0;
}
}
