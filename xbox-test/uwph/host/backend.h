#pragma once
#include <windows.h>
#include <stdint.h>
// Private uwph ABI v1, NOT Wine's version-specific ntdll opcode numbering.
enum class Op : unsigned {
 Alloc, Protect, Free, Query, Flush, CreateFile, Read, Write, Seek, Size,
 FlushFile, DeleteFile, MoveFile, Attributes, CreateDir, RemoveDir, Close,
 Thread, ThreadId, ProcessId, Event, SetEvent, ResetEvent, Wait, Mutex,
 ReleaseMutex, Semaphore, ReleaseSemaphore, TlsAlloc, TlsSet, TlsGet, TlsFree,
 Counter, Frequency, Tick, Sleep, Process, Duplicate, WaitMultiple, Count
};
struct Call { uintptr_t a[6]{}; uintptr_t result{}; DWORD error{}; };
using Dispatch = LONG (WINAPI*)(uint64_t, unsigned, void*);
extern "C" uint64_t __wine_unixlib_handle;
extern "C" Dispatch __wine_unix_call_dispatcher;
inline uintptr_t host(Op op, Call& c) {
 __wine_unix_call_dispatcher(__wine_unixlib_handle, unsigned(op), &c);
 SetLastError(c.error); return c.result;
}
