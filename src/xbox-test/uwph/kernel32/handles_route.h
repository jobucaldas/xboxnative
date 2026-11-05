#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace kernel32_core {
// All handles created here are native handles; use the native wait/close
// family as a unit. In particular, do not return guest records as HANDLEs.
inline uintptr_t resolve_handles(const char* dll, const char* name) {
    if (!dll || !name) return 0;
    bool kernel = !_stricmp(dll, "kernel32.dll") || !_stricmp(dll, "kernelbase.dll");
    bool synch = !_stricmp(dll, "api-ms-win-core-synch-l1-1-0.dll") ||
                 !_stricmp(dll, "api-ms-win-core-synch-l1-2-0.dll");
    bool handle = !_stricmp(dll, "api-ms-win-core-handle-l1-1-0.dll");
    if (!kernel && !synch && !handle) return 0;
#define K32_HANDLE(symbol) if (!strcmp(name, #symbol)) return reinterpret_cast<uintptr_t>(&::symbol)
    if (kernel || handle) { K32_HANDLE(CloseHandle); }
    if (kernel || handle) { K32_HANDLE(DuplicateHandle); }
    if (kernel || synch) { K32_HANDLE(CreateEventA); }
    if (kernel || synch) { K32_HANDLE(CreateEventW); }
    if (kernel || synch) { K32_HANDLE(CreateEventExW); }
    if (kernel || synch) { K32_HANDLE(SetEvent); }
    if (kernel || synch) { K32_HANDLE(ResetEvent); }
    if (kernel || synch) { K32_HANDLE(CreateMutexA); }
    if (kernel || synch) { K32_HANDLE(ReleaseMutex); }
    if (kernel || synch) { K32_HANDLE(CreateSemaphoreExW); }
    if (kernel || synch) { K32_HANDLE(ReleaseSemaphore); }
    if (kernel || synch) { K32_HANDLE(WaitForSingleObject); }
    if (kernel || synch) { K32_HANDLE(WaitForSingleObjectEx); }
    if (kernel || synch) { K32_HANDLE(WaitForMultipleObjects); }
    if (kernel || synch) { K32_HANDLE(WaitForMultipleObjectsEx); }
    if (kernel || synch) { K32_HANDLE(CreateWaitableTimerA); }
    if (kernel || synch) { K32_HANDLE(CreateWaitableTimerExW); }
    if (kernel || synch) { K32_HANDLE(SetWaitableTimer); }
    if (kernel || synch) { K32_HANDLE(CancelWaitableTimer); }
    if (kernel || synch) { K32_HANDLE(Sleep); }
    if (kernel || synch) { K32_HANDLE(SleepEx); }
    if (kernel || synch) { K32_HANDLE(SwitchToThread); }
    if (kernel || synch) { K32_HANDLE(InitializeCriticalSection); }
    if (kernel || synch) { K32_HANDLE(InitializeCriticalSectionAndSpinCount); }
    if (kernel || synch) { K32_HANDLE(InitializeCriticalSectionEx); }
    if (kernel || synch) { K32_HANDLE(EnterCriticalSection); }
    if (kernel || synch) { K32_HANDLE(LeaveCriticalSection); }
    if (kernel || synch) { K32_HANDLE(DeleteCriticalSection); }
    if (kernel || synch) { K32_HANDLE(SetCriticalSectionSpinCount); }
    if (kernel || synch) { K32_HANDLE(TryEnterCriticalSection); }
#undef K32_HANDLE
    return 0;
}
}
