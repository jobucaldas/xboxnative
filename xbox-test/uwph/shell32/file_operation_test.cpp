#include "route.h"
#include <shellapi.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
static void check(bool ok) { if (!ok) std::abort(); }
int main() {
    using Request = shell32_core::FileOperationRequest;
    static_assert(sizeof(Request) == sizeof(SHFILEOPSTRUCTW));
#define ABI_FIELD(field) static_assert(offsetof(Request, field) == offsetof(SHFILEOPSTRUCTW, field))
    ABI_FIELD(hwnd); ABI_FIELD(wFunc); ABI_FIELD(pFrom); ABI_FIELD(pTo);
    ABI_FIELD(fFlags); ABI_FIELD(fAnyOperationsAborted); ABI_FIELD(hNameMappings);
    ABI_FIELD(lpszProgressTitle);
#undef ABI_FIELD
    using Operation = int (WINAPI *)(Request *);
    auto operation = reinterpret_cast<Operation>(shell32_core::resolve("SHELL32.DLL", "SHFileOperationW"));
    check(operation && !shell32_core::resolve("kernel32.dll", "SHFileOperationW"));
    check(!shell32_core::resolve("shell32.dll", "SHFileOperationA"));
    auto host = reinterpret_cast<Operation>(GetProcAddress(LoadLibraryW(L"shell32.dll"), "SHFileOperationW"));
    check(host != nullptr);
    SetLastError(0x12345678);
    int expected = host(nullptr);
    DWORD error = GetLastError();
    SetLastError(0x12345678);
    check(operation(nullptr) == expected && GetLastError() == error);
    for (UINT function : {UINT(FO_COPY), UINT(FO_MOVE), UINT(FO_DELETE), UINT(FO_RENAME), UINT(0), UINT(0xffffffff)}) {
        for (BOOL aborted : {FALSE, TRUE}) {
            Request request{};
            request.hwnd = reinterpret_cast<HWND>(0x1234);
            request.wFunc = function;
            // Poison paths prove isolation: no parsing or host file operation.
            request.pFrom = reinterpret_cast<const WCHAR *>(1);
            request.pTo = reinterpret_cast<const WCHAR *>(1);
            request.fFlags = 0xffff;
            request.fAnyOperationsAborted = aborted;
            request.hNameMappings = reinterpret_cast<void *>(0x5678);
            request.lpszProgressTitle = reinterpret_cast<const WCHAR *>(1);
            auto before = request;
            SetLastError(0x12345678);
            check(operation(&request) == ERROR_CALL_NOT_IMPLEMENTED);
            check(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
            check(!memcmp(&before, &request, sizeof(request)));
        }
    }
    puts("PASS: 12 unsupported file-operation isolation cases; NULL Wine differential; resolver ABI");
}
