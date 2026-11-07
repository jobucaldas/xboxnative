// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
namespace kernel32_core {
// An AppContainer process may name pipes only under \\.\pipe\LOCAL\ (its own
// namespace); a bare \\.\pipe\<name> is refused on the console. A guest's
// \\.\pipe\<name> is mapped to \\.\pipe\LOCAL\<name> for the pipe's creator and
// its clients alike, so the pipe still connects inside the game. Other paths
// and names already under LOCAL\ are unchanged (empty result).
inline std::wstring local_pipe(LPCWSTR path) {
    static const wchar_t prefix[] = L"\\\\.\\pipe\\";
    const size_t n = wcslen(prefix);
    if (!path || _wcsnicmp(path, prefix, n) || !path[n] || !_wcsnicmp(path + n, L"LOCAL\\", 6)) return {};
    return std::wstring(prefix) + L"LOCAL\\" + (path + n);
}
inline std::wstring ansi_path(LPCSTR path) {
    int size = path ? MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0) : 0;
    if (size <= 0) return {};
    std::wstring wide(size_t(size), L'\0');
    MultiByteToWideChar(CP_ACP, 0, path, -1, &wide[0], size);
    wide.resize(size_t(size) - 1);
    return wide;
}
template<class F> inline F pipe_api(const char* name) {
    static HMODULE module = LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return reinterpret_cast<F>(reinterpret_cast<uintptr_t>(module ? GetProcAddress(module, name) : nullptr));
}
inline HANDLE WINAPI create_named_pipe_w(LPCWSTR name, DWORD open, DWORD mode, DWORD instances,
        DWORD out, DWORD in, DWORD timeout, LPSECURITY_ATTRIBUTES security) {
    using Fn = HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPSECURITY_ATTRIBUTES);
    static Fn fn = pipe_api<Fn>("CreateNamedPipeW");
    if (!fn) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return INVALID_HANDLE_VALUE; }
    std::wstring local = local_pipe(name);
    return fn(local.empty() ? name : local.c_str(), open, mode, instances, out, in, timeout, security);
}
inline HANDLE WINAPI create_named_pipe_a(LPCSTR name, DWORD open, DWORD mode, DWORD instances,
        DWORD out, DWORD in, DWORD timeout, LPSECURITY_ATTRIBUTES security) {
    std::wstring wide = ansi_path(name);
    if (name && wide.empty()) { SetLastError(ERROR_INVALID_NAME); return INVALID_HANDLE_VALUE; }
    return create_named_pipe_w(name ? wide.c_str() : nullptr, open, mode, instances, out, in, timeout, security);
}
inline BOOL WINAPI wait_named_pipe_w(LPCWSTR name, DWORD timeout) {
    using Fn = BOOL (WINAPI*)(LPCWSTR, DWORD);
    static Fn fn = pipe_api<Fn>("WaitNamedPipeW");
    if (!fn) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE; }
    std::wstring local = local_pipe(name);
    return fn(local.empty() ? name : local.c_str(), timeout);
}
inline BOOL WINAPI wait_named_pipe_a(LPCSTR name, DWORD timeout) {
    std::wstring wide = ansi_path(name);
    if (name && wide.empty()) { SetLastError(ERROR_INVALID_NAME); return FALSE; }
    return wait_named_pipe_w(name ? wide.c_str() : nullptr, timeout);
}
inline uintptr_t resolve_pipe(const char* dll, const char* name) {
    if (!dll || !name) return 0;
    bool kernel = !_stricmp(dll, "kernel32.dll") || !_stricmp(dll, "kernelbase.dll") ||
                  !_strnicmp(dll, "api-ms-win-core-namedpipe-l1-", 29);
    if (!kernel) return 0;
    if (!strcmp(name, "CreateNamedPipeW")) return reinterpret_cast<uintptr_t>(&create_named_pipe_w);
    if (!strcmp(name, "CreateNamedPipeA")) return reinterpret_cast<uintptr_t>(&create_named_pipe_a);
    if (!strcmp(name, "WaitNamedPipeW")) return reinterpret_cast<uintptr_t>(&wait_named_pipe_w);
    if (!strcmp(name, "WaitNamedPipeA")) return reinterpret_cast<uintptr_t>(&wait_named_pipe_a);
    return 0;
}
}
