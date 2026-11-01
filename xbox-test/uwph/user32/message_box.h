#pragma once
// Wine user32.spec defines the four-argument MessageBoxA ABI. Wine msgbox.c
// builds a dialog and enters its modal loop. This backend has neither a dialog
// template backend nor that loop. Use the Windows creation-failure result (0),
// with an explicit unsupported error, instead of synthesizing IDOK/IDCANCEL.
// Do not read guest strings or pass guest HWNDs to the launcher's USER32.
extern "C" int WINAPI c_MessageBoxA(HWND, LPCSTR, LPCSTR, UINT) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}
