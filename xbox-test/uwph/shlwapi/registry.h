#pragma once
#include <windows.h>

namespace shlwapi_core {
// Wine 10 shlwapi.spec forwards SHDeleteKeyW to shcore; shcore/main.c
// delegates to RegDeleteTreeW. The DWORD result is a registry error code,
// not a BOOL or HRESULT. No guest registry service exists in this backend.
// Never forward guest HKEY values or subkey names to the launcher's registry.
// Preserve thread last error: the failure is reported through the return value.
inline DWORD WINAPI delete_key(HKEY, const WCHAR *) {
    return ERROR_CALL_NOT_IMPLEMENTED;
}
}
