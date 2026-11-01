#pragma once
// No Window record has an IME context. Wine 10 imm32/imm.c and
// win32u/imm.c define these detach/no-context paths. Never forward guest
// handles to the host IME, and never manufacture a default context.
static BOOL ime_associate(HWND hwnd, HANDLE context, DWORD flags) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto w = window(hwnd);
    if (!w || w == desktop) SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    // NtUserAssociateInputContext accepts these flags individually, not a mask.
    if (flags != 0 && flags != 0x10 && flags != 0x20) return FALSE;
    if (flags == 0x10) { // IACE_DEFAULT requires a real default input context.
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }
    // Querying an unknown context fails after the initial window-context
    // lookup, and therefore overrides any invalid-window error.
    if (context) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!w || w == desktop) return FALSE;
    return TRUE;
}
extern "C" HANDLE WINAPI c_ImmAssociateContext(HWND hwnd, HANDLE context) {
    ime_associate(hwnd, context, 0);
    return nullptr; // Previous context is always absent, even on failure.
}
extern "C" BOOL WINAPI c_ImmAssociateContextEx(HWND hwnd, HANDLE context, DWORD flags) {
    if (!hwnd) return FALSE;
    // Wine's IACE_CHILDREN branch ignores individual association failures and
    // returns TRUE. All descendants already have no context in this backend.
    if (flags == 1) return TRUE;
    return ime_associate(hwnd, context, flags);
}
