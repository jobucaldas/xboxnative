#pragma once
#include <windows.h>

namespace shell32_core {
// Shell file operations are excluded from UWP SDK declarations. Preserve the
// desktop ABI locally without enabling desktop API partitions.
struct FileOperationRequest {
    HWND hwnd;
    UINT wFunc;
    const WCHAR *pFrom;
    const WCHAR *pTo;
    WORD fFlags;
    BOOL fAnyOperationsAborted;
    void *hNameMappings;
    const WCHAR *lpszProgressTitle;
};
// ABI: Wine 10 shell32.spec; NULL request: shell32/shlfileop.c.
// Guest filesystem operations need the guest path/handle backend. Until it
// exists, never send guest paths to host shell APIs or report a successful
// copy, move, rename or delete. Nonzero is SHFileOperation's failure contract.
inline int WINAPI file_operation(FileOperationRequest *request) {
    if (!request) return ERROR_INVALID_PARAMETER;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return ERROR_CALL_NOT_IMPLEMENTED;
}
}
