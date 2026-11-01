#pragma once
// Wine 10 user32.spec passes POINT by value (one int64 on x64).
// win32u/window.c returns FALSE immediately when VK_LBUTTON is not down,
// without changing last error or capture. Preserve that useful no-drag path.
// Detecting a held-button drag requires the message loop owned by another
// lane. Fail explicitly before taking capture rather than reporting a drag
// or consuming messages from the launcher's CoreWindow.
extern "C" BOOL WINAPI c_DragDetect(HWND, POINT) {
    if (c_GetKeyState(VK_LBUTTON) & 0x8000)
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
