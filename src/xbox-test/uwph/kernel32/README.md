# Kernel32 guest routes

The Play runtime executes guest code on native Windows threads. These routes
bind explicitly named, typed documented Windows APIs to the matching native
TLS slots, FLS callbacks, synchronization objects and kernel handles. They do
not dynamically forward arbitrary names or claim to implement a Wine server.
No synthetic handles or success results are introduced. OS failures and last
errors are returned unchanged. Loader and thread entry interception in
`play_runtime.h` retain precedence.

Run `WINEPREFIX=/tmp/k32x-wine ./test.sh` for off-device behavior regressions.
Tests cover thread-local isolation, thread-exit FLS callbacks, failed allocation
indices, SRW shared/exclusive ownership, condition wait timeout/reacquisition,
exclusive and shared wakeups, critical section contention/recursion/spin count,
one-time initialization contention and retry, event reset modes, duplicated
handle identity, wait-all/any, invalid handles, mutex recursion/ownership,
semaphore overflow, timer expiry/cancellation and alertable APC delivery.
Every bound API is called through its typed route. Tests do not contact Xbox.

Native export presence alone does not establish Xbox AppContainer support.
`research/k32x-todo.csv` distinguishes host-tested routes from pending behavior
audits and console verification. The launcher UWP build validates the actual
SDK imports. Real console acceptance remains a separate gate.

The file adapters translate CreateFileW to CreateFile2, convert CreateFileA
using the current ANSI/OEM file API encoding, and implement GetFileSize via
GetFileSizeEx. The tests compare disposition, flag and error behavior against
Wine's corresponding native APIs. HeapQueryInformation uses one exact typed
OS export because the UWP SDK omits its declaration; absence returns FALSE
with ERROR_CALL_NOT_IMPLEMENTED. Address waits link against synchronization
in MinGW; the UWP launcher links them with the existing SDK libraries.

The full owner run also covers pipe and named mapping identity, legacy locale
adapters, process/thread state, resources and mapped loader queries (through
`xbox-test/app/tests/play-runtime.sh`), WinRT strings/activation failures,
child-process inherited handles, native snapshots and direct SEH unwind.
`test-unwind.sh` uses the UWP crossbuild toolchain for MSVC termination handlers
and a COFF assembly frame. No Xbox access is performed by either host script.
The snapshot suite covers native threads/processes through typed guest routes;
mapped guest module lists remain unverified. The mapped-runtime regressions
cover module references/pins, dependency lifetime, callback reentrancy and
thread notification lifetime. Shared kernel_tail routes must also be included
when auditing the CSV; owner-header searches alone omit existing behavior.
