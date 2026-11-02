# Native TLS and unwind mapper contract

`wxl::load(Image&, path, log, Carrier, Resolver)` retains the image until explicit
`Image::close()` or owner destruction. DLL entry points receive process
attach/detach. Registered `.pdata` remains valid through guest detach, then is
deregistered before freeing memory. A failed process attach does not receive
DllMain process detach; TLS callbacks that attached still receive detach.

`close()` reports cleanup failure and retains the remaining resources for retry.
A failed `load()` attempts cleanup immediately but can likewise retain resources
if cleanup fails. Do not replace or reuse a live image: retry `close()` first.
Successful cleanup is idempotent and permits reuse; retries do not repeat guest
detach callbacks. The destructor makes a final best-effort attempt, not a success
guarantee. Persistent OS cleanup failure intentionally leaks remaining resources
rather than freeing memory still registered with the unwinder. `run()` reports
explicit cleanup failure even if its destructor subsequently succeeds.

Close on the process-attach thread, with all managed workers joined and carriers
still alive. Guest callbacks must return normally; arbitrary guest faults and
partial callback replay are outside this contract. The original `run()` wrapper
still runs the original WXL test.

Compiler static TLS and TlsAlloc are different Windows storage mechanisms.
`Carrier::index` must return an exclusive, Windows-initialized static index;
`Carrier::block` must return the corresponding TLS block base for the current
thread. The native packaged carriers reserve 64 KiB per image/thread using the
SDK CRT's standard TLS directory and startup lifecycle. Only its unused start
sentinel may precede the reserved array; a changed CRT layout fails closed.
`TlsAlloc` is used only for attachment bookkeeping. No TEB fields are modified.

The caller must retain each carrier while its guest is loaded. Only one guest
may own a carrier at once. Templates larger than 64 KiB or requiring alignment
above 16 fail. Explicit `attachThread(DLL_THREAD_ATTACH)` and
`detachThread(DLL_THREAD_DETACH)` surround every managed worker entry. Join all
workers before image/carrier destruction. Raw guest-created threads require a
future thread-creation adapter; this loader does not intercept arbitrary threads.

The independent lifecycle regression harness runs from the repository root with
`bash xbox-test/uwph/host/tests/run-lifecycle.sh`; `--build-only` compiles without
running Wine. `CC`, `CXX` and `LLD_LINK` select the MinGW C/C++ compilers and COFF
linker. It covers successful and failing DLL attach, cleanup failure/retry,
exactly-once detach, live-image replacement rejection and reuse after cleanup.
It uses a synthetic carrier and desktop memory API substitutions, so it does not
validate real static TLS access, cross-thread behavior, MSVC exceptions or FromApp.
Those require the separate TLS suite and fresh console/launcher acceptance;
`LIFECYCLE-OK` alone is not a merge gate.

Tests use native-TLS Clang/MinGW objects (`-fno-emulated-tls`) and Microsoft ABI
C++ exception objects linked into the same mapped EXE/DLL. The narrow test import
resolver binds genuine static CRT frame handling, termination and deletion.
Because the CRT throw helper cannot find a manually mapped image through
RtlPcToFileHeader, the test adapter raises the native AMD64 C++ exception record
with the owning image base explicitly supplied. The native CRT executes search,
unwind, destructors and catch. Only this test DLL's simple throw metadata is
accepted. General C++/WinRT throws, GCC's EH ABI and arbitrary runtime libraries
are not implemented by this adapter.

Reproduce off-device with `bash tests/run-tls-offdevice.sh` from this directory.
The Wine harness explicitly substitutes desktop memory calls because Wine 10
lacks VirtualProtectFromApp; its success is not Xbox W^X evidence. Build the
isolated console package with `bash build.sh out-tls tls`. After installing only
that package with the locked Device Portal helpers, run
`bash xbox-test/smoke-all.sh` from the worktree root. This lane's smoke defaults
to Joao.XboxSteam.Tls, preserves the exact original WXL assertions and adds strict
TLS/exception/lifecycle/protection checks. Read LANE_NOTES.md for actual console
results and limitations; this document is the API contract, not execution proof.
