#pragma once
#include <windows.h>

namespace shell32_core {
// Wine 10 shell32.spec: ShellExecuteW(long wstr wstr wstr wstr long).
// shlexec.c returns hInstApp <= 32 on failure; SE_ERR_NOASSOC is 31.
// This backend has no guest shell associations or shell execution service.
// Never forward guest paths, protocol URLs, verbs or HWNDs to the host shell.
// This is an explicit unavailable-service result, not full ShellExecute logic.
inline HINSTANCE WINAPI execute(HWND, const WCHAR *, const WCHAR *,
                                const WCHAR *, const WCHAR *, int) {
    SetLastError(ERROR_NO_ASSOCIATION);
    return reinterpret_cast<HINSTANCE>(INT_PTR(31));
}
}
