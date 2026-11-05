#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "handles_route.h"
#include "memory_route.h"
#include "file_route.h"
#include "nls_time_route.h"
#include "process_route.h"
#include "slist_route.h"
#include "exception_route.h"
#include "system_route.h"
#include "thread_query_route.h"
#include "console_route.h"
#include "process_attributes_route.h"
#include "winrt_route.h"
#include "process_creation_route.h"
#include "snapshot_route.h"

namespace kernel32_core {
// Guest execution uses native Windows threads and native synchronization
// objects. These documented, typed APIs operate on exactly those objects;
// no guest handle or callback translation is required. Loader/thread entry
// interception in play_runtime retains precedence over this table.
inline uintptr_t resolve(const char* dll, const char* name) {
    if (!dll || !name) return 0;
    if (auto p = resolve_handles(dll, name)) return p;
    if (auto p = resolve_memory(dll, name)) return p;
    if (auto p = resolve_file(dll, name)) return p;
    if (auto p = resolve_nls_time(dll, name)) return p;
    if (auto p = resolve_process(dll, name)) return p;
    if (auto p = resolve_slist(dll, name)) return p;
    if (auto p = resolve_exception(dll, name)) return p;
    if (auto p = resolve_system(dll, name)) return p;
    if (auto p = resolve_thread_query(dll, name)) return p;
    if (auto p = resolve_console(dll, name)) return p;
    if (auto p = resolve_process_attributes(dll, name)) return p;
    if (auto p = resolve_winrt(dll, name)) return p;
    if (auto p = resolve_process_creation(dll, name)) return p;
    if (auto p = resolve_snapshot(dll, name)) return p;
    bool kernel = !_stricmp(dll, "kernel32.dll") || !_stricmp(dll, "kernelbase.dll");
    bool synch = !_stricmp(dll, "api-ms-win-core-synch-l1-1-0.dll") ||
                 !_stricmp(dll, "api-ms-win-core-synch-l1-2-0.dll");
    bool local = !_stricmp(dll, "api-ms-win-core-fibers-l1-1-0.dll") ||
                 !_stricmp(dll, "api-ms-win-core-fibers-l1-1-1.dll");
    bool thread = !_stricmp(dll, "api-ms-win-core-processthreads-l1-1-0.dll") ||
                  !_stricmp(dll, "api-ms-win-core-processthreads-l1-1-1.dll");
    if (!kernel && !synch && !local && !thread) return 0;
#define K32_NATIVE(symbol) if (!strcmp(name, #symbol)) return reinterpret_cast<uintptr_t>(&::symbol)
    if (kernel || thread) { K32_NATIVE(TlsAlloc); }
    if (kernel || thread) { K32_NATIVE(TlsFree); }
    if (kernel || thread) { K32_NATIVE(TlsGetValue); }
    if (kernel || thread) { K32_NATIVE(TlsSetValue); }
    if (kernel || local) { K32_NATIVE(FlsAlloc); }
    if (kernel || local) { K32_NATIVE(FlsFree); }
    if (kernel || local) { K32_NATIVE(FlsGetValue); }
    if (kernel || local) { K32_NATIVE(FlsSetValue); }
    if (kernel || synch) { K32_NATIVE(InitializeSRWLock); }
    if (kernel || synch) { K32_NATIVE(AcquireSRWLockExclusive); }
    if (kernel || synch) { K32_NATIVE(AcquireSRWLockShared); }
    if (kernel || synch) { K32_NATIVE(ReleaseSRWLockExclusive); }
    if (kernel || synch) { K32_NATIVE(ReleaseSRWLockShared); }
    if (kernel || synch) { K32_NATIVE(TryAcquireSRWLockExclusive); }
    if (kernel || synch) { K32_NATIVE(TryAcquireSRWLockShared); }
    if (kernel || synch) { K32_NATIVE(InitializeConditionVariable); }
    if (kernel || synch) { K32_NATIVE(SleepConditionVariableCS); }
    if (kernel || synch) { K32_NATIVE(SleepConditionVariableSRW); }
    if (kernel || synch) { K32_NATIVE(WakeConditionVariable); }
    if (kernel || synch) { K32_NATIVE(WakeAllConditionVariable); }
    if (kernel || synch) { K32_NATIVE(InitOnceInitialize); }
    if (kernel || synch) { K32_NATIVE(InitOnceBeginInitialize); }
    if (kernel || synch) { K32_NATIVE(InitOnceComplete); }
    if (kernel || synch) { K32_NATIVE(InitOnceExecuteOnce); }
    if (kernel || synch) { K32_NATIVE(WaitOnAddress); }
    if (kernel || synch) { K32_NATIVE(WakeByAddressAll); }
    if (kernel || synch) { K32_NATIVE(WakeByAddressSingle); }
#undef K32_NATIVE
    return 0;
}
}
