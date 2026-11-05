# Lane vis notes (exported from the local LANE_NOTES.md)

Lines this lane added to the shared LANE_NOTES.md between 494a54020 and c9f759cd1.


### vis resume — deployment and foreground diagnosis, 2026-10-27

Resumed clean at 494a5402. Existing caps and notification integration is already
committed. Previous full console smoke completed 5 passed/4 failed, including
fresh-launch/D3DT and TLS timeouts; it is not green. Portal health returns 200.
Attempted the existing signed player-log package 0.5.158.2: upload accepted 202,
but install state stays HTTP 204 and installed identity remains 0.5.158.1.
A subsequent 0.5.158.3 upload returns 409, "Another deployment is running".
Neither newer version is claimed installed. No Play occurred this resume yet.
The old launcher process remains PID 4800, about 438 MiB working set; ordinary
package stop returns HTTP 400. Console user is already signed in. Diagnosing
normal Portal app/deployment state before further graphics changes. The
one-off shell lock nesting mistake was corrected before console upload;
subsequent transactions share inherited descriptor 9 with DP_LOCK_FD=9.

Normal Portal diagnostics found screenshot HTTP 500 and the launcher still
running after stop; no PID-kill capability was available (403; not bypassed).
The documented restart request with normal cookie/CSRF authentication returned
200. After reboot, screenshot capture works again. Fresh deployment of the
already built 0.5.158.3 was accepted; installed package list now verifies that
exact identity, even while the install-state endpoint reports 204. Actual Play
is running on that verified package. Evidence: restart-auth.log,
screen-reboot.png, packages-install3.json and deploy-after-reboot.log.

Fresh Play on verified 0.5.158.3 exits 0 and executes guest code; Unity still
hits DialogBoxParamW after two real D3D11CreateDevice successes. Retrieved the
actual console vis-player.log. It reports Unity 2023.2.22f1, D3D11 feature level
11.0, Microsoft SraKmd_arden renderer, then the concrete fatal condition:
"Switching to resolution 1920x1080 failed" and
"Screen: DX11 could not switch resolution (1920x1080 fs=1 hz=0/1)".
Missing XInput1_3/XInput9_1_0 is logged as reduced controller functionality,
not this fatal cause. No audio or Steam work was performed.

Changed only the launch option to -screen-fullscreen 0 to test Unity's windowed
startup against the console backend; this does not fabricate an API response.
Signed build passes; all 13 existing off-device runtime modes also pass.
Full unchanged smoke after reboot passes the first eight sections, including
fresh launch and 300 real presented D3D frames; TLS remains in progress.
The first smoke rerun was cancelled before console access because an outer
lock wrapper conflicted with smoke-all's own health-check lock. Reran the
unchanged script using its internal locking. No assertion was changed.

Full unchanged console smoke has completed 9 passed/0 failed after normal
restart. Evidence: smoke-reboot-unwrapped.log and
/workspace/x64-run/runs/smoke-evidence-2026-10-27-141059-hq1cY0/.
The windowed build deployment selected fresh version 0.5.158.4 under the lock.

Verified 0.5.158.4 Play with -screen-fullscreen 0 still reaches exactly the
same resolution fatal, now fs=0. Thus the option alone does not fix graphics;
retain it for the CoreWindow bridge's supported windowed presentation contract.
No swapchain/menu/frame is claimed. Found the existing validated SWC factory
adapter in src/xbox-test/uwph/host/swc_dxgi.h: it already translates validated guest
HWND requests to CreateSwapChainForCoreWindow. Play currently routes DXGI to the
native system factory instead, so the existing record HWND has no such bridge.
Next: extract/reuse that adapter, bind it to this runtime's record registry and
UI CoreWindow, and verify the actual native HRESULT before claiming rendering.

Extracted the existing SWC factory into shared/corewindow_factory.h with
constructor-supplied window lookup and result logger; SWC's calls, errors,
presentation conversion and guest execution remain unchanged. Historical SWC
strict checker passes before extraction. Fresh SWC package 0.1.0.1 verifies
installed after retrying a dropped upload. Console checker reports both HWND
and legacy paths, 300 exact-S_OK Presents each and zero RWX after extraction.
The box's own Tailscale reported SelfOnline=False before the upload retry;
ran the user-prescribed tailscale up command without printing credentials.

Milestone backup finished ok=19/fail=1, not complete success: wt/prun's bundle
hit the store's "File too large" error. This lane's bundle succeeded. No claim
that the failed all-worktree backup completed cleanly; retry after next commit.

Play integration now reuses the shared adapter for CreateDXGIFactory[1/2],
validates HWNDs with the current user32 record registry and supplies the real
CoreWindow only after validation. Surface creation queues to the UI dispatcher;
the UI releases the launcher's backbuffer/flip chain before native guest chain
creation, and continues pumping events while guest presentation owns the
surface. Native errors remain errors; no guest handle is forwarded to DXGI.
The factory2 path preserves requested native factory flags. New off-device
contract regression passes 34 checks: invalid handles, null outputs, unsupported
interfaces, error propagation, CoreWindow references, legacy conversion and
unsupported multisampling/fullscreen. Its injected surface callback always
returns E_ABORT; it proves routing contracts, never rendering. Final signed
build/runtime regression and fresh Xbox Play are pending.

### vis resume — CoreWindow bridge validation, 2026-10-27

Resumed with the pending graphics integration edits preserved. Read the lane
handoff and required runtime documents. Signed launcher build now passes
(build-corewindow-resume.log); all thirteen unchanged Play runtime modes pass
off-device (runtime-corewindow.log). The factory contract regression passes
34 checks off-device (factory-resume.log). No rendering claim follows from
these host checks. Fresh version 0.5.158.5 was signed after confirming installed
0.5.158.4, then accepted by Portal under the shared console lock. Console Play
and native swapchain results are being collected in play-corewindow/.

Console 0.5.158.5 Play proves the bridge creates Unity's native CoreWindow
swapchain: boot-late.log records `guest surface handoff native hr=0x00000000`
and `guest DXGI CoreWindow swapchain returned hr=0x00000000`. Unity no longer
reports the resolution fatal. Its next real-console stop is
user32.dll!PeekMessageA at UnityPlayer+0x741157. Console screenshots are black;
no presented frame, menu, stability or gameplay is claimed. Evidence is under
/workspace/x64-run/runs/vis/play-corewindow/. The unused factory5 interface
reports E_NOINTERFACE honestly; Unity continues past it.

Implemented the exact next missing pump API in the record module, with a real
per-thread posted-message queue, scoped filtering/removal, validated record
window ownership, dispatch and quit posting. No record HWND reaches native
user32. This is explicitly not a complete Wine server or keyboard/paint/timer
producer. Focused packaged-DLL regression passes 48 checks off-device; existing
mapped user32 lifecycle regression returns 42. The first lifecycle invocation
used the wrong working directory and failed to open its fixture; corrected
invocation runs from out/layout and passes. Evidence: message-queue-test.log
and user32-message-regression.log. Full smoke's default Wine prefix fails the
host dispatch prerequisite (exit 53); console sections are still running.
Rerun the unchanged gate using the previously validated /tmp/vis-wine prefix.

### vis — first visible game content, 2026-10-27

Verified package 0.5.158.6 advances beyond PeekMessageA on the real Xbox.
The console screenshot play-queue/4-final.png visibly contains the game's
`Loading...` text over black. This is first visible game content, not a main
menu or gameplay milestone. Unity again creates the native swapchain with S_OK.
Next real stop in boot-late.log: two failed Rewired_WindowsGamingInput.dll
loads (error 126), then unhandled C++ exception 0xe06d7363 at kernelbase.dll+
0xc79da. Causation is not established: that optional plugin is also absent
from the local owned-game reference, so do not add a fake DLL or suppress the
exception. Add bounded exception type/stack diagnostics to identify the actual
throw path. No audio or Steam change was made. No LANE_DONE exists.

Unchanged full smoke now passes 9/9 with WINEPREFIX=/tmp/vis-wine:
smoke-corewindow-freshprefix.log and smoke-evidence-2026-10-27-184309-JpOh7B/.
All seven console sections also passed in the preceding 8/9 attempt; its only
failure was the default off-device Wine prefix. The new exception diagnostics
build/sign passes; thirteen runtime modes pass off-device. The diagnostics
inspect bounded mapped throw metadata and unwind a copied context for at most
20 frames; they never mutate the actual exception context or select a handler.
Added raw C++ record parameters to distinguish missing image-base metadata
from an ordinary unhandled game exception. Fresh 0.5.158.7 deployment/Play is
in progress under CONSOLE.lock. Prior prun TLS notes explicitly document that
their synthetic C++ test supplied a constrained throw-base adapter; that proof
does not establish native mapped-game C++ compatibility.

### vis resume — native C++ image identity, 2026-10-27

Preserved pending native C++ fixture edits on resume at 307d72dd. Fresh Xbox
Play on verified 0.5.158.7 repeats Loading... and the unhandled 0xe06d7363.
The new raw diagnostics establish imageBase=0x0 and magic=0x1994000 in the
actual game throw. Evidence: runs/vis/play-exception/boot.session.log.
The first console wrapper used two distinct flock descriptors and blocked
before console work; terminated that wrapper and reran with inherited locked
fd 9. No lock was bypassed.

A genuine static Microsoft CRT throw/catch DLL, manually mapped by the same
runtime under Wine, reproduces imageBase=0x0 and fails to catch its own integer.
This is off-device evidence. The initial fixture linked WindowsApp API-set
names outside the existing import allowlist and stopped before throwing;
relinked unchanged fixture object against desktop kernel32 imports as used by
the owned game. Evidence: native-cxx-kernel-before.log. No fake throw adapter.

Implemented RtlPcToFileHeader in Play's process-identity layer: lock the owning
mapped-image registry, resolve interior addresses to their real mapped base,
and delegate all other addresses to the real native export. Wine's
ntdll/loader.c:4558 supplies the reference contract. This changes neither the
exception record nor catch decisions. Regression/build validation is pending.

The first updated full runtime run passes all fourteen modes, including the
native Microsoft CRT catch with an intervening destructor (return 42).
This establishes regression recovery off-device only. Added focused mapped
interior/end, absent-address and native-fallback lookup checks to the fixture;
final rerun and signed build remain in progress.
Final fourteen-mode runtime suite passes, including mapped last-byte lookup,
invalid-address clearing, native-image fallback and native C++ destruction.
Signed launcher build passes; unchanged offline smoke passes 2/2. Evidence:
runtime-native-cxx-final.log, build-cxx-image-base.log, smoke-cxx-offline.log.
Committed runtime and regression as f6f1ac6; fresh console deployment follows.

Real Xbox 0.5.158.8 passes the native C++ failure and advances to the enforced
real-client prerequisite: steam_api64.dll!LoadLibrary at UnityPlayer+0x4f4398
(function RVA +0x4f3ff0). No Steam DLL code was executed or replaced. The
console screenshot remains Loading..., not a menu. Evidence: play-cxx/boot.log
and boot-late.log. The player-log pull used the wrong directory and returned
404; it supplies no new player-log evidence. Full unchanged smoke is running.

The same fresh trace records repeated missing XInput dynamic modules. Porting
only the existing gfxin owner's WGI XInput routes, built-in module identities,
controller poller and focused tests. Keep this lane's working CoreWindow and
new C++ runtime intact; do not import the owner's older launcher wholesale.
Source references: dffc931b loader/module implementation and current gfxin
route/poller corrections. This does not bypass the Steam blocker.

Unchanged full console smoke after the C++ fix passes 9/9; evidence:
smoke-cxx-console.log and smoke-evidence-2026-10-27-234339-8laeFF/.
Owner XInput unit/Wine differential, real WGI structure conversion and actual
mapped input guest all pass off-device. The mapped fixture returns 42; its
injected host controller state is synthetic and proves no console input.
Signed integration build passes (xinput-build.log). Final fourteen-mode
runtime run and console XInput integration Play remain pending.

### vis — current real-console integration checkpoint

Fresh locked deployment verifies Xboxnative.Launcher_0.5.158.9_x64__dk715arakrne0.
Unchanged play-test.sh 3405340 exits 0. Native guest D3D11 and CoreWindow
swapchain return S_OK again; screenshot play-xinput/4-final.png still shows
Loading.... Repeated XInput module-load failures from 0.5.158.8 are absent
with the owner implementation integrated. No controller is attached (pads=0),
so real-pad behavior is not claimed. Off-device mapped controller input remains
explicitly synthetic. All fourteen runtime modes pass after integration.

Actual next stop remains the enforced real-client Steam prerequisite at
UnityPlayer+0x4f4398. No further vis code change can remove that prerequisite
within the assigned no-Steam/no-bypass scope. Shared owner handoff is
/workspace/x64-run/BLOCKED-vis.md. Optional Rewired plugin remains absent and
a secondary GameAssembly CreateWindowExW returns error 1400; neither is
established as the immediate stop and neither is fabricated successful.

Evidence: runs/vis/play-xinput/{boot.log,boot-late.log,launch_probe.log,
launch_request.json,launch_result.json,4-final.png,vis-player.log},
deploy-xinput.log, xinput-{owner,conversion,mapped,build}.log and
runtime-xinput-final.log. Player log was retrieved successfully from
LocalState\megabonk; earlier games\megabonk 404 is superseded only here.
The fps~ values in these logs belong to the launcher before surface handoff;
they are NOT measured Unity/gameplay fps. Menu stability, input-started run,
five-minute gameplay and game fps remain unproved. LANE_DONE is absent.
Local commits only, no push. Full smoke after input integration is running;
required backup is in progress and its final status remains to be recorded.

Final unchanged full console smoke after XInput integration passes 9/9.
Evidence: smoke-xinput-console.log and
/workspace/x64-run/runs/smoke-evidence-2026-10-27-234819-oYYCqX/.
This validates the shared console gates, not Megabonk menu/gameplay acceptance.
Working tree is clean after the local checkpoint commits; no LANE_DONE exists.
The required backup remains running across all worktrees.

Backup script completed: ok=19, skipped=0, fail=1. This lane's wt_vis bundle
succeeded. wt_sysx failed copying with File exists; an earlier prune also
reported Transport endpoint is not connected. This is not full backup success.
Retrying the failed bundle with a unique store name and SHA-256 verification.

### vis resume audit — 2026-10-28

Resumed at 7b44f00d with a clean lane/vis worktree. Re-read REBUILD.md,
PLAYABLE.md, prun handoff, current vis evidence and stm owner notes. Existing
console evidence remains the latest result; no new console execution occurred
in this audit. The production guard in src/xbox-test/app/host/play_runtime.h still
matches play-xinput/boot-late.log: steam_api64.dll!LoadLibrary (real client
prerequisite item 36), UnityPlayer+0x4f4398. The stm owner still requires the
isolated guest process/IPC execution seam before its genuine-client work.
There is no new owner integration that resolves this prerequisite. Removing
the guard or fabricating Steam success is outside authorization.

Confirmed backup-cxx-retry.log reports the failed wt_sysx bundle was retried
under a unique name and verified, including the latest lane/vis checkpoint.
The original all-worktree backup remains recorded as 19 successes/1 failure;
the targeted retry supplies the recovery evidence. No new visual milestone,
menu, gameplay FPS or five-minute stability is claimed. No LANE_DONE exists.
Resume locked console Play when the legitimate Steam dependency is supplied.

### vis — fresh Xbox blocker confirmation, 2026-10-28

Resumed clean at 65f1c1fa. Read required runbooks, prun handoff and current
stm/upmp owner notes. GetDeviceCaps integration already exists in fb037384;
no older owner launcher was imported. Portal health returns HTTP 200.
Fresh unchanged play-test.sh 3405340 on installed 0.5.158.9 exits 0 with
GAP summary and gameCodeExecuted=true. No package was rebuilt or reinstalled.

Real console boot-late.log again records native surface handoff and CoreWindow
swapchain HRESULT 0x00000000, then the exact enforced stop at
steam_api64.dll!LoadLibrary (real client prerequisite item 36),
UnityPlayer+0x4f4398. Inspected 4-final.png: visible Loading..., no main menu.
One-shot launcher input is synthetic. Launcher fps values are not game fps.
Evidence: /workspace/x64-run/runs/vis/play-resume-20261028/ contains acceptance,
boot/late/probe logs, request/result JSON and console screenshots.

The initial wrapper omitted opening fd 9, so its outer flock failed; helper
calls still acquired their own console locks. Stopped that wrapper and reran
with fd 9 opened on CONSOLE.lock and inherited DP_LOCK_FD=9 for the complete
transaction. No console security restriction or Steam prerequisite was bypassed.

No product source changed. Latest unchanged full smoke remains the prior 9/9
input-integration result; no fresh smoke result is claimed. stm still requires
isolated guest processes and its callable console fixture before genuine-client
integration. That external dependency prevents further authorized visual/game
progress. No audio work, remote push or LANE_DONE. Resume Play after owner supply
of the legitimate client path; menu stability, input-started run and measured
five-minute gameplay remain unproved.

### vis — dependency audit on resume, 2026-10-28

Resumed at e27c76e1 on lane/vis. Read RTK instructions, required runbooks,
prun handoff and current stm/upmp notes. GetDeviceCaps and later graphics,
message-queue, native C++ and XInput integrations are already committed.
The production runtime still explicitly stops steam_api64.dll/steamclient64.dll
loads at the real-client prerequisite. stm still lacks its required isolated
process/IPC console execution seam; no new owner implementation is available
for integration. The upmp branch's old mapping-only blocker describes its own
older runtime and must not be applied to vis's proven guest execution.

No new console transaction, rebuild, smoke result or visual milestone is
claimed in this audit. Latest console evidence remains play-resume-20261028;
latest full smoke remains the recorded 9/9 input-integration run. Removing the
Steam guard or substituting a client is outside this lane's authorization.
Menu, five-minute stability, input-started run and gameplay FPS remain blocked.
LANE_DONE remains absent. Existing deletions of 20 tracked SoftFloat build
files were present on resume and are left untouched and unstaged.

### vis — latest owner handoff audit, 2026-10-28

Resumed at cbc5bafe. Re-read RTK, required runbooks, prun handoff and vis
notes. Checked current stm head 03a1fef4 and upmp head a69e6383. The newest
stm handoff removes the stale memory planning gate but still requires the
execution owner to supply isolated guest processes and the callable console
process/IPC fixture seam. It supplies no client implementation to integrate.
Production play_runtime.h still explicitly stops steam_api64.dll and
steamclient64.dll loads at the legitimate-client prerequisite.

GetDeviceCaps and subsequent visual/input integrations are already present.
Latest Xbox evidence remains play-resume-20261028: native D3D11/CoreWindow
swapchain success and Loading..., followed by the enforced Steam stop. No
fresh console transaction, rebuild, smoke result, menu, stability or gameplay
FPS is claimed. Repeating Play without a changed prerequisite cannot establish
the missing milestone. Continue after the execution/Steam owners supply the
legitimate client path; do not remove the guard or fabricate API success.

The 20 pre-existing SoftFloat build-file deletions remain untouched and
unstaged. No audio work, remote push or LANE_DONE.

### vis — resumed prerequisite check, 2026-10-28

Resumed at 61d812f2 and verified the required runbooks and owner handoffs.
Owner heads remain stm 03a1fef4 and upmp a69e6383; neither supplies a new
legitimate-client integration. Existing GetDeviceCaps integration remains
present. Rechecked saved real-console play-resume-20261028 evidence: guest
CoreWindow swapchain returns S_OK, followed by the enforced Steam LoadLibrary
prerequisite at UnityPlayer+0x4f4398. This is saved evidence, not a fresh run.

No source change, console transaction, rebuild, smoke rerun or new milestone
is claimed. Further progress requires the execution/Steam owners' isolated
process/IPC seam and genuine client path. Removing the enforced prerequisite
is outside this lane's scope. Menu, stability, input-started run and game FPS
remain unproved. No LANE_DONE. The 20 pre-existing SoftFloat build-file
deletions remain untouched and unstaged. Local documentation commit only.

### vis — updated stm dependency checked, 2026-10-28

Resumed at a504193f. Read required runbooks and prun handoff. The new stm
head b3ef3388 changes documentation only and explicitly confirms the missing
isolated guest process implementation and callable Xbox parent/child seam.
It supplies no genuine-client code for this lane to integrate. Current vis
play_runtime.h:151-152 still enforces the real-client LoadLibrary prerequisite.
Saved play-resume-20261028/boot-late.log:127 records native CoreWindow swapchain
S_OK; line 132 records the subsequent Steam stop at UnityPlayer+0x4f4398.
These are existing Xbox results, not a new console run.

No console access, rebuild, installation, smoke rerun or milestone claimed.
Latest full smoke remains the recorded 9/9. No main menu, five-minute stability,
input-started run or gameplay FPS is established. Continue after the execution
and Steam owners provide the legitimate client path. No LANE_DONE or push.
The twenty pre-existing SoftFloat build-file deletions remain untouched.

### vis — current dependency verification, 2026-10-28

Resumed at 8ae5683a. Read required runbooks, prun handoff and current owner
notes. stm remains b3ef3388 and upmp remains a69e6383; neither provides the
isolated guest process/IPC seam or legitimate client integration. Confirmed
GetDeviceCaps is already integrated by fb037384. The current Steam load guard
remains enforced; the saved Xbox evidence still establishes swapchain S_OK
followed by that prerequisite, not menu or gameplay.

No console transaction, rebuild, installation or new test result occurred.
Latest full smoke remains the recorded 9/9. No milestone was reached, so no
LANE_DONE was created. Twenty pre-existing SoftFloat deletions remain untouched.
Further visual acceptance requires the execution/Steam owner handoff described
in /workspace/x64-run/BLOCKED-vis.md and BLOCKED-upmp.md. Local notes only;
no push, audio work, fabricated Steam success or security bypass.

### vis — resume at 84179218, 2026-10-28

Read the required runbooks and prun handoff. Verified GetDeviceCaps integration
fb037384 and the current runtime guard at play_runtime.h:151-152. Owner heads
remain stm b3ef3388 and upmp a69e6383; no new legitimate-client implementation
or isolated process/IPC seam is available. Saved Xbox evidence in
play-resume-20261028/boot-late.log still records swapchain S_OK at line 127
and the Steam prerequisite at UnityPlayer+0x4f4398 at line 132.

No new console use, build, install or test result. The unmet dependency belongs
to the execution/Steam handoff; no authorized visual fix removes it. Main menu,
five-minute stability, input-started run and gameplay FPS remain unproved.
No LANE_DONE or push. Twenty existing SoftFloat deletions remain untouched.

### vis — resume dependency check at 9e40efe8, 2026-10-28

Read RTK, REBUILD.md, PLAYABLE.md, prun handoff and current vis/stm notes.
GetDeviceCaps integration fb037384 is already present. Owner heads remain
stm b3ef3388 and upmp a69e6383; neither supplies a new legitimate client path
or the required isolated guest process/IPC seam. Current production runtime
continues to enforce the Steam LoadLibrary prerequisite. Saved real-Xbox
play-resume-20261028 evidence confirms native CoreWindow swapchain S_OK then
that stop. No fresh console execution or test result is claimed.

No authorized visual change resolves this external dependency. Menu stability,
input-started run and measured gameplay remain unproved. Latest full smoke
remains the recorded 9/9. No rebuild, install, push or LANE_DONE. Existing twenty
SoftFloat build-file deletions remain untouched and unstaged. Resume console
Play when execution/Steam owners supply the legitimate client integration.

### vis — resume at c4a09cbd, 2026-10-28

Re-read required instructions, runbooks, prun handoff and current stm notes.
Verified fb037384 already integrates GetDeviceCaps. Owner heads remain
stm b3ef3388 and upmp a69e6383, with no new client/process integration.
Saved real-console boot-late.log still records CoreWindow swapchain S_OK
then steam_api64.dll!LoadLibrary (real client prerequisite item 36) at
UnityPlayer+0x4f4398. Current production guard agrees with that evidence.

No new console run, build, install or test result. Further visual acceptance
requires the legitimate client/process owner handoff; removing its guard is
outside this lane. Menu, five-minute stability, input-started run and gameplay
FPS remain unproved. No LANE_DONE or push. Twenty pre-existing SoftFloat
build-file deletions remain untouched and unstaged.

### vis — resume at 817b6c86, 2026-10-28

Verified the required runbooks, prun handoff, current owner branches and saved
Xbox evidence. GetDeviceCaps is already integrated by fb037384. stm remains
b3ef3388 and upmp remains a69e6383; neither supplies the missing isolated guest
process/IPC seam or legitimate client path. Saved play-resume-20261028 evidence
records CoreWindow swapchain S_OK, then the enforced Steam prerequisite at
UnityPlayer+0x4f4398. No new console result is claimed.

No code change, rebuild, installation or smoke rerun is justified by an unchanged
external dependency. Menu, five-minute stability, input-started run and gameplay
FPS remain unproved. No LANE_DONE or push. The twenty pre-existing SoftFloat
build-file deletions remain untouched. Resume console acceptance after the
execution/Steam owner handoff; do not remove or bypass the client prerequisite.

### vis — resume at 3e89a768, 2026-10-28

Read required runbooks and prun handoff; checked current stm b3ef3388 and
upmp a69e6383. Neither provides a new legitimate-client/process integration.
Verified fb037384 is an ancestor of this branch. Saved real-console evidence
play-resume-20261028/boot-late.log:127 shows CoreWindow swapchain S_OK;
line 132 shows the enforced Steam LoadLibrary prerequisite at
UnityPlayer+0x4f4398. Current production guard still matches that evidence.

No fresh console transaction, build, installation or test result is claimed.
Latest full smoke remains the previously recorded 9/9. Menu, five-minute
stability, input-started run and measured gameplay remain unproved. Further
progress requires the execution/Steam owner handoff. No LANE_DONE or push.
Twenty pre-existing SoftFloat deletions remain untouched and unstaged.

### vis — resume at 58f29291, 2026-10-28

Read required runbooks, prun handoff and current stm dependency notes. Verified
GetDeviceCaps commit fb037384 is already an ancestor. stm b3ef3388 and upmp
a69e6383 remain unchanged; no legitimate-client/process handoff is available.
Saved Xbox boot-late.log:127 confirms CoreWindow swapchain S_OK; line 132
records the Steam prerequisite at UnityPlayer+0x4f4398. The production guard
still matches. This is existing evidence, not a fresh console test.

No source change, build, install or test rerun. Further visual acceptance waits
on the execution/Steam owners. Menu, five-minute stability, input-started run
and gameplay FPS remain unproved. No milestone, LANE_DONE or push. Twenty
pre-existing SoftFloat deletions remain untouched and unstaged.

### vis — resume at 4482ae47, 2026-10-28

Read the required runbooks and prun handoff. GetDeviceCaps integration fb037384
is already an ancestor. Current owner heads remain stm b3ef3388 and upmp
a69e6383; neither supplies the isolated process/IPC seam or legitimate client
integration. Production still enforces the Steam LoadLibrary prerequisite.
Saved Xbox play-resume-20261028 evidence records native CoreWindow swapchain
S_OK followed by that prerequisite; this is not fresh console evidence.

No source change, console transaction, build, install or new test result.
Menu, five-minute stability, input-started run and gameplay FPS remain unproved.
Resume after the execution/Steam owner handoff. No LANE_DONE or push. The twenty
pre-existing SoftFloat build-file deletions remain untouched and unstaged.

### vis — resume at b9272aee, 2026-10-28

Read required runbooks, prun handoff and current stm notes. Confirmed
GetDeviceCaps integration fb037384 is already an ancestor. Owner heads remain
stm b3ef3388 and upmp a69e6383; no new legitimate-client/process integration
is available. Saved Xbox play-resume-20261028/boot-late.log:127 records native
CoreWindow swapchain S_OK; line 132 records the enforced real-client Steam
LoadLibrary prerequisite at UnityPlayer+0x4f4398. Production guard remains.

No fresh console transaction, build, install or test result. Further acceptance
requires the execution/Steam owner handoff. Main menu, five-minute stability,
input-started run and gameplay FPS remain unproved. No LANE_DONE or push.
Twenty pre-existing SoftFloat build-file deletions remain untouched and unstaged.

### vis — owner branch verification at 46de23e8, 2026-10-28

Read required instructions, runbooks, prun handoff and current lane notes.
GetDeviceCaps integration fb037384 remains an ancestor. stm advanced to
ce3fde3e, but the delta from b3ef3388 changes LANE_NOTES.md only; it supplies
no isolated process/IPC seam or legitimate client implementation. upmp remains
a69e6383. Production play_runtime.h still enforces the real-client prerequisite.
Saved Xbox play-resume-20261028/boot-late.log:127 records native CoreWindow
swapchain S_OK; line 132 records the Steam stop at UnityPlayer+0x4f4398.
These are saved results, not fresh console execution.

No product source change, console transaction, rebuild, install or new test
result. Latest full smoke remains the previously recorded 9/9. Main menu,
five-minute stability, input-started run and measured gameplay remain unproved.
Resume after the execution/Steam owners supply the required handoff. No
LANE_DONE or push. Twenty pre-existing SoftFloat deletions remain untouched.

### vis — resume at de6279a9, 2026-10-28

Read required runbooks, prun handoff and current owner notes. GetDeviceCaps
commit fb037384 is already integrated. stm remains ce3fde3e; its delta from
b3ef3388 changes documentation only. upmp remains a69e6383. No new isolated
guest process/IPC seam or legitimate-client implementation is available.
Saved Xbox play-resume-20261028/boot-late.log:127 records CoreWindow swapchain
S_OK; line 132 records the enforced Steam prerequisite at UnityPlayer+0x4f4398.
These are saved results, not fresh console evidence.

No source change, console transaction, build, install or test rerun. Visual
acceptance still requires the execution/Steam owner handoff. Menu, five-minute
stability, input-started run and gameplay FPS remain unproved. No LANE_DONE
or push. Twenty pre-existing SoftFloat deletions remain untouched and unstaged.

### vis — resume at b55c2fd4, 2026-10-28

Read RTK, required runbooks, prun handoff and current owner notes. Confirmed
GetDeviceCaps integration fb037384 is already an ancestor. Owner heads remain
stm ce3fde3e and upmp a69e6383; stm's changes since b3ef3388 are documentation
only. No new legitimate client/process implementation is available. Current
production load guard matches saved Xbox play-resume-20261028 evidence:
native CoreWindow swapchain S_OK, then the real-client Steam prerequisite at
UnityPlayer+0x4f4398. This is saved evidence, not a fresh console test.

No source change, console use, build, install or new test result. Further
visual acceptance requires the execution/Steam owner handoff. Main menu,
five-minute stability, input-started run and gameplay FPS remain unproved.
No LANE_DONE or push. Twenty pre-existing SoftFloat build-file deletions
remain untouched and unstaged.

### vis — dependency audit at b3355806, 2026-10-28

Rechecked required runbooks, prun handoff and owner refs. GetDeviceCaps commit
fb037384 is already integrated. stm ce3fde3e contains only notes since b3ef3388;
upmp remains a69e6383. No client/process implementation is available to port.
Saved Xbox play-resume-20261028/boot-late.log lines 127 and 132 establish native
CoreWindow swapchain S_OK followed by the real-client LoadLibrary prerequisite
at UnityPlayer+0x4f4398. These are saved results, not a fresh console run.

No rebuild, install, console use or test rerun occurred. Menu, five-minute
stability and input-started gameplay with measured FPS remain unproved. Resume
acceptance after the execution/Steam handoff in BLOCKED-vis.md/BLOCKED-upmp.md.
No LANE_DONE or push. Twenty pre-existing SoftFloat deletions remain untouched.

### vis — resume verification at ba25731c, 2026-10-28

Read required runbooks, prun handoff and current owner notes. Confirmed
fb037384 already integrates GetDeviceCaps. Owner heads remain stm ce3fde3e
and upmp a69e6383; no new client/process implementation is available to port.
Saved Xbox play-resume-20261028/boot-late.log:127 reports native CoreWindow
swapchain S_OK; line 132 reports the enforced real-client LoadLibrary
prerequisite at UnityPlayer+0x4f4398. Production guard still matches.

No fresh console run, build, install or test result. Main menu, five-minute
stability, input-started run and gameplay FPS remain unproved. Further work
requires the execution/Steam owner handoff in BLOCKED-vis.md/BLOCKED-upmp.md.
No LANE_DONE or push. Twenty pre-existing SoftFloat deletions remain untouched.

### vis — resume at cc4b93d3, 2026-10-28

Verified required runbooks, prun handoff, GetDeviceCaps ancestor fb037384,
owner heads stm ce3fde3e and upmp a69e6383, and saved Xbox boot-late.log
lines 127/132. No new source handoff exists: native swapchain S_OK remains
followed by the enforced real-client LoadLibrary prerequisite. Existing
BLOCKED-vis.md and BLOCKED-upmp.md describe the required owner input.

No fresh console use, rebuild, install or test result. Menu, five-minute
stability and input-started gameplay with measured FPS remain unproved.
No LANE_DONE or push. Existing twenty SoftFloat deletions remain untouched.

### vis — resume at d82bb008, 2026-10-28

Rechecked required runbooks, prun handoff, owner heads and saved Xbox evidence.
GetDeviceCaps fb037384 is already integrated. stm remains ce3fde3e (only
LANE_NOTES.md changed since b3ef3388); upmp remains a69e6383. Neither supplies
the isolated process/IPC seam or genuine-client implementation. Production
play_runtime.h still enforces the Steam prerequisite. Saved Xbox evidence
shows native D3D11 device and CoreWindow swapchain S_OK, followed by
steam_api64.dll!LoadLibrary at UnityPlayer+0x4f4398.

No fresh console use, build, installation or test result. Menu, five-minute
stability, input-started run and gameplay FPS remain unproved. Resume after
the execution/Steam handoff described in BLOCKED-vis.md and BLOCKED-upmp.md.
No LANE_DONE or push. Twenty pre-existing SoftFloat deletions remain untouched.

### vis — resume audit at f8aaa99a, 2026-10-28

Verified required runbooks, prun handoff and saved real-console evidence.
GetDeviceCaps fb037384 is integrated; stm ce3fde3e and upmp a69e6383
provide no new client/process implementation. Saved play-resume-20261028
boot-late.log records D3D11 device and CoreWindow swapchain S_OK, followed
by the enforced steam_api64.dll LoadLibrary real-client prerequisite.

No fresh console run, build, installation or test result. Further acceptance
requires the execution/Steam owner handoff in BLOCKED-vis.md and
BLOCKED-upmp.md. Menu, five-minute stability and input-started gameplay
with measured FPS remain unproved. No LANE_DONE or push. Twenty existing
SoftFloat deletions remain untouched and unstaged.

### vis — resume verification at e657a337, 2026-10-28

Re-read required runbooks and prun handoff. GetDeviceCaps fb037384 is already
integrated. Owner refs remain stm ce3fde3e and upmp a69e6383; stm changes
since b3ef3388 are notes only. No new client/process implementation exists
to integrate. Saved Xbox play-resume-20261028/boot-late.log:124-132 shows
D3D11 device and native CoreWindow swapchain S_OK, then the real-client
LoadLibrary prerequisite at UnityPlayer+0x4f4398.

No new console use, build, installation or test result. Further acceptance
requires the execution/Steam handoff documented in BLOCKED-vis.md and
BLOCKED-upmp.md. Main menu, five-minute stability, input-started gameplay
and measured FPS remain unproved. No LANE_DONE or push. Twenty pre-existing
SoftFloat deletions remain untouched and unstaged.

### vis — resume verification at 506acff6, 2026-10-28

Read required runbooks and prun handoff. GetDeviceCaps commit fb037384 is
already integrated. Owner heads remain stm ce3fde3e and upmp a69e6383;
stm changes since b3ef3388 are documentation only. No new legitimate-client
or isolated guest process/IPC implementation is available to integrate.
Saved real-console play-resume-20261028/boot-late.log confirms D3D11 device
and native CoreWindow swapchain S_OK, then the enforced steam_api64.dll
LoadLibrary prerequisite at UnityPlayer+0x4f4398.

No fresh console use, build, installation or test result is claimed. Menu,
five-minute stability, input-started run and gameplay FPS remain unproved.
Further acceptance requires the execution/Steam owner handoff documented in
BLOCKED-vis.md and BLOCKED-upmp.md. No LANE_DONE or push. Twenty pre-existing
SoftFloat build-file deletions remain untouched and unstaged.

### vis — resume at fe209f6b, 2026-10-28

Verified required runbooks, prun handoff, integrated GetDeviceCaps ancestor
fb037384, and current owner refs stm ce3fde3e / upmp a69e6383. No new
client/process source handoff exists. Production src/xbox-test/app/host/play_runtime.h
still enforces the real-client prerequisite. Saved Xbox evidence in
runs/vis/play-resume-20261028/boot-late.log records D3D11 device and CoreWindow
swapchain S_OK, then steam_api64.dll!LoadLibrary at UnityPlayer+0x4f4398.

No fresh console access, build, install or test result. Further acceptance
requires the execution/Steam handoff in BLOCKED-vis.md and BLOCKED-upmp.md.
Menu, five-minute stability, input-started run and gameplay FPS remain unproved.
No LANE_DONE or push. Twenty pre-existing SoftFloat deletions remain untouched.

### vis — resume verification at 03ade109, 2026-10-28

Read required runbooks and prun handoff. GetDeviceCaps integration fb037384
remains an ancestor. Owner refs remain stm ce3fde3e and upmp a69e6383;
no new legitimate-client/process source handoff is available. Production
play_runtime.h retains the real-client prerequisite. Saved Xbox evidence
play-resume-20261028/boot-late.log confirms D3D11 device and CoreWindow
swapchain S_OK, followed by the Steam LoadLibrary prerequisite at
UnityPlayer+0x4f4398. These are saved results, not fresh execution.

No source change, console transaction, rebuild, install or new test result.
Further acceptance requires the execution/Steam handoff in BLOCKED-vis.md
and BLOCKED-upmp.md. Menu, five-minute stability, input-started run and
measured gameplay FPS remain unproved. No LANE_DONE or push. Twenty
pre-existing SoftFloat build-file deletions remain untouched and unstaged.

### vis — resume verification at 16cf78f1, 2026-10-28

Confirmed GetDeviceCaps ancestor fb037384, owner heads stm ce3fde3e and
upmp a69e6383, and unchanged production real-client load guard. The stm
delta from b3ef3388 contains notes only. Read required runbooks and prun
handoff. Saved Xbox play-resume-20261028/boot-late.log confirms D3D11 device
and CoreWindow swapchain S_OK, then the Steam prerequisite at
UnityPlayer+0x4f4398. These are saved results, not fresh console execution.

No new source handoff exists to integrate. No console use, rebuild, install
or new test result. Menu, five-minute stability, input-started run and
measured gameplay remain unproved. Resume after the execution/Steam handoff
in BLOCKED-vis.md and BLOCKED-upmp.md. No LANE_DONE or push. Twenty existing
SoftFloat deletions remain untouched and unstaged.

### vis — resume dependency check at 70572c85, 2026-10-28

Read required runbooks, prun handoff and external blocker files. GetDeviceCaps
fb037384 is already integrated. Owner refs remain stm ce3fde3e and upmp
a69e6383; stm changes since b3ef3388 contain notes only. No new client/process
implementation is available to integrate. Confirmed production loadExW still
enforces the real-client prerequisite. Saved Xbox play-resume-20261028
boot-late.log records native D3D11 device and CoreWindow swapchain S_OK, then
steam_api64.dll!LoadLibrary at UnityPlayer+0x4f4398.

These are saved results, not fresh console execution. No build, installation,
console use or test rerun occurred. The external execution/Steam handoff in
/workspace/x64-run/BLOCKED-upmp.md remains necessary. Main menu, five-minute
stability, input-started run and measured gameplay remain unproved. No
LANE_DONE or push. Twenty pre-existing SoftFloat deletions remain untouched.
