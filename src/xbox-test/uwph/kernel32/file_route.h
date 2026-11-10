#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include "pipe_route.h"
#include "../hid/pad_registry.h"
namespace kernel32_core {
inline HANDLE WINAPI create_file_w(LPCWSTR path, DWORD access, DWORD share,
        LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE template_file) {
    // A device path the synthetic pad registry issued is that pad, not a file:
    // the console has no such device, and opening it as a file would fail.
    if (path && pad_hid::slot_from_path(path) >= 0) return pad_hid::open(path);
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

// ---- Synchronous WriteFile guard ------------------------------------------
// A guest write on a synchronous handle blocks inside NtWriteFile until the
// file system completes it. On the console Puyo Puyo Tetris 2 stayed there
// forever ("Saving replay...", 0% CPU) while one WriteFile of a large dirty
// range to its save file never returned; Windows and Wine complete the same
// call. The route therefore (1) splits big writes to disk files into bounded
// chunks, so one request never asks the file system for a huge transfer,
// (2) reports a write that stalls, and (3) cancels a stalled disk write with
// CancelSynchronousIo so the game gets ERROR_OPERATION_ABORTED (a failed
// save it can handle) instead of a frozen process. Overlapped writes and
// writes to non-disk handles (pipes, consoles) are never split or cancelled:
// those block legitimately while the peer is slow.
struct WriteStall {
    HANDLE file; DWORD size, done, elapsed_ms;
    bool disk, cancelled; DWORD cancel_error;   // cancel_error is meaningful when cancelled
};
struct WriteGuardConfig {
    DWORD chunk = 1u << 20;       // bytes per NtWriteFile on disk files; 0 = never split
    DWORD report_ms = 5000;       // a chunk this old is reported once
    DWORD cancel_ms = 60000;      // a disk chunk this old is cancelled; 0 = never cancel
    bool cancel_any = false;      // tests only: also cancel non-disk handles
    void (*hook)(const WriteStall&) = nullptr;
};
inline WriteGuardConfig& write_guard() { static WriteGuardConfig config; return config; }

namespace write_guard_detail {
struct Slot { bool used, cancelable, reported, cancelled; HANDLE thread, file; DWORD size, done; ULONGLONG start; };
struct Table {
    CRITICAL_SECTION lock; Slot slots[32]; bool started;
    Table() : slots(), started(false) { InitializeCriticalSection(&lock); }
};
inline Table& table() { static Table instance; return instance; }
// The watchdog cancels a writer thread's I/O, which needs a real handle to
// that thread; the pseudo handle only names the caller.
struct ThreadHandle {
    HANDLE handle = nullptr;
    ThreadHandle() {
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle, 0, FALSE, DUPLICATE_SAME_ACCESS))
            handle = nullptr;
    }
    ~ThreadHandle() { if (handle) CloseHandle(handle); }
};
inline HANDLE this_thread() { static thread_local ThreadHandle h; return h.handle; }
// CancelSynchronousIo is not in the app-partition import libraries; resolve it
// from the system kernel32 like AreFileApisANSI.
inline BOOL cancel_thread_io(HANDLE thread, DWORD* error) {
    using Cancel = BOOL (WINAPI*)(HANDLE);
    static Cancel cancel = []() -> Cancel {
        HMODULE module = LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto p = module ? GetProcAddress(module, "CancelSynchronousIo") : nullptr;
        return reinterpret_cast<Cancel>(reinterpret_cast<uintptr_t>(p));
    }();
    if (!cancel) { *error = ERROR_CALL_NOT_IMPLEMENTED; return FALSE; }
    BOOL ok = cancel(thread); *error = ok ? 0 : GetLastError(); return ok;
}
inline DWORD WINAPI watchdog(LPVOID) {
    Table& t = table();
    for (;;) {
        const WriteGuardConfig config = write_guard();
        DWORD period = 250;
        if (config.report_ms && config.report_ms / 4 < period) period = config.report_ms / 4 + 1;
        if (config.cancel_ms && config.cancel_ms / 4 < period) period = config.cancel_ms / 4 + 1;
        Sleep(period);
        WriteStall events[32]; int count = 0;
        EnterCriticalSection(&t.lock);
        ULONGLONG now = GetTickCount64();
        for (Slot& s : t.slots) {
            if (!s.used) continue;
            DWORD elapsed = DWORD(now - s.start);
            bool report = !s.reported && config.report_ms && elapsed >= config.report_ms;
            bool cancel = !s.cancelled && s.cancelable && s.thread && config.cancel_ms && elapsed >= config.cancel_ms;
            if (!report && !cancel) continue;
            WriteStall& e = events[count++];
            e = WriteStall{s.file, s.size, s.done, elapsed, s.cancelable, false, 0};
            if (report) s.reported = true;
            if (cancel) { s.cancelled = true; e.cancelled = true; cancel_thread_io(s.thread, &e.cancel_error); }
        }
        LeaveCriticalSection(&t.lock);
        if (config.hook) for (int i = 0; i < count; ++i) config.hook(events[i]);
    }
}
// Registers one in-flight chunk; -1 when the table is full (the write then runs unwatched).
inline int enter(HANDLE file, DWORD size, DWORD done, bool cancelable) {
    Table& t = table();
    int index = -1;
    EnterCriticalSection(&t.lock);
    for (int i = 0; i < 32 && index < 0; ++i) if (!t.slots[i].used) index = i;
    if (index >= 0) {
        t.slots[index] = Slot{true, cancelable, false, false, this_thread(), file, size, done, GetTickCount64()};
        if (!t.started) {
            HANDLE thread = CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr);
            if (thread) { CloseHandle(thread); t.started = true; }
        }
    }
    LeaveCriticalSection(&t.lock);
    return index;
}
inline void leave(int index) {
    if (index < 0) return;
    Table& t = table();
    EnterCriticalSection(&t.lock);
    t.slots[index].used = false;
    LeaveCriticalSection(&t.lock);
}
}

inline BOOL WINAPI write_file(HANDLE file, LPCVOID buffer, DWORD count, LPDWORD written, LPOVERLAPPED overlapped) {
    // The native call defines every case the guard does not own.
    if (overlapped || !written) return ::WriteFile(file, buffer, count, written, overlapped);
    const WriteGuardConfig config = write_guard();
    bool disk = GetFileType(file) == FILE_TYPE_DISK;
    const BYTE* data = static_cast<const BYTE*>(buffer);
    DWORD total = 0, error = 0;
    BOOL ok;
    for (;;) {
        DWORD size = count - total;
        if (disk && config.chunk && size > config.chunk) size = config.chunk;
        int slot = write_guard_detail::enter(file, count, total, disk || config.cancel_any);
        DWORD got = 0;
        ok = ::WriteFile(file, data + total, size, &got, nullptr);
        if (!ok) error = GetLastError();
        write_guard_detail::leave(slot);
        total += got;
        if (!ok || got != size || total >= count) break;
    }
    *written = total;
    if (!ok) SetLastError(error);
    return ok;
}

// A pad handle has no reports of its own: Play feeds the pad through XInput, so
// the read stays pending the way a HID device with nothing new to say does.
inline BOOL WINAPI read_file(HANDLE file, LPVOID buffer, DWORD count, LPDWORD read, LPOVERLAPPED overlapped) {
    if (pad_hid::slot_of(file) >= 0) return pad_hid::read(file, buffer, count, read, overlapped);
    return ::ReadFile(file, buffer, count, read, overlapped);
}
inline BOOL WINAPI get_overlapped_result(HANDLE file, LPOVERLAPPED overlapped, LPDWORD transferred, BOOL wait) {
    if (pad_hid::slot_of(file) >= 0) return pad_hid::overlapped_result(file, overlapped, transferred, wait);
    return ::GetOverlappedResult(file, overlapped, transferred, wait);
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
    if (kernel || file) { if (!strcmp(name, "ReadFile")) return reinterpret_cast<uintptr_t>(&read_file); }
    if (kernel || file) { if (!strcmp(name, "WriteFile")) return reinterpret_cast<uintptr_t>(&write_file); }
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
    if (kernel || io) { if (!strcmp(name, "GetOverlappedResult")) return reinterpret_cast<uintptr_t>(&get_overlapped_result); }
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
