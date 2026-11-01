# Lane sysx notes (exported from the local LANE_NOTES.md)

Lines this lane added to the shared LANE_NOTES.md between 494a54020 and 8e0f523b1.

### sysx resume — current acceptance check, 2026-10-28

Resumed at 37a722b3 on lane/sysx. Read RTK, required rebuild/playable
references and read-only vis/prun handoffs. Regenerated research/sysx-todo.csv
before work: unchanged 222 owned imports and 20 pending rows. Seven rows are
deferred audio; eleven require Xbox native export/API operation proof; two
DWM rows retain unsupported policy operations. No additional missing route was
identified. Existing source and host proofs remain unchanged.

Fresh system and native regressions pass under WINEPREFIX=/tmp/vis-wine.
Unchanged smoke-all.sh --offline passes 2/2; console sections were skipped.
Evidence: /workspace/x64-run/runs/sysx/resume-current/{system,native,smoke}.log.
These are off-device results, not Xbox/AppContainer certification.

Read-only vis resolver still contains no sysx_native integration. Its current
handoff reports no new legitimate-client/process implementation beyond saved
swapchain success and the real-client Steam prerequisite. Integration details
remain in research/sysx/CONSOLE_HANDOFF.md. No console deployment, package
version, other lane edit, Steam change or policy bypass occurred. DWM policy
calls remain E_NOTIMPL because this guest backend has no policy renderer.

Twenty pre-existing SoftFloat build-file deletions remain untouched and excluded
from this checkpoint. No product code or existing test was changed. Whole-area
Xbox acceptance remains open; LANE_DONE is absent. Local status checkpoint and
required backup follow; nothing is pushed or posted.


## sysx — inventory and first implementation wave, 2026-10-27

Resumed lane/sysx at 494a5402. Read RTK, REBUILD.md, PLAYABLE.md and vis/prun
handoffs. Existing untracked sysx CSVs contain the previous interrupted
session's initial route comparison. They are preserved as the initial inventory:
20 unresolved imports, including seven audio imports explicitly deferred by
this task. Native routes are inventory entries, not certified API semantics.
No Xbox verification or gameplay milestone is claimed. No LANE_DONE exists.

Priority is IMM32 association, then dbghelp crash diagnostics and WinINet.
Guest window/context handles must never reach host IMM32. The current backend
has no IME context; detach can succeed, while context creation/default
association must report unsupported behavior. Full smoke's vis-reported
rendering failure remains unresolved by this lane; tests will not be weakened.

### sysx — first host-tested routes

Implemented IMM32 association against the existing record-window validator.
Null-context detach succeeds without allocating an IME; invalid windows and
foreign contexts fail. IACE_DEFAULT explicitly fails ERROR_NOT_SUPPORTED.
ImmGetContext now validates record windows instead of accepting arbitrary
non-null HWNDs. Tests retain the six existing null-context Wine differentials,
add two association differentials, and cover guest-record detach/default/error
contracts. No host IMM32 ever receives a guest HWND/HIMC.

Added eleven explicit real-System32 dbghelp/WinINet export routes in the separate
uwph/sysx/native.h, integrated only in the launcher's import resolver. Missing
native exports still reach its existing fail-stop trap. The native regression
registers its actual PE, resolves an exported function, rejects unknown addresses,
parses URLs, and performs real local TCP HTTP requests with status/header and
buffer-size checks. Wine's SymRegisterFunctionEntryCallback64 returns error 120;
the test compares and preserves this failure, not working callback semantics.

Signed launcher build passed. Unchanged offline smoke passed 2/2. Current
vis notes report stuck console deployment and full-smoke failures predating
these changes; no console was used by sysx and no Xbox availability is claimed.
The reproducible research/sysx/audit.py reports 222 owned imports, with 18 still
missing or Xbox-unverified (11 new native routes plus seven deferred audio).
System/tail entries also remain unverified for operation-specific UWP behavior;
that count is an import-route inventory, not a completion claim.

Evidence is under /workspace/x64-run/runs/sysx. Full Play runtime regression
and final rebuild after window-validation tightening are in progress. No
LANE_DONE: console availability/semantics and end-to-end gameplay remain open.

### sysx — integration handoff and remaining console gate

Final signed build with native export diagnostics passes. Every Play now logs
SYSX NATIVE <dll>!<symbol> available=0/1 for all eleven allowlisted exports.
This is export discovery only, not API-operation success. Native discovery
regression verifies all eleven named exports on Wine; callback-registration
failure remains explicitly preserved. IMM32 association regression passes.
All thirteen original Play runtime modes pass again against the final logging
build; evidence: play-runtime-discovery.log (unchanged suite, exit 0).

The complete owned-game diagnostic (/workspace/steam-owned/games/megabonk)
executes actual guest code and stops at user32.dll!PeekMessageA,
UnityPlayer+0x741157. It reaches no missing sysx import. This is off-device Wine
evidence, not Xbox gameplay. Evidence: host-game-events-wave1.log and
host-game-wave1.log under /workspace/x64-run/runs/sysx.

Read-only fresh vis player log confirms the console fatal path follows
"Switching to resolution 1920x1080 failed" / "Screen: DX11 could not switch
resolution (1920x1080 fs=1 hz=0/1)". Mode switching is outside this lane. It also
reports absent XInput DLLs; those are not replaced here. No fatal path was
suppressed and no Steam/audio implementation changed.

Integration for vis: pick sysx implementation eebb0017 and the following
native-discovery commit, then inspect SYSX NATIVE lines on its next locked,
monotonically versioned Play. The sysx signed package is build proof only;
it has not been deployed and retains the source's old manifest version.
Never deploy it directly without assigning a fresh console version and using
the existing console signing identity. No console was contacted by sysx.

Milestone backup after eebb0017 completed ok=20/fail=0. Remaining acceptance:
console export availability for eleven native routes, operation semantics for
system/tail imports as exercised by Unity/gameplay, unchanged full console
smoke, and the owning vis lane's visual/gameplay gates. Seven audio entries
remain explicitly deferred. LANE_DONE remains absent; this is not area completion.

### sysx resume — DWM guest-handle boundary, 2026-10-27

Resumed clean at db0fdde8. Re-read RTK, REBUILD.md, PLAYABLE.md and reference
vis/prun handoffs. Regenerated the import inventory before implementation.
Vis now records unchanged full console smoke 9/9 after an ordinary restart;
0.5.158.4 still fails resolution switching with fs=0. Its next work is the
CoreWindow swapchain bridge. This lane has not contacted the console.

Found DwmGetWindowAttribute/DwmSetWindowAttribute classified as system imports,
which passed guest record HWNDs to desktop DWM. Added a separate typed dwmapi
route that binds only existing packaged user32 record callbacks. Frame bounds
use the borderless backend's physical screen rectangle; null/foreign windows,
short/null output, and child windows fail explicitly. Unsupported attributes
and DWM policy changes return E_NOTIMPL without touching output. Wine 10's
setter is an unconditional-success stub and is deliberately not copied.

Focused off-device regression passes in /tmp/vis-wine: invalid-HWND getter
results match Wine; host record geometry covers negative origins, every short
buffer size, sentinel preservation, child rejection, backend errors, unsupported
policies and resolver restrictions. No native HWND forwarding occurs. These
are partial DWM semantics, not an implemented compositor or console proof.
Both imports remain in research/sysx-todo.csv, now 20 entries including the
seven explicitly deferred audio imports. Signed build and nearest gates follow.

The DWM test now also loads the actual built uwph-u32c.dll explicitly, creates
a real guest record window, checks its physical bounds, destroys it and rejects
the stale handle. A false geometry callback with ERROR_SUCCESS becomes E_FAIL,
never a successful HRESULT. The UWP SDK hides DWMWINDOWATTRIBUTE, so the route
uses documented ABI value 9 with a host SDK static_assert instead of importing
desktop DWM declarations. Final focused regression passes.

All thirteen unchanged Play runtime modes pass with PRUN_WINEPREFIX=/tmp/vis-wine.
Unchanged offline smoke passes 2/2 with WINEPREFIX=/tmp/vis-wine. Initial default
prefix attempts failed exit 53 before executing tests; those prefixes were
preserved and no assertions changed. Source builds initially exposed the UWP
SDK enum partition issue, fixed as described above. Logs under runs/sysx:
dwm-wave2.log, play-runtime-dwm.log, smoke-offline-dwm.log, build-dwm-final.log.
No console deployment or current-package full-console-smoke claim is made.

Final signed launcher build passes. Integration for vis is the next sysx commit
(route, regression, inventory and resolver override only). Native export
availability remains pending its next integrated console Play. Current sysx
source does not contain vis's pending CoreWindow bridge; deploying it separately
would not establish rendering. No LANE_DONE exists because console operation
proof and remaining unsupported DWM policy semantics are still open.

### sysx resume — foreign IME context errors, 2026-10-27

Resumed clean at f106736c. Re-read required references and regenerated
research/sysx-todo.csv before edits: 222 owned imports, 20 unresolved or
Xbox-unverified entries, including seven deferred audio imports. The generated
inventory is unchanged. Latest read-only vis handoff is building its CoreWindow
bridge; existing console logs establish system DLL loading, not all API
operations. Its source does not yet include the sysx native discovery routes.

Expanded the existing typed IMM32 Wine differential to non-null foreign HIMCs.
Before the fix, ImmGetCompositionStringW returned zero with last error 57005;
Wine returned zero with ERROR_INVALID_HANDLE (6). Composition retrieval,
conversion status, composition setting and notification now reject foreign
contexts with that error, without accessing output buffers or host IMM32.
Null-context behavior and the resource-free ImmReleaseContext contract remain
unchanged. The expanded differential passes every original assertion and the
foreign-context cases. Source oracle: Wine 10 dlls/imm32/imm.c get_imc_data,
ImmGetCompositionStringT, ImmGetConversionStatus and ImmNotifyIME.

Evidence under /workspace/x64-run/runs/sysx: imm-foreign.log passes;
smoke-imm-foreign.log records unchanged offline smoke 2 passed/0 failed.
Signed launcher build and all original Play runtime modes are running next.
No console access, package deployment, Xbox operation proof, or gameplay claim.
No LANE_DONE: native Xbox availability and integration acceptance remain open.

Final signed launcher build passes (build-imm-foreign.log). All thirteen
unchanged Play runtime modes pass (runtime-imm-foreign.log). No assertion or
smoke requirement changed. Integration is limited to the IMM32 route and its
existing test/documentation files. Vis now records deployed 0.5.158.5 with
its CoreWindow bridge and is collecting console evidence; sysx did not use
the console. This source package retains its historical manifest version and
must not be deployed without normal monotonic version selection.

Read-only console evidence arrived during backup: vis/play-corewindow on
0.5.158.5 reaches Unity input initialization and records UnloadTime, without
the earlier resolution fatal in vis-player.log. Its import summaries show
advapi32 33, crypt32 7, ole32 18, oleaut32 16 and WinRT API sets 18 total native
exports resolved; ws2_32 resolves 103, setupapi 5 and HID 14. These counts prove
resolution in that package, not every operation's correctness. It still lacks
sysx integration: dbghelp reports resolved=0/stubbed=2. There is no new named
sysx execution blocker in these collected files. Rendering/input/gameplay
acceptance remains owned by vis and is not inferred from UnloadTime or JSON ok.

### sysx resume — system-operation regression coverage, 2026-10-27

Resumed clean at d32066be. Read RTK, REBUILD.md, PLAYABLE.md and read-only
vis/prun handoffs before regenerating research/sysx-todo.csv. The inventory
remains 222 owned imports and 20 unresolved/partial/Xbox-unverified entries,
including seven explicitly deferred audio entries. Latest vis console evidence
still stops at user32!PeekMessageA after successful CoreWindow swapchain
creation. That blocker belongs to u32m; no sysx execution blocker is identified
in the available console logs. No console was contacted by this resume.

Added a separate off-device system-operation regression under uwph/sysx.
It reads the production mlp_routes.h and requires every exercised DLL to have
a system route before loading its actual System32 export. Coverage includes
SHA-256's known abc vector, required hash-buffer sizing, CryptGenRandom's
return contract, destroyed-hash rejection, a volatile per-process registry key
with typed value persistence across close/reopen and missing-value errors,
process-token buffer sizing and SID access, balanced COM and WinRT apartment
initialization including changed-mode failures, COM memory and GUID formatting,
OLEAUT32 BSTR imports by the game's ordinals, and WinRT owned/reference strings
with embedded NULs and duplicated handles surviving source deletion. The test
removes its registry key on success. No account or game files are touched.
These are actual host operations, not Xbox/AppContainer permission proof.

The existing native regression initially printed PASS and exited zero but
Wine logged an unhandled worker-thread page fault at shutdown. Its unchanged
repeat was clean. Cause is not established; no product behavior was changed
on that evidence. Both sysx test runners now explicitly reject Wine's unhandled
fault diagnostic even when Wine exits zero, preserving ordinary nonzero exit
codes. Injected fake-Wine runs confirm both runners reject this exact case.
The final native and system suites pass cleanly with /tmp/vis-wine; IMM32's
existing Wine differential also passes. Unchanged offline smoke passes 2/2.
Evidence: runs/sysx/system-contracts.log, native-resume.log (fault observed),
native-resume-repeat.log, native-fault-gate.log, imm-resume.log and
smoke-system-contracts.log. Shell syntax and git diff --check pass.

No production implementation change, signed rebuild, console deployment,
full-console-smoke result or gameplay milestone is claimed for this test wave.
Pending: integrate prior sysx route commits into vis, inspect all eleven native
export diagnostics on Xbox, verify exercised operation semantics there, and
retain unsupported DWM policy failures. No LANE_DONE: the area is not complete.

### sysx resume — native networking operation coverage, 2026-10-27

Resumed clean at 8d88b2f2. Read RTK, REBUILD.md, PLAYABLE.md and reference
vis/prun handoffs. Regenerated research/sysx-todo.csv before edits: unchanged
222 owned imports and 20 pending entries, including seven deferred audio
imports. Latest vis evidence reaches visible Loading... content, then a C++
exception; its bounded diagnostic deployment is pending. There is no named
sysx missing-import blocker in that evidence. No console was contacted.

Added a separate Winsock operation regression under uwph/sysx. It requires
the production ws2_32 system route, loads real System32 exports, verifies the
game's ordinal aliases, and performs a real IPv4 TCP exchange using ordinary
and scatter/gather I/O. It checks numeric address conversion/resolution,
select readiness, nonblocking WSAEWOULDBLOCK, orderly EOF, closed-socket
WSAENOTSOCK, event set/reset/wait lifecycle and balanced Winsock startup.
No AF_UNIX transport or external network is used by the test. Receive timeouts
and an overall runner deadline bound execution. The runner rejects Wine
unhandled faults even when Wine exits zero. Initial test development corrected
its ioctlsocket ordinal from 12 to the actual game-import ordinal 10; no product
route or existing assertion changed. Final test passes under /tmp/vis-wine;
evidence: /workspace/x64-run/runs/sysx/network-contracts.log.

This wave adds host operation proof, not production implementation or Xbox
networking permission proof. Pending acceptance remains prior sysx integration
into vis, eleven native export diagnostics and API operations on Xbox, and
visual/gameplay integration. DWM policy operations still fail explicitly.
No LANE_DONE exists; this area is not complete. Unchanged offline smoke,
shell syntax and whitespace checks follow before the local milestone commit.

Unchanged offline smoke passes 2/2 (smoke-network-contracts.log); console
sections were explicitly skipped. Shell syntax and git diff --check pass.
No signed rebuild is needed for this test-only change. No existing test gate,
production resolver, package version, audio or Steam behavior changed.

### sysx resume — certificate operation coverage, 2026-10-27

Resumed at ad50a2b9 with a pending certificate regression and README changes;
preserved and reviewed that work before running it. Re-read RTK, REBUILD.md,
PLAYABLE.md and read-only vis/prun handoffs. Regenerated the owned-import audit
before testing: 222 imports, 20 partial/missing/Xbox-unverified entries, including
seven deferred audio entries. No inventory change. Latest vis handoff identifies
an unhandled C++ exception after visible Loading...; its cause remains unresolved.
No named sysx missing import appears in that evidence. No console was contacted.

Completed verification of the pending separate crypt32 regression. It exercises
all seven imported certificate exports through the production system route and
actual System32 exports. An ephemeral self-signed DER fixture is never installed
in a system trust store. Checks cover malformed input, duplicate rejection,
encoded-byte preservation, context lifetime after store closure, cache-only
chain construction, signature/time status and explicit untrusted-root policy
failure. A corrupted signature must produce both the chain failure flag and
TRUST_E_CERT_SIGNATURE from policy. A TRUE policy API result is not treated as
certificate acceptance. No external trust exemption or network request is used.

The regression passes twice with WINEPREFIX=/tmp/vis-wine. Evidence:
/workspace/x64-run/runs/sysx/certificate-contracts.log. Unchanged offline smoke
passes 2/2 in smoke-certificate-contracts.log; console sections were explicitly
skipped. Shell syntax and git diff --check pass. These are off-device operation
checks, not Xbox crypt32/UWP permission proof. No production route, game file,
manifest version, existing assertion, audio or Steam behavior changed.

Pending acceptance remains Xbox availability and API-operation proof for native
routes after vis integration, plus remaining partial DWM behavior and overall
visual/gameplay gates. This test-only milestone needs no signed package rebuild.
No LANE_DONE exists because the requested whole area is not complete.

Read-only integration update during backup: vis's fresh play-cxx console log
now advances past its prior C++ exception and stops at
steam_api64.dll!LoadLibrary (real client prerequisite item 36), caller
UnityPlayer+0x4f4398. This is the excluded stm lane's prerequisite, not a sysx
missing import. Its console summaries resolve advapi32/crypt32/COM/WinRT,
setupapi, HID, shlwapi, version, winhttp and Winsock exports; export resolution
still does not prove every operation or pending dbghelp/WinINet integration.
No game or Steam behavior was changed here. Evidence remains owned by vis at
/workspace/x64-run/runs/vis/play-cxx/. Xbox native-operation verification and
whole-area completion remain open; no LANE_DONE was created.

### sysx resume — WinHTTP proxy operation coverage, 2026-10-28

Resumed clean at 250dc304 on lane/sysx. Read RTK, required rebuild/playable
references and read-only vis/prun handoffs. Regenerated research/sysx-todo.csv
before edits: unchanged 222 owned imports, 20 pending entries including seven
explicitly deferred audio imports. Latest vis handoff verifies Xbox package
0.5.158.9, native D3D11/CoreWindow swapchain creation and full console smoke
9/9. The screenshot remains Loading... and the immediate stop is the legitimate
real-client Steam prerequisite, outside this lane. No menu/gameplay acceptance
is inferred. No console was contacted by this resume.

Added a separate WinHTTP proxy regression covering all four game imports via
the production system route and actual System32 exports. It verifies invalid
handle/options errors, current-user proxy configuration ownership, real local
HTTP PAC retrieval and JavaScript URL/host evaluation, named-proxy output and
closed-session rejection. Configuration is read without changing user settings.
The test does not use WPAD, external DNS, an external proxy or AF_UNIX. Listener
readiness, socket reads and Wine execution have deadlines; the runner rejects
Wine unhandled faults even with exit zero. Parameter-error oracle is local
Wine 10 dlls/winhttp/tests/winhttp.c and dlls/winhttp/session.c.

Focused regression passes under /tmp/vis-wine; evidence is
/workspace/x64-run/runs/sysx/proxy-contracts.log. This is off-device operation
proof, not Xbox PAC/permission proof. No production route or implementation
changed because the exercised native route already satisfies these checks.
Pending acceptance remains native Xbox operation/export verification after vis
integration and partial DWM policy semantics. Audio remains deferred. No
LANE_DONE exists; whole-area completion is not established.

Unchanged offline smoke passes 2/2 (smoke-proxy-contracts.log); console gates
were explicitly skipped. Shell syntax and git diff --check pass. This test-only
milestone requires no package rebuild or version change. Local commit and the
required backup follow; no existing assertion or test gate was weakened.

Read-only fresh integration check: vis/play-xinput/launch_probe.log lines
688-689 still install fail-stop traps for dbghelp!SymInitialize/SymFromAddr;
its summary resolves zero dbghelp exports. Therefore Xbox 0.5.158.9 cannot
certify this lane's native dbghelp route or its eleven-export discovery output.
The owning vis integration must include sysx production routes before collecting
that evidence. No separate sysx deployment was attempted over the newer shared
console package. Backup is running in backup-proxy-contracts.log.

### sysx resume — bounded native regression, 2026-10-28

Resumed at 04e05da7. Read RTK, REBUILD.md, PLAYABLE.md, and read-only vis/prun
handoffs. Regenerated research/sysx-todo.csv before changes: unchanged 222
owned imports, 20 pending entries, including seven deferred audio entries.
Pre-existing deletions of twenty SoftFloat build files were present on entry;
they are unrelated and remain untouched and excluded from this commit.

Fresh read-only vis evidence in runs/vis/play-resume-20261028/ still reaches
CoreWindow swapchain success and stops at the legitimate real-client Steam
prerequisite. Its boot-late.log reports dbghelp resolved=0/stubbed=2, so that
package still cannot establish this lane's native dbghelp implementation or
its eleven-export diagnostics. No new sysx execution blocker was identified.
No console access, installation, security bypass or Steam change occurred.

The native dbghelp/WinINet runner had no Wine execution deadline and attempted
to read a missing port file if its listener failed. Added explicit listener
readiness failure and a 30-second Wine deadline with a five-second forced-kill
grace period. Existing native assertions and unhandled-fault rejection remain
unchanged. Synthetic runner checks passed: a sleeping Wine replacement exits
124 after 30.2 seconds; an absent listener exits 1 before invoking Wine with
'FAIL: local HTTP listener did not become ready'. These checks establish only
runner behavior, not Windows API semantics. Shell syntax and diff checks pass.

Real native and unchanged offline smoke gates were attempted and could not
start: x86_64-w64-mingw32-g++/gcc, Wine and llvm-readobj are absent from this
host. Native runner exited 127; offline smoke reported 0 passed/2 failed due
to missing tools. Toolchain restoration was attempted; apt index fetches from
Debian HTTPS failed with SSL unexpected EOF, leaving packages unavailable.
Logs: runs/sysx/native-deadline.log, smoke-native-deadline.log,
toolchain-update.log and toolchain-restore.log. This is an environment blocker,
not a green smoke result; earlier off-device passes are historical only.

Pending: restore host toolchain and rerun unchanged gates; integrate sysx
production routes into vis and collect Xbox export/API-operation evidence;
retain explicit unsupported DWM failures. Audio stays deferred. Whole-area
completion is not established, so no LANE_DONE is created. Local checkpoint
and the required backup follow. Nothing is pushed.


### sysx resume — restored host verification, 2026-10-28

Resumed at 7b95e0db on lane/sysx. Read RTK, REBUILD.md, PLAYABLE.md and
read-only vis/prun handoffs. Regenerated research/sysx-todo.csv before any
changes: unchanged 222 owned imports and 20 pending entries, including seven
explicitly deferred audio imports. The twenty pre-existing SoftFloat build-file
deletions remain untouched and excluded from this checkpoint.

The prior host-toolchain blocker is resolved: Wine, MinGW and llvm-readobj are
available again. Reran the unchanged native dbghelp/WinINet, system operations,
Winsock, certificate, WinHTTP PAC and IMM32 suites with WINEPREFIX=/tmp/vis-wine;
all pass. Rebuilt the production user32 record DLL using its existing build.sh,
then compiled and ran the unchanged DWM regression against that exact DLL at
uwph/user32/out/layout/uwph-u32c.dll. This avoids requiring a signed launcher
rebuild merely to populate app/out/layout. DWM also passes, including record
geometry, destroyed handles and explicit unsupported-policy failures. No test
assertion or production implementation was changed.

Evidence: /workspace/x64-run/runs/sysx/{native,system,network,certificate,proxy,
imm,dwm}-restored.log and record-build-restored.log. Unchanged smoke-all.sh
--offline passes 2/2 in smoke-restored.log; console sections are explicitly
skipped. These results replace the prior unavailable host gates for the current
source state. They prove off-device contracts, not Xbox API permissions,
export availability, rendering or gameplay. No console was contacted, package
installed, version selected, Steam behavior changed or security bypass used.

Fresh read-only examination of saved vis/play-resume-20261028/boot-late.log
still shows dbghelp resolved=0/stubbed=2 (line 100), successful CoreWindow
swapchain creation (line 127), then the legitimate Steam LoadLibrary prerequisite
(line 132). The current vis resolver does not contain sysx_native integration.
Consequently this saved package cannot verify our eleven-export diagnostic or
native operation behavior. The updated vis handoff still requires the isolated
execution/IPC seam and real client path from its owners. No new sysx missing
import is identified by the available execution evidence.

Pending: integrate sysx production routes into the console-owning vis package;
collect Xbox export and API-operation evidence; retain unsupported DWM policy
errors. Audio remains deferred. The whole-area acceptance is not established;
LANE_DONE remains absent. Local documentation checkpoint and required backup
follow. Nothing is pushed or posted.


### sysx resume — effective route inventory, 2026-10-28

Resumed at 79190161. Read RTK, lane history and notes, required rebuild/playable
runbooks and read-only vis/prun references. Regenerated research/sysx-todo.csv
before changes: 222 owned imports, 20 pending, including seven deferred audio
imports. The twenty existing SoftFloat deletions remain untouched and unstaged.

Corrected the audit's tree_route field to reflect the symbol-level native
allowlist and IMM32/DWM record builtin overrides. Previously it misleadingly
reported dbghelp as stub, WinINet as unclassified, and DWM as system despite
the production resolver applying these explicit overrides before DLL routes.
Source console_route, pending statuses and completion counts remain unchanged.
This is an inventory correction, not a new production implementation or Xbox
availability claim. Both generated CSVs now show the effective route.

Current native dbghelp/WinINet regression passes under /tmp/vis-wine. Unchanged
offline smoke passes 2/2; console sections explicitly skipped. Evidence:
/workspace/x64-run/runs/sysx/resume-20261028-current/{native,smoke}.log.
Whitespace checks pass. No assertion or test requirement changed.

Read-only vis resolver still lacks sysx_native integration. Its latest saved
Xbox run remains stopped at the legitimate Steam prerequisite after swapchain
creation. Neither that package nor these host tests certify our eleven native
exports or their Xbox operations. Next required milestone remains integration
of this lane's production routes into the console-owner package and bounded
Xbox export/API checks. DWM policy changes retain explicit E_NOTIMPL because
there is no nonclient renderer or policy backend. Audio remains deferred.
No console use, new package version, Steam change, push or LANE_DONE. Whole
area completion remains unproved. Local checkpoint and backup follow.

### sysx resume — actionable integration handoff, 2026-10-28

Resumed at 24af2f43. Read the required runbooks, lane notes and read-only
vis/prun handoffs. Regenerated the symbol inventory before changes: unchanged
222 owned imports, 20 pending rows (eleven native exports/API operations, two
partial DWM entries, seven deferred audio entries). The twenty pre-existing
SoftFloat deletions remain untouched and excluded from the commit.

Verified vis still has no sysx_native resolver integration. Its latest handoff
continues to report the legitimate Steam prerequisite after swapchain creation;
no new sysx execution failure is identified. Added research/sysx/CONSOLE_HANDOFF.md
with the exact production commits/files, resolver ordering, guest-record handle
constraints and separate export-discovery/API-operation acceptance gates. This
makes the pending integration reviewable without overwriting vis lane notes.
No other lane worktree was changed or message sent.

Current native regression passes under /tmp/vis-wine. Unchanged offline smoke
passes 2/2; console sections are explicitly skipped. Evidence is in
/workspace/x64-run/runs/sysx/resume-20261028-handoff/{native,smoke}.log.
Whitespace checks pass. No production behavior or existing test changed.

No Xbox access, package rebuild, version allocation, Steam change or push.
Xbox native operations and partial DWM policy support remain unproved; there
is no backend evidence that justifies replacing E_NOTIMPL with success.
The requested whole-area acceptance remains open, so LANE_DONE is absent.
Local handoff checkpoint and required backup follow.

### sysx resume — SHLWAPI operation proof, 2026-10-28

Resumed at 6ec1dc8e. Read RTK, required rebuild/playable runbooks and read-only
vis/prun handoffs. Regenerated research/sysx-todo.csv before changes: unchanged
222 owned imports, 20 pending rows including seven deferred audio entries.
Twenty pre-existing SoftFloat build-file deletions remain untouched and unstaged.
Current vis notes still identify the legitimate client/process prerequisite;
no new sysx runtime failure or console operation result is available.

Added a separate regression for both imported SHLWAPI exports through the exact
production pe_tail resolver, with address identity against System32 exports.
PathCanonicalizeW checks absolute, UNC and relative paths, drive-root parent
traversal, empty input and null-argument failures. SHDeleteKeyW removes a
populated nested registry subtree, returns ERROR_FILE_NOT_FOUND on repeated
deletion, preserves a sibling, and removes the unique volatile HKCU test root.
The runner has listener-independent 30-second execution and five-second kill
bounds and rejects unhandled Wine faults even with exit zero. No external
service, console or existing user registry key is used.

Initial development incorrectly assumed PathCanonicalizeW supports overlapping
source/output buffers. Local Wine 10 dlls/kernelbase/path.c clears output before
reading source; its dlls/shlwapi/tests/path.c supplies null/empty argument checks.
Corrected the new test's unsupported assumption and added empty-source and
null-output checks. No existing assertion, gate or product route changed.
The completed regression passes with WINEPREFIX=/tmp/vis-wine. Unchanged
smoke-all.sh --offline passes 2/2; console sections were explicitly skipped.
Shell syntax and git diff --check pass. Evidence:
/workspace/x64-run/runs/sysx/path-contracts/{path,smoke}.log.

Also corrected tree_route for explicit pe_tail allowlist entries to native-tail;
HID, setupapi, SHLWAPI and timer rows previously retained their DLL-level stub
label despite their native-tail status and effective symbol route. Pending
counts and source console_route values remain unchanged. This inventory change
makes no new implementation or Xbox availability claim.

No production implementation, package version, Xbox installation, audio or
Steam behavior changed. Pending acceptance remains sysx route integration into
vis, native Xbox export/API-operation evidence and unsupported DWM policies.
Whole-area completion is not established, so LANE_DONE remains absent. Local
milestone commit and required backup follow; nothing is pushed or posted.

### sysx resume — version resource operation coverage, 2026-10-28

Resumed at 76456063 on lane/sysx. Read RTK, current notes/history, required
REBUILD.md/PLAYABLE.md and read-only vis/prun handoffs. Regenerated the owned
symbol diff before changes: research/sysx-todo.csv remains 222 owned imports,
20 pending entries (eleven native Xbox checks, two partial DWM entries, seven
deferred audio imports). Twenty pre-existing SoftFloat build-file deletions
remain untouched and excluded from this commit.

Added a separate version regression and deterministic VERSIONINFO resource.
All three game-imported version APIs must have the production system route
and resolve from System32. The test reads its own temporary PE and checks exact
file/product version words, file type, language/codepage translation, ANSI
strings, missing-value length and ERROR_FILE_NOT_FOUND for size/read calls.
No game file, existing test assertion or production route was changed. The
runner preserves the existing 30-second Wine deadline, five-second kill grace
and unhandled-fault rejection pattern. It modifies no installed file.

The new regression passes under WINEPREFIX=/tmp/vis-wine. Unchanged offline
smoke passes 2/2; console sections explicitly skipped. Shell syntax and
whitespace checks pass. Evidence: runs/sysx/version-contracts/{version,smoke}.log
under /workspace/x64-run. These are host operation results, not Xbox proof.

Read-only vis notes still identify the legitimate real-client/process handoff
as the runtime dependency; sysx console integration and native operations remain
pending as described in research/sysx/CONSOLE_HANDOFF.md. No console contact,
package version, Steam behavior, audio work or security exemption occurred.
Whole-area acceptance remains open; LANE_DONE is absent. Local milestone commit
and required backup follow. Nothing is pushed or posted.

### sysx resume — IP Helper operation coverage, 2026-10-28

Resumed at 9479ea5e on lane/sysx. Read RTK, lane notes/history, required
REBUILD.md/PLAYABLE.md and read-only vis/prun handoffs. Regenerated the owned
symbol diff before changes: unchanged 222 imports and 20 pending rows (eleven
native Xbox checks, two partial DWM entries, seven deferred audio entries).
Twenty pre-existing SoftFloat build-file deletions remain untouched and unstaged.

Added a separate read-only regression for all three imported IP Helper exports
through the production system route classification and actual System32 exports.
GetAdaptersAddresses must satisfy buffer sizing and enumerate IPv4 adapters
including loopback. GetIfEntry must agree on index, interface type, MTU and
physical address. GetNetworkParams must satisfy sizing and return terminated
metadata; null arguments must report ERROR_INVALID_PARAMETER. Buffer-size races
permit at most four retries, exclusively for ERROR_BUFFER_OVERFLOW. No adapter,
DNS or proxy settings change, and the test opens no network connection.

The new regression passes with WINEPREFIX=/tmp/vis-wine. Unchanged offline
smoke passes 2/2; console sections explicitly skipped. Shell syntax and
whitespace checks pass. Evidence: /workspace/x64-run/runs/sysx/adapter-contracts/
{adapter,smoke}.log. These results establish host operation contracts, not
Xbox adapter visibility, AppContainer permissions or gameplay. Existing tests
and production routes were not changed or weakened.

Current vis handoff still waits for the legitimate client/process seam;
sysx integration and native console operation evidence remain pending as
specified in research/sysx/CONSOLE_HANDOFF.md. No new sysx runtime blocker
appears in saved execution evidence. No console contact, package version,
Steam/audio change, push or security exemption occurred. Whole-area Xbox
acceptance remains open; LANE_DONE is absent. Local checkpoint and required
backup follow.

### sysx resume — system regression deadline, 2026-10-28

Resumed at c51fa162 on lane/sysx. Read RTK, lane history/notes, required
REBUILD.md/PLAYABLE.md and read-only vis/prun handoffs. Regenerated the owned
symbol diff before editing: research/sysx-todo.csv remains unchanged at 222
owned imports and 20 pending rows (eleven native Xbox operation/export checks,
two partial DWM entries, seven deferred audio entries). Twenty pre-existing
SoftFloat build-file deletions remain untouched and excluded from this commit.

The system operation runner invoked Wine without a deadline. Added a 30-second
execution limit and five-second forced-kill grace using the established native
runner pattern. No production route, API assertion or existing gate changed.
The unchanged crypto, registry/token and COM/WinRT operations pass under
WINEPREFIX=/tmp/vis-wine. A synthetic sleeping Wine executable exits 124 after
31.8 seconds including compilation, proving timeout failure propagation.
Unchanged smoke-all.sh --offline passes 2/2; console gates explicitly skipped.
Shell syntax and whitespace checks pass. Evidence:
/workspace/x64-run/runs/sysx/system-deadline/{system,timeout,smoke}.log.
These are host results, not Xbox/AppContainer or gameplay certification.

Read-only vis production resolver still lacks sysx_native integration. Saved
play-resume-20261028/boot-late.log:127 records native CoreWindow swapchain
success, followed at line 132 by the legitimate Steam prerequisite. Current
vis notes report no new client/process handoff. No new sysx import failure is
identified. Next required acceptance work remains production integration and
bounded Xbox native API-operation checks described in
research/sysx/CONSOLE_HANDOFF.md. Unsupported DWM policies retain E_NOTIMPL;
audio remains deferred. No console use, version allocation, Steam change,
security exemption or push occurred. Whole-area completion remains unproved;
LANE_DONE is absent. Local checkpoint and required backup follow.

### sysx resume — acceptance audit at e4b49aad, 2026-10-28

Read current lane notes/history, RTK, required REBUILD.md/PLAYABLE.md and
read-only vis/prun handoffs. Regenerated research/sysx-todo.csv before work;
unchanged: 222 owned imports, 20 pending rows. Seven are deferred audio,
eleven require Xbox native export/API-operation evidence, and two DWM rows
retain explicit unsupported-policy failures. No newly missing route appeared.

Confirmed this branch invokes sysx_native::resolve and discovery in the
production resolver. The vis worktree still lacks that integration. Saved
vis/play-resume-20261028/boot-late.log:127 records CoreWindow swapchain S_OK;
line 132 records the legitimate real-client LoadLibrary prerequisite. Current
vis notes provide no new execution/client implementation. Integration steps
and separate discovery/operation gates remain in research/sysx/CONSOLE_HANDOFF.md.
No other worktree was edited and no message was sent externally.

Fresh native and system operation regressions pass under /tmp/vis-wine.
Unchanged smoke-all.sh --offline passes 2/2; console sections are skipped.
Evidence: /workspace/x64-run/runs/sysx/acceptance-e4b49aad/{native,system,smoke}.log.
These are off-device proofs, not Xbox export, AppContainer, rendering or
playable-game acceptance. No implementation or assertion changed. Existing
matching-state host proofs remain applicable; no unrelated work was added.

No console contact, package version, Steam/audio change, security exemption or
push occurred. Twenty pre-existing SoftFloat deletions remain untouched and
excluded from this checkpoint. Whole-area acceptance remains open; LANE_DONE
is absent. Required next step is console-owner integration and bounded native
operation verification, preserving the legitimate-client guard and unsupported
DWM errors. Local documentation checkpoint and required backup follow.

### sysx resume — route and acceptance check at 6c8adc29, 2026-10-28

Read RTK, current lane notes/history, required REBUILD.md/PLAYABLE.md and
read-only vis/prun handoffs. Regenerated research/sysx-todo.csv before work:
unchanged 222 owned imports, 20 pending rows. Eleven require Xbox native
export/API-operation evidence; two retain unsupported DWM policy operations;
seven are deferred audio. No new missing route or sysx execution failure was
identified. Existing implementation and assertions remain unchanged.

Fresh native operation regression passes with WINEPREFIX=/tmp/vis-wine.
Unchanged smoke-all.sh --offline passes 2/2; console sections explicitly skip.
Evidence: /workspace/x64-run/runs/sysx/resume-6c8adc29/{native,smoke}.log.
These are host results, not Xbox or AppContainer operation certification.

Checked the existing diagnostic path: sysx discovery runs through production
mlp_probe.h in this branch, but read-only vis still lacks sysx_native calls.
Its saved boot-late.log:127 shows CoreWindow swapchain S_OK; line 132 stops
at the legitimate real-client prerequisite. No independent sysx operation
entrypoint exists in that installed launcher. Console-owner integration and
bounded native-operation verification remain the concrete next milestone in
research/sysx/CONSOLE_HANDOFF.md. No guard or unsupported DWM error was changed
to obtain artificial progress.

No console contact, deployment, package version, Steam/audio change or push.
Twenty pre-existing SoftFloat deletions remain untouched and unstaged.
Whole-area Xbox acceptance remains open; LANE_DONE is absent. Local notes
checkpoint and required backup follow.

### sysx resume — dependency unchanged at 03c46583, 2026-10-28

Regenerated research/sysx-todo.csv before work after reading the required
runbooks and current vis/prun handoffs. Inventory is unchanged: 222 owned
imports, eleven native Xbox export/operation checks, two partial DWM policy
entries, and seven audio entries outside first-version scope. No new missing
route is identified. Production routes and test assertions remain unchanged.

Fresh native regression passes under WINEPREFIX=/tmp/vis-wine. Unchanged
smoke-all.sh --offline passes 2/2, with console sections explicitly skipped.
Evidence: /workspace/x64-run/runs/sysx/resume-03c46583/native.log and
/workspace/x64-run/runs/sysx/resume-03c46583-smoke.log. These are off-device
results only. This branch calls sysx_native::resolve/discover; read-only vis
still lacks those calls. Its latest handoff reports no new legitimate-client
or process implementation. The integration and console operation gates remain
specified in research/sysx/CONSOLE_HANDOFF.md. No additional host-only result
can establish Xbox export availability or AppContainer operation permissions.

No console use, package version, Steam/audio change, unsupported-policy success,
other lane edit or push. Twenty pre-existing SoftFloat deletions stay untouched
and unstaged. Xbox acceptance remains incomplete; LANE_DONE is absent. Await
console-owner route integration and native operation evidence. Local checkpoint
and required backup follow.
