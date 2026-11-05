# Lane prun notes (exported from the local LANE_NOTES.md)

Lines this lane added to the shared LANE_NOTES.md between 86960ae8a and 494a54020.


### prun — item 52 implementation started, 2026-10-26
- Worktree `/workspace/wt/prun`, branch `lane/prun`, base `86960ae8`; no push.
  Read REBUILD.md first, BLOCKED-upmp.md, inherited notes, RTK instructions.
- Confirmed Play previously mapped three PEs and returned before all guest
  initialization. Probe missing imports returned ABI-unsafe integer zero.
- Added a separate Play runtime: recursive dependency mapping (publish exports
  before recursion for cycles), dependency-order TLS/DllMain, existing tlm
  validation and unwind registration, retained image ownership, 16 native static
  TLS carriers, 16 MiB guest thread stack, SEH and vectored exception reporting.
  Runtime missing imports use named non-returning exception traps; probe mode
  keeps its original behavior. Steam API/client entry remains a named blocker,
  without an emulator or a false successful Steam initialization.
- Guest command-line/module APIs and dynamic loads route through guest state.
  Native AppContainer PEB/TEB remain Windows-owned; direct PEB readers still see
  the launcher. This is a known compatibility limit, not a complete Wine process.
  Guest current directory uses the documented system API; environment inherits
  the AppContainer process environment. No private syscall/segment manipulation.
- Wine loader.c `process_attach` / `MODULE_InitDLL` supply ordering reference:
  dependency recursion with in-progress cycle guard, TLS callbacks before entry.
  Existing tlm carrier and RtlAddFunctionTable code reused. Reference UWP
  CoreWindow swap chain remains unchanged (UnleashedRecomp plume_d3d12.cpp:1244).
- Signed launcher build 0.5.52.0 passes. Focused off-device runtime tests being
  built for TLS, dependency ordering, worker TLS, return, exit, missing import,
  crash and hang. No console execution claimed yet.
- Focused off-device Wine suite now passes all five modes. Guest DLL static TLS
  callback precedes DllMain; EXE TLS and created worker TLS each pass actual guest
  checks. Return=42; exit=37; named missing import traps; synthetic exception
  captured; hang remains live with exact last event. Wine 10 lacks
  VirtualProtectFromApp and package identity, so the test runner alone aliases
  protection to Wine's native VirtualProtect and carrier loading to LoadLibraryW.
  Console product still uses VirtualProtectFromApp and LoadPackagedLibrary.
- Extracted tlm TLS/unwind preparation from existing mapper without changing the
  host copy. Mapping now accepts an import-free dependency in mapped mode;
  execution tests exercise this valid PE case.

### prun — first real Xbox guest execution, launcher 0.5.52.0
- Installed signed launcher on actual Series S, then unchanged
  `src/xbox-test/tools/play-test.sh 3405340 150 .../runs/prun/play-52.0` through the
  real detail/Play path with synthetic one-shot input. Test exits 0, GAP intact.
- Real result: `mapped=2/2 gameCodeExecuted=true`, stopped precisely at
  `missing import hit: kernelbase.dll!InitializeCriticalSectionEx`.
  Evidence: `/workspace/x64-run/runs/prun/play-52.0/launch_result.json`,
  launch_probe.log, boot.log and screenshots. This is console execution of
  guest DLL initialization, not yet EXE entry or Unity's window/device.
- First blocker is the runtime dynamic GetProcAddress router treating
  kernelbase.dll as unclassified, despite an available native implementation.
  Next fix normalizes this kernel facade and preserves optional-lookup NULL
  semantics instead of manufacturing a missing-import pointer for every query.
- Offline canonical smoke-all.sh --offline also passes (2 passed, 0 failed).
  Fresh full console smoke remains pending. Launcher import audit passes.
- 0.5.52.1 deployment was refused: another lane installed 0.5.72.0 during the
  off-device build. Exact Device Portal error: -2147009274, higher version
  already installed. The shell continued into play-test against 0.5.72.0;
  stopped that invalid-attribution test and claim no result from it.
  Next build uses 0.5.100.0. Future console sequences use errexit so an install
  failure cannot accidentally test a different lane's package.
- Fixed kernelbase facade routing and optional dynamic export semantics; extended
  the guest fixture to call InitializeCriticalSectionEx through GetProcAddress
  and assert NULL for an absent optional export. All five Wine modes pass.
- Preparation now initializes every module's static TLS template before any
  callback, on both initial and created guest threads. Added a dependency TLS
  callback which actually calls an EXE export and reads its TLS before the EXE
  callback; this regression passes. Thread-attach DllMain return values are
  ignored as required; PROCESS_ATTACH failure remains fatal.
- Added guest-only CreateWindowExW and D3D11CreateDevice call/result diagnostics.
  Launcher rendering does not invoke these hooks. A guest window record is
  explicitly labeled as such; it is not evidence of a rendered Unity frame.
  Missing-import events now include caller address/module/function RVA.

### prun — resumed at 77bc5c69, 2026-10-26
- Re-read rebuild instructions, upmp blocker, lane history, and current runtime.
  Worktree was clean. Existing commits already implement real Play execution.
- Prior `play-100.0/launch_probe.log` reaches UnityPlayer TLS/DllMain and
  `EXE entry megabonk.exe+0x1264`, then dynamic GameAssembly/baselib mapping.
  Its boot log/result disagree and no fresh result passed acceptance.
- Found an evidence collection bug: play-test reused local files while get-file
  resumes with `curl -C -`. A restarted app truncates/replaces boot.log, so a
  resumed snapshot can preserve the previous session's prefix. Play pulls now
  remove each local snapshot first. Acceptance conditions are unchanged.
- Fresh off-device runtime suite passes all five modes (normal/missing/crash/
  exit/hang). Fresh canonical offline smoke passes 2/2. Signed launcher
  0.5.110.0 builds. Console installation succeeds; fresh Play is in progress.
  No new console acceptance or Unity window/device claim yet.

### prun — acceptance blocked by console transport, 2026-10-26 19:42 BRT
- Local commit `dbb4edd` fixes fresh evidence snapshots. Shell syntax and
  `git diff --check` pass. No acceptance gate was removed or relaxed.
- 0.5.110.0 installed successfully. That first Play attempt was invalidated
  because I edited play-test.sh while Bash was still reading the file; Bash
  reported a parse error. No acceptance is claimed from that run.
- Subsequent run found the package had been replaced by another lane:
  Device Portal returned `Element not found` for the pinned 0.5.110.0 identity.
  Stopped that run. Repacking 0.5.156.0 then collided with an installed package
  of the same identity but different contents (`-2147009285`). Upload also
  returned HTTP 000, so the deployment-state response may have been stale.
- A final transaction was prepared to read the installed version, increment
  its revision, repack, install and Play under one shared CONSOLE.lock. Its
  first package query failed with curl exit 97 before any mutation/deployment.
  Two separate bounded `/api/os/info` checks also returned HTTP 000; the final
  check exited 97. Tailscale status reported this host offline. No network
  reconfiguration or secret handling was attempted. Console lock is released.
- Off-device only: the same loader entered Megabonk.exe, then caught the named
  missing import `user32.dll!MessageBoxA`. A temporary diagnostic wrapper
  captured its arguments without continuing the game: caption `Fatal error`,
  text `Failed to initialize IL2CPP`. This does not establish the console's
  current first blocker. Evidence and temporary diagnostic source are saved
  under `/workspace/x64-run/runs/prun/game-host-*`; no Steam bypass was used.
- Fresh runtime fixtures pass normal/missing/crash/exit/hang. Fresh canonical
  offline smoke passes 2/2. Full smoke and fresh Xbox Play remain unverified;
  no Unity window, device or message-loop acceptance is claimed.
- Restored the source manifest to committed 0.5.110.0; uninstalled experimental
  signed packages remain only in ignored out/. No pushes. Work stays on the
  assigned `lane/prun`, rather than changing the unrelated lane/upmp branch.
- Backup after dbb4edd saved the prun bundle, but finished `ok=13 ... fail=4`
  as the persistent store disconnected (`Transport endpoint is not connected`).
  A final backup attempt follows this notes commit.
- Explicit impossible-acceptance stop: append exact transport failure to
  `/workspace/x64-run/BLOCKED-upmp.md`; do not create LANE_DONE. Resume with a
  healthy Tailscale/Device Portal route, then run the prepared atomic
  version-check/install/Play window and unchanged full smoke. Continue fixing
  the first *console-observed* runtime blocker; do not treat the host dialog
  or prior mapping-only JSON as a fresh console result.

### prun — resume transport recheck, 2026-10-26

Resumed at 248e227e on lane/prun with a clean worktree. Read REBUILD.md,
BLOCKED-upmp.md and prior lane notes. Existing real guest execution commits
remain present; no implementation or test gates changed this session.
A fresh bounded check under the shared CONSOLE.lock ran:
`/workspace/x64-run/dp.sh /api/os/info -m 15 -o /dev/null -w 'HTTP %{http_code}\n'`.
It returned HTTP 000 and curl exit 97. No deployment or Play request occurred.
Fresh real-launcher Play and full console smoke remain impossible through this
transport. Prior host results are not fresh console evidence. Per the explicit
blocked-acceptance stop rule, stop and leave LANE_DONE absent. No pushes.

### prun — fresh resume transport check, 2026-10-26

Resumed at 28a8e6ef on lane/prun with a clean worktree. Read REBUILD.md,
BLOCKED-upmp.md and LANE_NOTES.md. Existing guest runtime commits remain
present. Fresh bounded console check under the shared lock:
`flock -w 20 /workspace/x64-run/CONSOLE.lock /workspace/x64-run/dp.sh /api/os/info -m 15 -o /dev/null -w 'HTTP %{http_code}\n'`
returned HTTP 000 and curl exit 97. The lock was acquired and released;
no deployment or Play request occurred. Real launcher Play and full console
smoke cannot be verified through the current transport. No fresh console
guest execution, Unity milestone, or test pass is claimed. No code or gate
changes; no push. Stop under the explicit impossible-acceptance rule;
LANE_DONE remains absent. Resume with a working Portal route, then use the
atomic version-check/install/Play transaction described above.

### prun — transient Portal recovery, fresh deployment unverified (2026-10-26)

Resumed at 920b1c06 on lane/prun with a clean worktree. Read REBUILD.md,
BLOCKED-upmp.md, LANE_NOTES.md, RTK instructions and the caveman skill.
The first bounded locked Portal health check returned HTTP 200; a subsequent
locked package query confirmed launcher 0.5.157.0 installed. This permitted a
fresh attempt rather than assuming the earlier transport blocker persisted.

Fresh off-device runtime suite passes all five modes: normal returns 42,
missing import reports its name and caller, synthetic exception is caught,
ExitProcess reports 37, and hang remains live with the EXE-entry last event.
Fresh canonical offline smoke passes 2/2. Fresh signed launcher build passes.
Evidence: /workspace/x64-run/runs/prun/{runtime,offline,build}-reconnected.log.
No source or test gate changes were needed for these checks.

A single locked transaction queried the current package version, incremented
its revision in the ignored packaging layout, signed this lane's fresh build,
and submitted installation. Upload returned HTTP 202 with deployment accepted.
Install-state responses then had empty bodies; stopped the polling transaction
before Play rather than treating acceptance of upload as successful installation.
The shared lock was released. A bounded locked installed-package check returned
HTTP 000 without a JSON response; a separate locked /api/os/info check returned
HTTP 000 and curl exit 97. Thus installed identity and deployment outcome are
unknown. No Play request occurred and no new console execution is claimed.
Evidence: /workspace/x64-run/runs/prun/window-reconnected.log.

Fresh actual Play and full console smoke remain impossible through the failed
transport. No Unity window/device/message-loop acceptance or completion marker.
Per the explicit impossible-acceptance stop rule, append the exact failure to
/workspace/x64-run/BLOCKED-upmp.md and stop after local commit and backup.
Resume by verifying installed package identity, then use a locked version-check,
install, and unchanged Play transaction. Never push; retain branch lane/prun.

### prun — resume: preserve guest fatal dialogs, 2026-10-26

Resumed at 1e713060 on lane/prun with a clean worktree. Read REBUILD.md,
BLOCKED-upmp.md, lane notes, RTK instructions and caveman/surgical-patch skills.
The single bounded locked Portal health check returned HTTP 200. This is only
transport evidence; it does not establish any new guest execution on Xbox.

Added a Play-only MessageBoxA diagnostic trap. It preserves caption, message,
type and caller module/RVA in the existing missing-import result, then stops
without fabricating a button selection. Escaping keeps quotes and newlines
unambiguous; text is bounded at 2048 bytes per field. This exposes the previously
observed off-device `Fatal error` / `Failed to initialize IL2CPP` in durable
runtime results rather than requiring a temporary diagnostic runtime.

All six off-device runtime fixture modes pass: normal, missing, crash, exit,
hang and the new fatal-dialog regression. The dialog regression verifies its
text, escaped quote/newline, caller address, unsuccessful completion and actual
guest execution. Canonical unchanged offline smoke passes 2/2. Evidence:
/workspace/x64-run/runs/prun/runtime-dialog.log and offline-dialog.log.
Signed launcher build and current-runtime Megabonk host diagnostic are pending.
No console Play acceptance, Unity milestone, or completion marker is claimed.

### prun — extensionless dynamic DLL loading and complete host data

Fixed Play LoadLibrary normalization: a basename with no extension now receives
`.dll` before local-file lookup, matching the Windows loader rule. Added an
actual dynamically mapped dependency fixture under a second filename; normal
execution verifies its export, TLS/DllMain state, worker TLS and retained handle.
All six runtime modes pass. Failed native fallback loads now log requested name
and exact GetLastError while preserving the caller's error value.
Unchanged canonical offline smoke passes 2/2; signed launcher build passes.
Evidence: runtime-extension.log, offline-extension.log, build-extension.log in
/workspace/x64-run/runs/prun. Source manifest remains unchanged.

The partial host copy lacks Megabonk_Data/il2cpp_data/Metadata/global-metadata.dat.
Player.log reveals this as the cause of its IL2CPP fatal dialog. The complete
owned installation at /workspace/steam-owned/games/megabonk contains that data;
SHA-256 matches for Megabonk.exe, UnityPlayer.dll, GameAssembly.dll and baselib.dll.
Using that complete installation with the current runtime reaches
user32.dll!GetSystemMetrics. Adding this lane's packaged uwph-u32c.dll to the
host runner layout resolves that test setup omission; the next actual host
blocker is user32.dll!EnumDisplayDevicesA at unityplayer.dll+0x73c45b
(function RVA=0x73c410). GameAssembly/baselib DllMain ran. These are off-device
Wine diagnostics only. No Steam API/client initialization was bypassed.

A fresh locked console transaction selected installed 0.5.157.0 and packaged
0.5.157.1, then upload returned HTTP 000 before installation could be verified.
Errexit stopped before Play. Evidence: window-extension.log. An earlier window
failed to acquire the shared lock before any request. No fresh console guest
execution, Unity milestone, or full smoke pass is claimed. No repeated transport
health checks were made. LANE_DONE remains absent.

Backup after 51dfcc6 reported ok=15/fail=1: the prun bundle was saved, but the
external notes archive had a missing-file error while other lanes also backed up.
Another milestone backup follows this commit.

### prun — reproducible full-game host diagnostics and integration finding

Added tests/play-game-host.sh GAME_DIR [DIAGNOSTIC_USER32_DLL]. It rebuilds and
runs all six fixture gates, requires complete owned Megabonk data, prints the
actual PE/core DLL hashes, and invokes the same Play runtime under Wine. Its
optional DLL override affects only the host test layout and restores this lane's
DLL on exit. SHA-256 equality after restoration was verified. No packaged source,
manifest, console state, Steam API, or DRM behavior is replaced by this harness.

The host runner now retains host-game-events.log independently of stdout: Unity
redirects stdout into Player.log after EXE startup, which previously hid later
runtime events from the shell log. Game diagnostics wait up to 10 seconds for
the worker, then report executed/finished/success/exit code and exact last event.
An exit-zero diagnostic means a guest ran and a result was captured; its success
field and missing-import detail still describe failure. It is not console or
playable-game acceptance. Normal fixture exit-code gates are unchanged.

Fresh reproducible run with this lane's own packaged core stops precisely at
user32.dll!EnumDisplayDevicesA (UnityPlayer +0x73c45b). That API already exists
in lane/u32m, so no duplicate implementation was added here. A second explicit
host-only diagnostic using its existing compiled core (u32m HEAD ef9c3cf7)
reaches `guest CreateWindowExW` at UnityPlayer +0x740ffc, then stops inside that
call on imm32.dll!ImmGetContext at UnityPlayer +0x9681bb (function RVA=0x968193).
This is the first observed Unity window-creation call in this session, on Wine
only; it did not return a guest window or create a native Xbox window. U32m also
has a separate imm32 implementation (4d982f53), not adopted or claimed here.
Keep integration work with the owning lane rather than duplicating its APIs.

Evidence: host-repro.log, host-repro-u32m.log and host-repro-u32m-events.log in
/workspace/x64-run/runs/prun. Six runtime modes, current-core game diagnostic,
alternate-core diagnostic, shell syntax, git diff --check and unchanged offline
smoke (2/2) pass. The last console upload failed curl exit 28 / HTTP 000; no Play
occurred. Fresh Xbox Play/full smoke remain outstanding. LANE_DONE is absent.

### prun — normalized Steam dynamic-load prerequisite

Resumed at 2e3cea94 on lane/prun with a clean worktree. Read REBUILD.md first,
then RTK instructions, lane history, external blocker and caveman/surgical-patch
skills. The single bounded locked Portal health check returned HTTP 200. This
is transport evidence only; no deployment, fresh Play or console smoke occurred.

Fixed the Play loader's dynamic Steam prerequisite check to run after DLL-name
normalization. Previously an extensionless `LoadLibraryW(L"steam_api64")` skipped
that check and reached normal mapping/fallback. Both steam_api64.dll and
steamclient64.dll now stop with the named real-client prerequisite, including
caller module and RVA. Static import behavior remains unchanged. No Steam DLL,
DRM code or real client was executed or replaced by this change.

Added actual guest-execution regressions for mixed-case extensionless API loading
and explicit client DLL loading. All eight host runtime modes pass; both new
modes finish unsuccessfully with their exact named blocker and caller address.
Canonical unchanged offline smoke passes 2/2. Evidence:
/workspace/x64-run/runs/prun/runtime-steam-boundary.log and
/workspace/x64-run/runs/prun/offline-steam-boundary.log. These are off-device Wine
results, not fresh Xbox acceptance. Signed launcher build validation follows.

Fresh Xbox Play and full console smoke remain outstanding; LANE_DONE is absent.
The owning u32m lane still supplies EnumDisplayDevicesA/imm32 integration. Keep
those API implementations in their lane. Next console window must install and
verify this lane's identity before running unchanged play-test.sh 3405340.
Signed launcher build also passes (build-steam-boundary.log); source manifest
version is unchanged. Shell syntax and git diff --check pass. Commit remains
local on lane/prun, followed by the required backup. No push.

### prun — wide guest fatal-dialog diagnostics, 2026-10-27

Resumed at 9d330215 on lane/prun. Four files already contained uncommitted
MessageBoxW diagnostics and a short Unicode regression; preserved and completed
that work. Read REBUILD.md, external blocker, lane history, RTK and the caveman
and surgical-patch skills. One bounded locked Portal check returned HTTP 000 /
curl exit 97. No installation or Play request occurred; no further transport
checks were made.

Play now preserves MessageBoxW caption, text, type and caller in its named
fail-stop result, like MessageBoxA. It does not fabricate a button response.
Fixed a discovered boundary issue: conversion can exceed 2048 UTF-8 bytes, so
wide messages must not pass through the ANSI byte limit after conversion.
UTF-16 input remains bounded at 2048 code units and a boundary high surrogate
is excluded before conversion. Added actual guest regressions for escaped
Unicode text and a 2047-character message ending at a surrogate-pair boundary.

All ten off-device runtime modes pass. Unchanged offline smoke passes 2/2;
signed launcher build, shell syntax and git diff --check pass. Evidence lives
under /workspace/x64-run/runs/prun: runtime-wide-bound.log,
offline-wide-bound.log and build-wide-bound.log. No fresh console execution,
Unity milestone, full console smoke or completion marker is claimed.
Next offline work: mapped PE export forwarders currently return NULL in
mlp::asset_export, so exercise and implement forwarding in Play's loader while
preserving the probe-only path and Steam prerequisite.

### prun — execute PE export forwarders through the Play router

Added execution-only PE export forwarding in play_runtime.h. The static MLP
probe and mapper were left unchanged. Wine ntdll/loader.c:950
find_forwarded_export supplies the ordering and parsing reference: split at the
last dot, support named and #ordinal targets, retain dependency edges, and
initialize a newly loaded dynamic target before exposing callable code.
Forwarder strings are bounded to the export directory; recursion is limited to
32 steps. Missing optional forwarder targets return NULL/ERROR_PROC_NOT_FOUND;
static imports retain named fail-stop traps. Steam targets still go through the
existing real-client boundary rather than loading Steam/DRM code.

The initial forwarder fixture was incomplete: lld with only a .def produced an
import library, and an added object without relocations hit the mapper's existing
relocation-directory requirement. Corrected the fixture with a retained pointer
relocation; llvm-readobj confirms actual name/ordinal/lazy/cycle/missing/Steam
forwarders. Neither setup failure is counted as proof of forwarding behavior.
A runner built from the pre-change aec83c6c runtime then mapped the corrected
DLL and exited 1 with missing import play-forward.dll!ForwardName. Evidence:
forward-real-before.log and build-forward-before.log under runs/prun.

The new runtime passes all eleven modes. Normal execution verifies a static
name forwarder, a target ordinal, lookup by source ordinal, bounded cyclic and
absent optional lookups, and lazy mapping plus actual target TLS/DllMain state.
The new steam-forward mode reaches the exact steam_api64.dll!SteamAPI_Init
fail-stop trap with its guest caller, without executing any Steam code.
Existing hang assertion remains unchanged; normal-only dynamic-loading checks
run after negative-mode branches so their events do not obscure the hang gate.
Signed launcher build, unchanged offline smoke 2/2, shell syntax and diff checks
pass. Evidence: runtime-forward-after.log, build-forward.log, offline-forward.log.

A fresh complete-owned-game run using the current production routes still
executes Megabonk and stops at user32.dll!EnumDisplayDevicesA, UnityPlayer
+0x73c45b (function RVA 0x73c410). Evidence: host-forward-events.log. This remains
off-device Wine evidence. No fresh Xbox Play/full smoke or Unity milestone is
claimed; LANE_DONE remains absent. Further host-only integration diagnosis uses
the owning u32m lane's existing API implementations without changing its files.

### prun — first returned Unity guest window in host-only integration

A supplemental off-device diagnostic reused u32m's existing user32 core and
imm32/route.h (u32m observed at ab52450f). The generated runner adds only the
owning lane's imm32 resolver before Play's existing fallback; the product
runtime, launcher package, probe gates and game files are unchanged. No Steam
or DRM DLL code is loaded or replaced. This is explicitly a diagnostic
integration, not this lane's packaged production behavior or console evidence.

With these existing owner implementations, Unity's actual call to
CreateWindowExW at UnityPlayer +0x740ffc returns a non-null guest window record.
Next actual blocker: gdi32.dll!GetDeviceCaps at UnityPlayer +0x7389dd (function
RVA 0x7388e0). A second typed, non-returning diagnostic captures index=88
(LOGPIXELSX) and the supplied HDC before reporting the same named blocker.
No fabricated device capability is returned. Wine gdi32/dc.c:621 and
win32u/dc.c:877 validate a DC and ask its device driver for capabilities.
u32m core.cpp:395 creates opaque record DCs, so native GDI cannot consume them.
The next backend work must keep GetDC/ReleaseDC validation, virtual-display DPI
and GetDeviceCaps coherent; avoid forwarding these handles into host GDI.

Evidence under /workspace/x64-run/runs/prun: owner-integration-events.log,
owner-integration-caps-events.log, owner-integration-hashes.txt, generated
owner-integration-runtime.h/runner.cpp and build-owner-integration*.log.
Supplemental core SHA-256:
e1a156de6f484b5d7df5e23607522510a21e5731e263a104b8f7b78223dff3e2.
Supplemental imm32 header SHA-256:
3f266fbfa0707dc35a0df02794957f486750bb18587882ba1f30161ae10202f9.
A shell EXIT trap restored this lane's host-test core; SHA-256 equality with
out/layout/uwph-u32c.dll was checked afterward
(a44283f5779bc7113543fc69797257737a63f55895673cd64f0b23113e4dacf2).
No source or output was written into u32m's worktree.

This is a returned guest record under Wine, not a native Xbox window, rendered
frame, D3D11 device, or Unity message-loop acceptance. This lane's own packaged
API set still stops at EnumDisplayDevicesA; the owner implementation must be
integrated before claiming that blocker fixed in Play. Fresh console Play and
full smoke remain pending the failed Portal transport. The one health check
this session returned curl 97/HTTP 000, and no console mutation occurred.
LANE_DONE is absent. Work remains on lane/prun; nothing pushed or posted.

Milestone backup after aec83c6c succeeded (ok=16, fail=0). Backup after 93108f69
is running; its result will be reported without treating it as console proof.

### prun — rejected DLL initialization cannot succeed on retry, 2026-10-27

Resumed at cf048bf1 on lane/prun with a clean worktree. Read REBUILD.md,
external blocker, lane history, RTK and caveman/surgical-patch instructions.
A bounded locked Portal check returned HTTP 200. No secrets were printed.

Found a Play loader state bug: DllMain returning FALSE left initializing=true.
A second LoadLibrary then treated the retained rejected DLL as initialized and
returned its handle. Added an actual mapped guest DLL that rejects attach and
an EXE regression that loads it twice. Before the fix it exits 17, proving the
second load incorrectly returned a handle (reject-attach-before.log).

Failed dependency/TLS/DllMain initialization now clears the recursion flag and
records permanent failure for the retained image. Repeated LoadLibrary returns
NULL with ERROR_DLL_INIT_FAILED. This runtime retains modules for diagnostics;
full Windows detach/unload/reload recovery is still unsupported. No retry is
reported as successful. Wine ntdll/loader.c process_attach clears its recursion
flag after both successful and failed attachment; that supplied the reference.

All twelve off-device runtime modes pass, including the new two-load rejection
regression; unchanged offline smoke passes 2/2. Evidence under
/workspace/x64-run/runs/prun: runtime-reject-before.log,
reject-attach-before.log, runtime-reject-after.log and offline-reject.log.
Signed updated build and unchanged full console smoke are pending.

A locked transaction installed and verified Grok.XboxTest_0.5.157.1 using the
pre-fix current launcher build, then invoked unchanged play-test.sh 3405340.
Upload HTTP 202, command uploads HTTP 200 and app launch HTTP 200 are transport
and installation evidence only. Result collection is pending. No Unity or
console guest-execution milestone is claimed here. LANE_DONE remains absent.

The updated rejection build also passes; milestone backup completed
ok=16/fail=0. Fresh console Play on verified 0.5.157.1 failed without a
launch: RESULT or launch_result.json. Its launch_probe.log reaches actual
worker startup (guest environment event) after mapping Megabonk/UnityPlayer,
but no TLS callback or guest entry event. Screenshots show Xbox Home after
Play. This does not prove guest code executed. Evidence: play-resume/ and
window-resume.log. Added real startup-boundary events around observer/worker,
current-directory, TLS-carrier and unwind setup to narrow the console failure.
All twelve runtime modes and signed boundary build pass off-device.

Unchanged full smoke finished 3 passed/6 failed. Root cause of fresh-launch
and TLS uploads was curl exit 26: expected local files in out/uwph/layout and
out/tls/layout were missing; Portal answered other requests. Rebuilding those
specific existing build variants before rerunning the unchanged gate. No test
assertion, product manifest or console API implementation was weakened.

### prun — console dump isolates recursive vectored logging

Fresh diagnostic package 0.5.157.2 again produced no launch result; exact
probe last event remains guest environment setup. Verified the deployed EXE
contains every new marker, so the missing following marker is real. Enabled
standard Device Portal crash capture and listed xbtest.exe.2460.dmp.
Used documented GET /api/debug/dump/usermode/crashdump with HTTP Range under
short locked windows; metadata and two 32 KiB stack slices suffice. No full
memory dump, secrets or account data were printed. Reference:
https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/device-portal-api-core

Exception: thread 2584, code 0xc0000005, ntdll.dll+0x10555, write target
0xac57b00ff8, RSP=0xac57b01000. The stack occupies 0xfff000 bytes from that RSP.
Both ends show repeated play_runtime::vectored -> State::event -> lp_sink and
std::system_error/exception dispatch. The initial frame is runImpl+0x134
logging guest environment, with fflush/_write below it. main.cpp logf also
calls OutputDebugStringA. Logging from the vectored callback reenters the same
logging path during an exception and exhausts the worker stack. Thus earlier
absence of a current-directory marker did not establish that API was failing.
Fix and an injected handled-log-exception regression are in progress.

After building the existing out/uwph and out/tls variants, unchanged canonical
full console smoke passes 9/9, including native TLS, two threads, callbacks,
DLL lifecycle, cross-image C++ catch and 29 W^X protections. Evidence:
smoke-built-fixtures.log and smoke-evidence-2026-10-27-081633-80YRZR/.
This console smoke is real; it does not establish Megabonk guest execution.

The new observer-log regression reproduces the observer bug without console
transport: the logger raises handled 0x40010006 exceptions. Before the fix,
executed=0/finished=0 and the last event recursively reports that exception.
After the fix, actual guest execution returns 42, and the regression verifies
nonzero observer count/address and the exact observed code. VEH now records
only atomic code/address/count, with no logger, formatting, allocation or mutex.
Unhandled exceptions still reach the existing SEH filter and detailed result.
All thirteen runtime modes and signed launcher build pass. Evidence:
observer-log-before.log, runtime-observer-{before,after}.log, build-observer.log.
Fresh Play of the fix is in progress; no guest execution on Xbox claimed yet.

### prun — real Xbox guest execution restored; integrate existing user32 owner

Verified installed 0.5.157.3 and ran unchanged play-test.sh 3405340 through the
real launcher. It now returns exit 0 with the required GAP summary and exact
launch: RESULT. launch_result.json records stage=guest-execution,
gameCodeExecuted=true, mapped=4/4 and the exact runtime blocker:
user32.dll!EnumDisplayDevicesA at UnityPlayer+0x73c45b (function RVA 0x73c410).
This is fresh real-console guest execution, not Wine or mapping-only proof.
No Steam DLL/client or DRM code was executed or replaced. Evidence:
play-observer-fixed/{boot.session.log,launch_probe.log,launch_result.json}.

To fix this established blocker and reach the already diagnosed Unity window
path, integrated existing owning-lane user32 commits d7a25e6b, 75377f16 and
618c4c03 (only src/xbox-test/uwph/user32 files; their historical lane notes omitted).
Also integrated imm32 owner 4d982f53 route, tests and README, wiring its resolver
into mlp_probe.h after user32 routing. No files in the owner's worktree were
written. These APIs are existing owner implementations, not duplicate code.
Their missing-import assertions become stricter (83 -> 15), never weaker.
They expose record windows/DCs and no-context IME behavior, not native HWNDs
or GDI devices. Supplemental host evidence already identified GetDeviceCaps
as the next blocker after the returned guest window. Fresh integration build,
owner tests, full Play and unchanged smoke verification are in progress.

Integrated owner verification passes: mapped user32 regression returns 42,
imm32 Wine null-context differential passes, all thirteen Play runtime modes
pass, complete-owned-game host diagnostic returns its exact captured result,
and signed launcher build succeeds. The current product routes now execute
Unity CreateWindowExW at +0x740ffc and return a non-null guest window record;
next host blocker is gdi32.dll!GetDeviceCaps at +0x7389dd. This replaces the
previous supplemental-only integration with actual packaged source. Host
output remains explicitly off-device, with no native window or rendering claim.
Evidence: user32-owner-test.log, imm32-owner-test.log,
host-owner-integrated.log, host-owner-integrated-events.log,
build-owner-integrated.log under /workspace/x64-run/runs/prun.
Installed 0.5.157.4 is running unchanged console Play; full smoke rerun follows.

### prun item 52 — final acceptance and handoff, 2026-10-27

Reached the requested Unity window-creation stopping point ON Xbox Series S
through the unchanged real launcher Play path, with verified installed package
Grok.XboxTest_0.5.157.4_x64__dk715arakrne0. play-test.sh 3405340 exits 0 and
collects the required launch: RESULT, GAP summary, screenshots and JSON.
Console launch_probe.log records UnityPlayer TLS PROCESS_ATTACH and DllMain,
Megabonk EXE entry +0x1264, then baselib and GameAssembly DllMain. Unity calls
CreateWindowExW at +0x740ffc (function RVA +0x740e51); it returns non-null
00000200776F8E20 with error=0. This is a guest window record; native CoreWindow
bridging, visible Unity rendering, D3D11 creation and gameplay are NOT claimed.

The exact next console blocker is gdi32.dll!GetDeviceCaps, caller
0x2007fd389dd, UnityPlayer+0x7389dd (function RVA +0x7388e0). Both launcher
RESULT and launch_result.json report it with gameCodeExecuted=true and
stage=guest-execution. JSON ok=true means the recorded window milestone was
reached; exitCode=null and the named missing import do not imply a clean game
exit. The game remains unplayable. No Steam/DRM DLL code was run or replaced.

Final evidence under /workspace/x64-run/runs/prun:
- play-owner-integrated/: fresh real-console logs, request/result JSON and shots.
- window-owner-integrated.log: verified package/install and actual Play exit 0.
- smoke-owner-integrated.log: unchanged full console smoke 9 passed, 0 failed.
- runtime-observer-after.log: all 13 off-device runtime modes pass.
- user32-owner-test.log: mapped owner regression entry=42.
- imm32-owner-test.log: typed ABI/Wine differential and no-context tests pass.
- host-owner-integrated-events.log: current production host diagnostic agrees
  on the returned window and exact GetDeviceCaps blocker; explicitly Wine only.
- build-owner-integrated.log and audit-owner-integrated.log: signed launcher
  build and both launcher/user32 PE import audits pass.
Shell syntax and git diff --check pass. No test gate was weakened.

Local milestone commits on assigned lane/prun: ec4c0626 rejects failed DLL
initialization retries; dbdf4e47 records startup boundaries; 9de0b843 removes
recursive vectored logging; f054e934 integrates existing owner user32/imm32
implementations. Source manifest stays 0.5.110.0; monotonically selected console
versions changed only ignored packaging output. Nothing pushed or posted.

Stop at the specified Unity window-creation milestone. Follow-up graphics work
must implement GetDeviceCaps against the same validated record DC/display
backend as GetDC/ReleaseDC, rather than forwarding opaque records to host GDI.
Steam still requires the legitimate real-client lane. Historical transport and
mapping-only failures above are superseded only for this verified prun result.
Final summary commit and required backup precede the uncommitted LANE_DONE marker.

## vis — visual/gameplay integration started, 2026-10-27

Resumed clean at 1174baec on lane/vis. Read REBUILD.md, PLAYABLE.md and prun's
handoff. Ported the minimal gdi32 files from owner commits 5bc5f2fa and
01b44758, plus the caps hook in the existing user32 record registry. Adapted
only resolver integration to preserve prun's current Play loader. No owner
worktree was modified. No graphics or gameplay milestone is claimed.

Locked Portal health check returned HTTP 200. Signed launcher build is in
progress. The initial caps test could not run because this fresh worktree had
no built user32 DLL; rerun after build. Reused prun's existing local signing
certificate for package identity continuity. Next: off-device caps/runtime
checks, monotonically versioned deployment under CONSOLE.lock, actual Play,
and diagnose the next console blocker. Audio and Steam belong to other lanes.

Signed build passed. Deployed 0.5.158.0 after checking installed 0.5.157.4;
Portal installation returned Success=true and play-test.sh selected 0.5.158.0.
The whole install/Play transaction holds CONSOLE.lock. Caps regression passes
in a fresh /tmp/vis-wine prefix: 40 invalid-DC differential cases plus live
and released DCs, configured metrics, conservative capabilities and ABI routing.
Existing default/prun Wine prefixes failed to load kernel32.dll before tests;
they were preserved. Runtime verification is rerunning in the fresh prefix.

First fresh Xbox Play completed through 0.5.158.0. Evidence: runs/vis/play-caps.
Unity called D3D11CreateDevice twice with hr=0 and non-null device pointers.
The first launch result snapshots that device milestone while the worker
continues: the subsequent exact blocker is user32!RegisterDeviceNotificationW,
UnityPlayer+0x9676db (function RVA +0x967680). Thus ok=true does not establish
rendering, a swapchain, main menu or gameplay. Screenshots are collected.

Porting owner's fb6c341b notification implementation and regression next.
It validates guest windows/filter types and explicitly fails unsupported
arrival/removal delivery; it never fabricates a successful registration.

Caps milestone validation: all 13 Play runtime modes passed off-device in the
fresh prefix; unchanged offline smoke passed 2/2. Screenshot 4-final.png shows
launcher restoration text, not Unity content. The logs establish guest record
window creation and D3D11 device creation only. Milestone backup completed
ok=17/fail=0. Notification test passes 24 cases. Signed notification build
passed; deploying 0.5.158.1 and collecting fresh Play. Built all existing host,
uwph and TLS smoke fixture variants for the unchanged full console gate.

Second fresh Xbox Play on 0.5.158.1 passes the notification import but reaches
Unity's fatal dialog: DialogBoxParamW at UnityPlayer+0x946d4d. Static inspection
of the unmodified owned UnityPlayer shows its caller +0x946cf0 stores a fatal
message pointer then opens dialog resource 105; +0x946d80 calls it before exit.
Do not treat this as an ordinary missing UI import or suppress the fatal path.
Enabled Unity's documented command-line logFile output vis-player.log in the
game directory to capture the underlying cause next. This does not alter the
game binary or return a synthetic dialog result. Host-only integration passes
notifications and reaches PeekMessageA; that is not the console's blocker.

First unchanged full smoke attempt did not stay green: fresh launch timed out
and D3DT lacked all presented-frame markers after successful triangle setup.
Kernel 71 checks, mapped W^X and HID sections pass. TLS section is still running.
Do not claim 9/9 or weaken the 300-frame requirement. Once the locked gate
finishes, check ordinary foreground/user state and rerun after stopping the
launcher. Player-log package 0.5.158.2 is built and queued behind that lock.
