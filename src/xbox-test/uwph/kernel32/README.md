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
Issue #171 tracks which routes are host-tested versus pending behavior
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
The write guard suite (`write_guard_test.cpp`) covers the guest `WriteFile` route:
bounded chunks for disk files, stall reporting, and `CancelSynchronousIo` of a
stalled write (opt-in for non-disk handles in the test), plus the synchronous or
overlapped mode of handles from the `CreateFile2` conversion.

## Growable function tables

Mono's JIT (`mono-2.0-bdwgc.dll`, which Haven, Slime Rancher and Say No! More
load) registers its generated code with `RtlAddGrowableFunctionTable` and drops
it with `RtlDeleteGrowableFunctionTable`, both looked up dynamically from
`ntdll.dll`. The desktop SDK declares the family only for the desktop/system
API family, so the UWP build cannot import it statically; Play routes the names
through `exception_route.h` instead.

These three are forwarded to the OS `ntdll.dll` exports, never reimplemented
locally. The unwinder is the OS's: `RtlLookupFunctionEntry` and
`RtlVirtualUnwind` read only the dynamic table list inside the ntdll that owns
them, so a private registry here would accept every registration and then never
be consulted — the guest would believe its JIT frames are unwindable while any
exception through them failed. When the OS export is absent, `RtlAddGrowableFunctionTable`
returns `STATUS_NOT_SUPPORTED` and leaves the caller's table handle untouched,
and grow/delete are inert (they return void and no handle was ever issued),
matching Wine's handling of a handle it does not own. `kernel32.dll` and
`kernelbase.dll` do not export the family in Wine 10; only `ntdll.dll` and the
`api-ms-win-core-rtlsupport-l1-*` sets do.

`exception_test.cpp` asserts the route is the exact OS export, registers a
two-entry table over a code buffer, checks lookup before and after
`RtlGrowFunctionTable`, checks the entry disappears after delete, and compares
the post-delete lookup with a direct call to the export. The refusal path is
always exercised, so a console without the export is covered by the same run.
