#pragma once
#include <windows.h>

namespace shell32_core {
// ABI: Wine 10 shell32.spec and shell32/shellpath.c. Folder discovery needs
// a guest profile/registry backend. Never expose the launcher's host profile
// or claim a guest folder exists. E_NOTIMPL explicitly reports that absence.
// Clear outputs so callers cannot consume stale paths after failure.
inline HRESULT WINAPI folder_path(HWND, int, HANDLE, DWORD, WCHAR *path) {
    if (path) path[0] = 0;
    return E_NOTIMPL;
}
inline HRESULT WINAPI known_folder_path(const GUID &, DWORD, HANDLE, WCHAR **path) {
    if (!path) return E_INVALIDARG;
    *path = nullptr;
    return E_NOTIMPL;
}
}
