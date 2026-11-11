# lane/sd

## 2026-11-01: PR #6 continuation (off-device only)

- Verified the reported starting head `0a3a848`; no PR comments, inline comments
  or reviews were present. Checked existing work before editing: LR-1 cleanup is
  in draft #3 (`678acd2`), policy tests in #8 (`3c19bed`), mapper/provenance
  integration in #12 (`ece6589`). These drafts are not on `main` and are not
  duplicated or merged into this branch. Reconciled `main` at `5184a7a` (PR #5).
- Made the strict fixture runner accept `LLVM_MINGW_ROOT` and `WINE_BIN`, respect
  `WINEPREFIX`, use an ignored local prefix by default, and fail explicitly for
  missing tools or extra arguments. Added negative ordinal-export and nonempty
  relocation-directory assertions. Tightened runtime checks to require actual
  named/ordinal calls and the mapper's unresolved-import rejection as well as
  the exact resolver miss and zero entry markers.
- Fresh off-device checks passed with LLVM-MinGW 20260908 / clang 23.1.1 and
  Wine 11.0: strict fixture build/run; four configuration-failure cases; two
  mutated-build negatives; four mutated-harness assertion negatives; tracked
  shell/Python checks, session-restore, three depot verifier unit tests,
  dependency-pin dry runs, focused ShellCheck and `git diff --check`.
  Linux tool executables needed host-only ELF interpreter/library adaptation
  for NixOS; no PE inputs, mapper assertions or production code were patched.
- Initial Wine setup in the checkout was stopped before fixture results; a
  dedicated `/tmp` prefix then ran the checks. The initial native session test
  lacked `c++`; after provisioning it, the complete local CI checks passed.
  These initial attempts are not counted as passing validation.
- `smoke-all.sh --offline` exited 1: dispatch reached the HID backend link and
  failed with `undefined symbol: 0`; the import-map section lacked
  `/workspace/megabonk-from-console`. Summary: **0 passed, 2 failed**. Console
  SKIPs are not passes. No workaround or gate change was applied.
- UWP app preflight exited 1 / NOT READY: missing `lld-link-19`, `openssl`,
  `makemsix`, `osslsigncode` and `UWP_CROSSBUILD`. No app/package/FromApp runtime
  result is claimed. Full console smoke and launcher acceptance were not run;
  no console access was attempted. Independent review remains pending.
- Local logs/statuses and final-head/source/artifact provenance are kept under
  ignored `src/xbox-test/uwph/host/out-strict-imports/evidence/`. See
  `docs/xbox/strict-import-contract.md` for LR-1 and LR-2 reconciliation and the
  exact distinction between fixture evidence, probe mapping and execution.

## Historical notes (not validation of this continuation)

2026-10-29: LR-2-CONTRACT-REVIEW (proposal only). Added `docs/xbox/strict-import-contract.md` based on `wxl_mapper.h:26,162-180,193-205`, `mlp_probe.h:57-121,126-138`, `STUBS.md:3-6,43`, `launch.h:66-84,119-120,140-144`; read-only inspected `wt/k32` mapper diff. Spec separates callable from probe-only and defines named/ordinal failure/entry-marker fixture. No mapper/app implementation or test run; LR-2 remains unimplemented.

2026-10-29: LR-2-FIXTURES (build-only). Added synthetic provider/importer fixtures and `src/xbox-test/uwph/host/tests/run-strict-imports.sh`. `bash src/xbox-test/uwph/host/tests/run-strict-imports.sh --build-only` passed using shared LLVM-MinGW; toolchain `objdump -p` verified `strict-provider.dll` exports and `importer.dll` imports named `fixture_named` and ordinal 7. `objdump -r` confirmed unresolved `absent_name` and `absent_ordinal` object references; these are not PE imports yet. `git diff --check` passed. No mapper adapter/resolver or guest-entry test exists; failure diagnostics and entry-marker absence remain pending runtime/harness integration. The script does not claim `llvm-readobj` (not in toolchain) or runtime success.

2026-10-29: DEPOT-INTEGRITY-REVIEW (source-only). Added `docs/xbox/depot-integrity-review.md`. `steamdl.h:317-325,438-448,467-484` contains chunk/download and final file SHA1 checks; the `.done` marker is best-effort and does not include manifest identity (`:481`). No owned-depot runtime evidence was reviewed or claimed. Proposed a synthetic valid/mutated/missing-hash verifier test. `rg -n -i 'manifest|sha|hash|verify|complete' src/xbox-test/app` run and `git diff --check` passed.

2026-10-30: LR-2 strict-import fixture accepted off-device under Wine 11 using desktop aliases for FromApp memory APIs. `bash src/xbox-test/uwph/host/tests/run-strict-imports.sh --run` passed: named plus ordinal #7 imports called into fixture host functions and DllMain marker recorded; exact missing `strict-provider.dll!absent_name` and `strict-provider.dll!#9` resolver requests returned zero, mapper rejected before DllMain, and marker count remained zero. no-CRT direct `lld-link` fixtures include genuine DIR64 relocations and omit unsupported Load Config metadata; no mapper gate changed. This proves only the current raw resolver contract and fixture lifecycle. Production probe provenance, LR-1 cleanup reconciliation, UWP FromApp and console acceptance remain pending.

2026-10-25: Read lane prompt, REBUILD.md, sd dispatch evidence and vrx notes.
Branch started clean at c18b4cc6. No inherited lane notes existed at root.

Objective: AppContainer host primitives, dispatch test PE and real Xbox evidence,
using Xboxnative.Launcher independently of Xboxnative.Launcher. All console access
uses CONSOLE.lock; never push or expose credentials.

Implementation underway in src/xbox-test/uwph/host. The initial backend uses an
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

Final console acceptance: package Xboxnative.Launcher_0.1.3.0_x64__dk715arakrne0,
PID 7156. Separate PE DLLs loaded; 52 assertions passed, zero failed; executed
FromApp RW->RX stub returned 42; D3D11 created FL11_0 and swapchain successfully.
Fresh evidence /workspace/x64-run/runs/k32/k32-final.log, also committed as
src/xbox-test/uwph/host/console-evidence.log. Build hashes/imports/exports saved in
runs/k32. No console security bypass or changes to Xboxnative.Launcher were made.

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
