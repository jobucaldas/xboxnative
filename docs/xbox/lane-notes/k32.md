# lane/k32

2026-10-25: Read lane prompt, REBUILD.md, sd dispatch evidence and vrx notes.
Branch started clean at c18b4cc6. No inherited lane notes existed at root.

Objective: AppContainer host primitives, dispatch test PE and real Xbox evidence,
using Joao.XboxSteam.Uwph independently of Grok.XboxTest. All console access
uses CONSOLE.lock; never push or expose credentials.

Implementation underway in xbox-test/uwph/host. The initial backend uses an
explicit versioned opcode table served entirely by PE code, with FromApp memory,
native threads/files/synchronization and process creation denied. It is a host
primitive foundation; adapting Wine's actual Nt syscall entrypoints and loader
is separate integration and must not be claimed as tested here.

Next: build the UWP test, deploy own identity, pull LocalState/k32.log, record
actual results and limitations. Commit and run backup at milestones.

Milestone 1: 7a98046e built and installed successfully. First ON-CONSOLE run
(pid 6880) passed 48 assertions, including file roundtrip, native thread join,
sync/TLS, process denied, and executed RX code returning 42. Evidence pulled to
/workspace/x64-run/runs/k32/k32.log. First build statically linked all test code
into the UWP PE; strengthening to separate packaged host/test PE DLLs before
final acceptance. vrx evidence now exists in runs/vrx/vrx.log: A RWX fails 87,
B RW->RX succeeds, C FromApp RW->RX succeeds (all on real console).

Milestone 2: version 0.1.1.0 ON-CONSOLE pid 6120 loaded separate uwph-host.dll
and kernel-test.dll via LoadPackagedLibrary, then passed all 48 assertions.
Evidence: /workspace/x64-run/runs/k32/k32-dll.log. Build compiles all three PEs
with WINAPI_FAMILY=APP and static CRT. No launcher package was modified.

Added a separate source-derived Wine 10 ntdll eight-opcode adapter: precise time
and debug byte-count implemented, remaining ELF/server/unwind/fd/spawn operations
fail STATUS_NOT_IMPLEMENTED. Important correction to inherited sd notes: most
Nt memory/thread/file functions do NOT flow through this eight-opcode table;
they need a separate syscall bridge. The public host ABI is intentionally
separate from this Wine opcode ABI. Do not infer that prebuilt Wine now loads.

Final console acceptance: package Joao.XboxSteam.Uwph_0.1.3.0_x64__dk715arakrne0,
PID 7156. Separate PE DLLs loaded; 52 assertions passed, zero failed; executed
FromApp RW->RX stub returned 42; D3D11 created FL11_0 and swapchain successfully.
Fresh evidence /workspace/x64-run/runs/k32/k32-final.log, also committed as
xbox-test/uwph/host/console-evidence.log. Build hashes/imports/exports saved in
runs/k32. No console security bypass or changes to Grok.XboxTest were made.

One earlier run had a flaky test assuming GetTickCount64 must advance after
10ms. Its coarse resolution makes that invalid; now verify monotonic tick count
and elapsed QPC across Sleep. Preserved failed log as k32-clock-test-failure.log.
Also found wait-log.sh could resume old local logs and accept stale matches;
changed it to download fresh snapshots. Verified with mocked two-poll regression
(preexisting matching stale log ignored) and the real fresh console log.

Backlog item 9's native-host/test-PE/console acceptance is complete. This is
NOT a claim that unmodified Wine kernel32/ntdll or Megabonk now loads: those
need a Wine syscall bridge plus loader/server integration. Host primitives,
versioned ABI, actual Wine10 leaf-opcode adapter and explicit unsupported results
are provided for that work. Next integration must preserve exact Wine argument
layouts, NTSTATUS translation, TEB/TLS/unwind handling and loader state; see
README.md. No further console testing is needed for this milestone.
