#pragma once
// Wine user32/dialog.c: DialogBoxParamA/W reject an invalid non-NULL owner
// before looking up a template. No guest HINSTANCE is passed to host resources.
extern "C" INT_PTR WINAPI c_DialogBoxParamW(HINSTANCE, LPCWSTR, HWND owner,
                                           DLGPROC, LPARAM) {
    // Guest handles are record identities, not native USER handle-table
    // indices. Preserve the registry's invalid-window error for every unknown
    // owner, including small integers and all-bits-set pseudo handles.
    if (owner && !c_IsWindow(owner)) return 0;
    // There is no dialog template/loop backend yet. -1 is the documented
    // creation-failure result; never report IDOK without displaying a dialog.
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return -1;
}
extern "C" INT_PTR WINAPI c_DialogBoxParamA(HINSTANCE, LPCSTR, HWND owner,
                                           DLGPROC, LPARAM) {
    return c_DialogBoxParamW(nullptr, nullptr, owner, nullptr, 0);
}
extern "C" BOOL WINAPI c_EndDialog(HWND hwnd, INT_PTR) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    // Wine DIALOG_get_info(hwnd, FALSE) never creates dialog state. All windows
    // in this registry are ordinary windows; none has acquired DIALOGINFO.
    // Invalid HWND sets ERROR_INVALID_WINDOW_HANDLE; ordinary HWND preserves it.
    if (!window(hwnd)) return FALSE;
    return FALSE;
}
