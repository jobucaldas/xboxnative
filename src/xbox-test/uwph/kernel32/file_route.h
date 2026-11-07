#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include "pipe_route.h"
namespace kernel32_core {
inline HANDLE WINAPI create_file_w(LPCWSTR path, DWORD access, DWORD share,
        LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE template_file) {
    CREATEFILE2_EXTENDED_PARAMETERS parameters{};
    parameters.dwSize = sizeof(parameters);
    parameters.dwFileAttributes = flags & 0xffff;
    parameters.dwFileFlags = flags & 0xfff00000;
    parameters.dwSecurityQosFlags = flags & SECURITY_VALID_SQOS_FLAGS;
    parameters.lpSecurityAttributes = security;
    parameters.hTemplateFile = template_file;
    std::wstring local = local_pipe(path);   // a pipe client finds a guest pipe where its creator put it
    return CreateFile2(local.empty() ? path : local.c_str(), access, share, disposition, &parameters);
}
inline HANDLE WINAPI create_file_a(LPCSTR path, DWORD access, DWORD share,
        LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE template_file) {
    if (!path) return create_file_w(nullptr, access, share, security, disposition, flags, template_file);
    // CreateFileA follows the process file API encoding, which can be OEM.
    using Encoding = BOOL (WINAPI*)();
    static Encoding encoding = []() -> Encoding {
        HMODULE module = LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto p = module ? GetProcAddress(module, "AreFileApisANSI") : nullptr;
        return reinterpret_cast<Encoding>(reinterpret_cast<uintptr_t>(p));
    }();
    if (!encoding) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return INVALID_HANDLE_VALUE; }
    UINT codepage = encoding() ? CP_ACP : CP_OEMCP;
    int size = MultiByteToWideChar(codepage, 0, path, -1, nullptr, 0);
    if (!size) return INVALID_HANDLE_VALUE;
    std::wstring wide(size, L'\0');
    if (!MultiByteToWideChar(codepage, 0, path, -1, &wide[0], size)) return INVALID_HANDLE_VALUE;
    return create_file_w(wide.c_str(), access, share, security, disposition, flags, template_file);
}
inline DWORD WINAPI file_size(HANDLE file, LPDWORD high) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) return INVALID_FILE_SIZE;
    if (high) *high = size.HighPart;
    if (size.LowPart == INVALID_FILE_SIZE) SetLastError(ERROR_SUCCESS);
    return size.LowPart;
}

// The runtime installs the guest's native current directory. Files opened by
// these routes are OS handles; access remains subject to AppContainer policy.
// No path sandbox escape or policy bypass is attempted.
inline uintptr_t resolve_file(const char* dll, const char* name) {
    if (!dll || !name) return 0;
    bool kernel = !_stricmp(dll, "kernel32.dll") || !_stricmp(dll, "kernelbase.dll");
    bool file = !_stricmp(dll, "api-ms-win-core-file-l1-1-0.dll") ||
                !_stricmp(dll, "api-ms-win-core-file-l1-2-0.dll");
    bool io = !_stricmp(dll, "api-ms-win-core-io-l1-1-0.dll");
    if (!kernel && !file && !io) return 0;
#define K32_FILE(symbol) if (!strcmp(name, #symbol)) return reinterpret_cast<uintptr_t>(&::symbol)
    if (kernel || file) { if (!strcmp(name, "CreateFileA")) return reinterpret_cast<uintptr_t>(&create_file_a); }
    if (kernel || file) { if (!strcmp(name, "CreateFileW")) return reinterpret_cast<uintptr_t>(&create_file_w); }
    if (kernel || file) { K32_FILE(ReadFile); }
    if (kernel || file) { K32_FILE(WriteFile); }
    if (kernel || file) { K32_FILE(FlushFileBuffers); }
    if (kernel || file) { K32_FILE(SetFilePointer); }
    if (kernel || file) { K32_FILE(SetFilePointerEx); }
    if (kernel || file) { K32_FILE(SetEndOfFile); }
    if (kernel || file) { if (!strcmp(name, "GetFileSize")) return reinterpret_cast<uintptr_t>(&file_size); }
    if (kernel || file) { K32_FILE(GetFileSizeEx); }
    if (kernel || file) { K32_FILE(GetFileType); }
    if (kernel || file) { K32_FILE(GetFileInformationByHandle); }
    if (kernel || file) { K32_FILE(GetFileInformationByHandleEx); }
    if (kernel || file) { K32_FILE(SetFileInformationByHandle); }
    if (kernel || file) { K32_FILE(GetFileTime); }
    if (kernel || file) { K32_FILE(SetFileTime); }
    if (kernel || file) { K32_FILE(GetFileAttributesW); }
    if (kernel || file) { K32_FILE(GetFileAttributesExW); }
    if (kernel || file) { K32_FILE(SetFileAttributesW); }
    if (kernel || file) { K32_FILE(DeleteFileW); }
    if (kernel || file) { K32_FILE(CreateDirectoryW); }
    if (kernel || file) { K32_FILE(RemoveDirectoryW); }
    if (kernel || file) { K32_FILE(FindFirstFileExW); }
    if (kernel || file) { K32_FILE(FindFirstFileW); }
    if (kernel || file) { K32_FILE(FindNextFileW); }
    if (kernel || file) { K32_FILE(FindClose); }
    if (kernel || file) { K32_FILE(GetFileAttributesA); }
    if (kernel || file) { K32_FILE(GetDiskFreeSpaceExW); }
    if (kernel || file) { K32_FILE(GetDriveTypeW); }
    if (kernel || file) { K32_FILE(GetLogicalDrives); }
    if (kernel || file) { K32_FILE(GetLongPathNameW); }
    if (kernel || file) { K32_FILE(CopyFileW); }
    if (kernel || file) { K32_FILE(CopyFileExW); }
    if (kernel || file) { K32_FILE(CopyFile2); }
    if (kernel || file) { K32_FILE(MoveFileExW); }
    if (kernel || file) { K32_FILE(ReplaceFileW); }
    if (kernel || io) { K32_FILE(GetOverlappedResult); }
    if (kernel || io) { K32_FILE(CancelIo); }
    if (kernel || io) { K32_FILE(CancelIoEx); }
    if (kernel) { K32_FILE(CreatePipe); }
    if (kernel) { K32_FILE(PeekNamedPipe); }
    if(kernel && !strcmp(name,"DeleteVolumeMountPointW")) {
        using Remove=BOOL (WINAPI*)(LPCWSTR);
        HMODULE module=GetModuleHandleW(L"kernel32.dll");
        if(!module)return 0;
        static Remove remove=reinterpret_cast<Remove>(reinterpret_cast<uintptr_t>(GetProcAddress(module,"DeleteVolumeMountPointW")));
        return reinterpret_cast<uintptr_t>(remove);
    }
#undef K32_FILE
    return 0;
}
}
