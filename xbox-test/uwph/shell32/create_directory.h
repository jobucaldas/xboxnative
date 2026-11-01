#pragma once
#include <windows.h>

namespace shell32_core {
// ABI and lexical rejection: Wine 10 shell32/shlfileop.c and
// kernelbase/path.c (PathIsRelativeW). Absolute paths require a guest
// filesystem backend; never create directories in the launcher's namespace.
inline int WINAPI create_directory(HWND, const WCHAR *path, const SECURITY_ATTRIBUTES *) {
    if (!path || !*path || (*path != L'\\' && path[1] != L':')) {
        SetLastError(ERROR_BAD_PATHNAME);
        return ERROR_BAD_PATHNAME;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return ERROR_CALL_NOT_IMPLEMENTED;
}
}
