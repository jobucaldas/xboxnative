#pragma once
#include <windows.h>
#include <imm.h>
#include <cstring>
#include <cstdint>
#include <atomic>

// No IME/input contexts exist in this backend. Do not pass guest HWND/HIMC
// values to host IMM32. These are the no-context paths in Wine 10 imm.c;
// signatures follow imm32.spec. Context creation remains unsupported.
namespace imm32_core {
using IsWindow = BOOL (WINAPI*)(HWND);
inline std::atomic<IsWindow> window_validator{nullptr};
inline bool valid_window(HWND window) {
    const auto validator = window_validator.load();
    if (!window || (validator && !validator(window))) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    if (!validator) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }
    return true;
}
// Every window in this backend starts with no context. Detaching that absent
// context changes no state. Never accept foreign HIMCs or create a default IME.
inline HIMC WINAPI associate_context(HWND window, HIMC context) {
    if (!valid_window(window)) return nullptr;
    if (context) SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
}
inline BOOL WINAPI associate_context_ex(HWND window, HIMC context, DWORD flags) {
    // Wine imm.c returns immediately for NULL HWND, preserving last error.
    if (!window) return FALSE;
    if (flags != 0 && flags != IACE_CHILDREN && flags != IACE_DEFAULT &&
        flags != IACE_IGNORENOCONTEXT) return FALSE;
    if (!valid_window(window)) return FALSE;
    if (flags == IACE_DEFAULT) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (context) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}
inline HIMC WINAPI get_context(HWND window) {
    valid_window(window);
    return nullptr;
}
// No guest HIMC can be issued by this backend. Preserve Wine's null-context
// behavior, but reject foreign non-null values without touching caller buffers.
inline void reject_foreign_context(HIMC context) {
    if (context) SetLastError(ERROR_INVALID_HANDLE);
}
inline LONG WINAPI get_composition(HIMC context, DWORD, void*, DWORD) {
    reject_foreign_context(context);
    return 0;
}
inline BOOL WINAPI get_conversion(HIMC context, DWORD*, DWORD*) {
    reject_foreign_context(context);
    return FALSE;
}
inline BOOL WINAPI set_composition(HIMC context, DWORD, const void*, DWORD, const void*, DWORD) {
    reject_foreign_context(context);
    return FALSE;
}
inline BOOL WINAPI notify(HIMC context, DWORD, DWORD, DWORD) {
    SetLastError(context ? ERROR_INVALID_HANDLE : ERROR_SUCCESS);
    return FALSE;
}
// Candidate/composition window state belongs to a context; none exists.
inline DWORD WINAPI get_candidate_list(HIMC context, DWORD, void*, DWORD) {
    reject_foreign_context(context);
    return 0;
}
inline BOOL WINAPI get_composition_font(HIMC context, void*) {
    reject_foreign_context(context);
    return FALSE;
}
inline BOOL WINAPI set_composition_window(HIMC context, const void*) {
    reject_foreign_context(context);
    return FALSE;
}
inline BOOL WINAPI set_candidate_window(HIMC context, const void*) {
    reject_foreign_context(context);
    return FALSE;
}
// No keyboard layout here is an IME (Wine: zero for a non-IME HKL).
inline UINT WINAPI get_ime_file_name_a(HKL, LPSTR, UINT) { return 0; }
// Wine releases no resource here, even for a null context or window.
inline BOOL WINAPI release_context(HWND, HIMC) { return TRUE; }
inline uintptr_t resolve(const char* dll, const char* name, IsWindow validator = nullptr) {
    if (!dll || !name || _stricmp(dll, "imm32.dll")) return 0;
    if (validator) window_validator = validator;
    if (!strcmp(name, "ImmAssociateContext")) return reinterpret_cast<uintptr_t>(&associate_context);
    if (!strcmp(name, "ImmAssociateContextEx")) return reinterpret_cast<uintptr_t>(&associate_context_ex);
    if (!strcmp(name, "ImmGetContext")) return reinterpret_cast<uintptr_t>(&get_context);
    if (!strcmp(name, "ImmGetCompositionStringW")) return reinterpret_cast<uintptr_t>(&get_composition);
    if (!strcmp(name, "ImmGetConversionStatus")) return reinterpret_cast<uintptr_t>(&get_conversion);
    if (!strcmp(name, "ImmSetCompositionStringW")) return reinterpret_cast<uintptr_t>(&set_composition);
    if (!strcmp(name, "ImmNotifyIME")) return reinterpret_cast<uintptr_t>(&notify);
    if (!strcmp(name, "ImmReleaseContext")) return reinterpret_cast<uintptr_t>(&release_context);
    if (!strcmp(name, "ImmGetCandidateListW")) return reinterpret_cast<uintptr_t>(&get_candidate_list);
    if (!strcmp(name, "ImmGetCompositionFontW")) return reinterpret_cast<uintptr_t>(&get_composition_font);
    if (!strcmp(name, "ImmSetCompositionWindow")) return reinterpret_cast<uintptr_t>(&set_composition_window);
    if (!strcmp(name, "ImmSetCandidateWindow")) return reinterpret_cast<uintptr_t>(&set_candidate_window);
    if (!strcmp(name, "ImmGetIMEFileNameA")) return reinterpret_cast<uintptr_t>(&get_ime_file_name_a);
    return 0;
}
}
