# Lane gfxin notes (exported from the local LANE_NOTES.md)

Lines this lane added to the shared LANE_NOTES.md between 494a54020 and 695dd8339.


## gfxin — inventory and production seams, 2026-10-27

Started lane/gfxin at 494a5402 with a clean worktree. Read REBUILD.md,
PLAYABLE.md and the prun/vis handoffs. Initial inventory is
research/gfxin-todo.csv, diffed against this branch's production resolver and
record-window exports. It includes existing system graphics routes to distinguish
working device creation from missing presentation. XInput/DirectInput are dynamic
queries absent from the 628-import CSV and also need explicit routes.

Console evidence inherited from vis establishes D3D11CreateDevice success only.
No visible guest frame or controller acceptance is claimed. Production factories
still receive opaque record HWNDs, and the launcher retains its own CoreWindow
swapchain. The isolated swc/display spikes are reference code, not integrated
Megabonk behavior. Input must reuse the user32 record registry rather than add
a second window/message model. All implementation and tests stay in this worktree.

### gfxin — production factory handoff, 2026-10-27

Added separate `uwph/graphics/route.h` and host regression. Production resolver
now intercepts CreateDXGIFactory/CreateDXGIFactory1/CreateDXGIFactory2 rather
than giving record HWNDs directly to native factory swapchain methods. The
wrapper validates HWNDs through the existing owner DLL, keeps COM ownership,
converts legacy single-sampled discard/sequential chains to two-buffer flip,
and preserves native creation errors. Unsupported fullscreen and multisampled
legacy requests fail explicitly. MakeWindowAssociation records supported flags
and validates handles; no host user32 receives guest record addresses.

Launcher installs a UI dispatcher callback before guest startup. It releases
its backbuffer/swapchain and flushes its context before creating the guest
CoreWindow swapchain. Successful handoff stops launcher Present calls; a failed
creation attempts to restore the launcher chain. Guest rendering is not
established by this code or the build. No console deployment/version consumed.

Off-device regression passes with WINEPREFIX=/tmp/vis-wine: HWND validation,
factory QueryInterface/refcounts, callback dispatch/error propagation, legacy
flip conversion, fullscreen/MSAA refusal and route matching. Signed launcher
build passes (`/tmp/gfxin-build.log`). Tests use a fake native factory and make
no console/GPU claim. Existing tests and smoke thresholds remain unchanged.

Next: wrap swapchain desktop queries/fullscreen semantics, address adapter
GetParent returning an unwrapped native factory, connect WGI/XInput and keyboard,
and run host runtime/smoke gates before a console milestone. No visible Unity
frame, menu, controller acceptance or gameplay claimed. LANE_DONE absent.

### gfxin — swapchain identity and WGI/XInput host milestone

Added IDXGISwapChain1 wrapper preserving guest HWND in GetHwnd/GetDesc and the
wrapped factory in GetParent. Native Present/Present1, buffer access and
ResizeBuffers keep their exact results. Exclusive fullscreen returns
DXGI_ERROR_NOT_CURRENTLY_AVAILABLE; desktop ResizeTarget is explicitly
unsupported. Actual CoreWindow chains remain windowed in the desktop model.
The regression covers successful wrapping/refcounts, HWND/factory identity,
Present flags/status and resize arguments/device-reset propagation.

Added separate uwph/xinput route and tests. UI polls real WGI controllers,
retains four slot identities across list reordering, and publishes buttons,
triggers and thumbsticks without dead zones. Packet numbers change with state.
Disconnect reports ERROR_DEVICE_NOT_CONNECTED, not a successful neutral pad.
WGI vibration receives requested motor magnitudes and propagates disconnection.
XInputEnable neutralizes input/stops feedback and restores it on re-enable.
Capabilities describe the supported WGI gamepad surface; battery information
explicitly returns ERROR_NOT_SUPPORTED until a battery backend exists. Dynamic
XInput DLL loading on Xbox and keystroke exports remain open; no controller
acceptance claim. The static CSV has no XInput rows; dynamic rows are now also
tracked in research/gfxin-todo.csv.

Off-device evidence under /workspace/x64-run/runs/gfxin:
- runtime.log: all thirteen existing Play runtime modes pass unchanged.
- conversion.log: actual SDK WGI data-layout conversion passes buttons, signed
  axis endpoints, trigger ranges and nonfinite-input rejection.
- smoke-offline-retry.log: unchanged smoke-all.sh --offline passes 2/2 using the
  established /tmp/vis-wine prefix. Initial newly selected gfxin-smoke prefix
  failed before dispatch output; preserved failure in smoke-offline.log.
Graphics and XInput unit scripts also pass. Signed input launcher build passes
in /tmp/gfxin-build-input.log. All evidence is off-device. No console version
used, no Xbox gameplay or visible frame claimed. First milestone backup ended
ok=20/fail=0. LANE_DONE remains absent; substantial graphics/input gaps remain.

### gfxin — handoff for vis: production bridge and dynamic controller modules

For vis integration: local commits 9410261e and a7f8edc contain the production
factory/CoreWindow handoff, swapchain identity wrapper and WGI/XInput polling.
Unlike swc_dxgi.h's isolated spike, the production callback uses the actual
uwph-u32c IsWindow registry and dispatches onto the launcher's UI CoreWindow.
The launcher releases its own swapchain before guest creation and stops
Present afterwards. This directly addresses vis's latest windowed diagnostic
handoff; no actual console creation HRESULT has yet been observed for this code.

Further changes add built-in module identities for xinput1_4/xinput1_3/
xinput9_1_0, including LoadLibrary, GetModuleHandle, named GetModuleHandleEx,
GetProcAddress and FreeLibrary routing in the production guest runtime. These
modules expose only implemented exports. Unsupported optional exports return
NULL/ERROR_PROC_NOT_FOUND; asking for a physical module filename explicitly
fails ERROR_NOT_SUPPORTED rather than inventing an on-disk path.

xinput/runtime-test.sh compiles a separate relocatable guest and reuses the
actual Play loader/runner completion assertions. Its first fixture lacked a
base relocation and the mapper correctly rejected it; added a retained actual
relocation, without altering the mapper. Now guest entry returns 42 after
reading an injected A button/negative thumbstick through dynamic imports,
checking disconnected slot errors, module refs/unload, optional lookup errors
and extensionless xinput1_3 loading. This is explicitly an off-device injected
reading, not real controller evidence. xinput-runtime.log preserves the final
pass; runtime-modules.log also passes all 13 existing runtime modes.

Adopted the same windowed launch option already under test in vis after its
console player log established exclusive resolution switching fails. Added
GFXIN factory-QI, handoff and successful Present-submission markers for future
console diagnosis. A submission marker alone does not prove visible pixels.
Signed build and focused regressions are rerunning before the next commit.

### gfxin — resume: HID failure contracts, keyboard feed/names, activation

Resumed at dffc931b with unfinished HID and CoreWindow keyboard edits. Preserved
and completed those changes in separate hid/keyboard route and test files.
Updated research/gfxin-todo.csv against this tree: MapVirtualKeyA already has an
owner DLL alias and is no longer incorrectly listed missing. GetKeyNameTextW
now uses the same fixed US layout as the record backend, with modifier-side
collapse, bounded Unicode output and function/navigation/keypad names.

All fourteen imported HID exports now have production routes. This backend
exposes no HID file endpoints or preparsed allocations: controllers use WGI /
XInput. HidD_GetHidGuid returns the real class GUID; device calls reject unknown
handles without changing outputs. Parser calls reject unknown preparsed data
and preserve Wine's report-length precedence and affected length outputs.
This is explicit no-HID-endpoint behavior, not HID report delivery or a claim
that parser support exists for real descriptors. UWP desktop header partitions
required local ABI-compatible scalar/opaque-pointer declarations; nothing is
forwarded to system HID. Wine differential exposed an unsafe test fixture with
null length pointers; it now supplies valid buffers and compares errors and
length/output preservation. Native Wine code was not changed.

CoreWindow KeyDown/KeyUp now feed the owning user32 record registry, including
left/right modifiers and aggregate state. Deactivation releases held keys.
XInput deactivation mutes visible input and motors while preserving the guest's
separate XInputEnable policy and last vibration request. Hidden launcher input
and synthetic launcher commands stop after the guest owns presentation.
Raw input/keyboard message delivery remains open; this feed establishes state
polling only. No real physical keyboard/controller has been tested here.

Off-device evidence under runs/gfxin: hid-routes.log passes differential
GUID/invalid-handle/preparsed/report contracts; keyboard-feed.log passes event,
modifier and focus-loss cases; keyboard-names.log passes 280 Wine comparisons
plus 512 guarded one-character-buffer cases. The latter deliberately tests
size=1 directly: Wine 10's headless printable-key fallback has an uninitialized
length in NtUserGetKeyNameText and can write outside that buffer. Existing
tests were not changed to accept this Wine bug. Signed build and all thirteen
unchanged Play runtime modes pass before the final activation edit; final
activation build, XInput regression and smoke gates are being recorded next.

Read-only coordination: vis's runs/vis/play-corewindow on verified 0.5.158.5
reports real native CoreWindow swapchain creation hr=0, then user32!PeekMessageA
at UnityPlayer+0x741157. Its player log no longer reports resolution failure.
This is vis's console evidence, not a gfxin deployment or proof of visible
frames. Our committed dffc931b dynamic XInput modules address its logged
XInput1_3/XInput9_1_0 load failures. No console version consumed this resume.
No LANE_DONE: visible guest frames, controller acceptance, adapter-parent escape
paths, RawInput/message delivery and remaining display lifecycle work are open.

### gfxin — keyboard queue handoff and DirectInput refusal

Read vis's committed ad49c93a queue implementation without modifying that
worktree. Extended our separate keyboard Feed to post scalar WM_KEYDOWN/UP and
WM_SYSKEYDOWN/UP through the owner DLL's PostMessageW export when available.
Messages preserve scan code, extended flag, prior/down transition, repeat count,
Alt context and generic modifier virtual keys. Focus/foreground callbacks select
the guest record; no private window registry or second message queue exists.
Deactivation clears state without inventing key-up events. Our current older
record DLL has no PostMessageW export, so the launcher explicitly logs queue=0
until vis integrates the input changes with its queue. No missing export is
silently replaced with a successful no-op.

keyboard/queue-test.sh requires an explicit owner DLL path. It passes against
vis's built uwph-u32c.dll read-only: actual guest record creation, UI-like feed
on another thread, state polling, guest-thread dequeue, repeat/release flags
and focus-loss clearing. Evidence: keyboard-owner-queue.log. keyboard/test.sh
also passes the event/message contract assertions. These are injected off-device
events, not real keyboard/controller input or an Xbox game acceptance result.

Added a separate dinput route plus built-in dinput8 module identity. It validates
DirectInput8Create arguments, interfaces and versions against Wine, then returns
DIERR_UNSUPPORTED with NULL output because this backend exposes controllers only
through WGI/XInput. Aggregation returns CLASS_E_NOAGGREGATION. No fake DirectInput
object, device, descriptor or successful enumeration is supplied. Regression
passes Wine invalid-request comparisons and explicit no-backend checks; the
mapped input guest now also dynamically loads this module and verifies refusal.
Remaining DirectInput optional exports stay absent/ERROR_PROC_NOT_FOUND.

Signed activation build passed and unchanged offline smoke passed 2/2.
All thirteen Play runtime modes passed after the first HID/keyboard routing
changes; final queue/module build and mapped input runtime are running.
No smoke assertion was changed. Console remains with vis; no gfxin deployment
or version reservation. LANE_DONE remains absent.

### gfxin — resumed RawInput error-contract milestone

Preserved unfinished RawInput files and runtime edits at resume. Production now
supersedes the record DLL's successful unknown-handle size query with typed
ERROR_INVALID_HANDLE. Device-info errors preserve pointer/command precedence;
empty buffered input reports zero bytes. This backend issues no raw handles:
WGI gamepads use XInput and keyboard events use the owner message queue.
No HID endpoint or WM_INPUT delivery is claimed.

Off-device rawinput/test.sh passes 74 Wine differential cases on /tmp/vis-wine,
including output preservation and header-size precedence. The actual mapped
input fixture returns 42 after its imported GetRawInputData verifies the same
error and unchanged size. The runner also prints an unrelated search-fixture
self-test failure because this focused fixture does not copy that DLL; its
mapped entry completion succeeds. No gate or assertion was weakened.

Read-only vis handoff now reports visible Loading... on console 0.5.158.6;
0.5.158.7 exception diagnostics are pending there. That is vis's evidence,
not a gfxin console deployment. No console version consumed here. Remaining
work includes adapter-parent factory escapes, display/pointer lifecycle and
real controller/menu/gameplay acceptance. LANE_DONE remains absent.

Final checks for 6f4ec41: signed launcher build passes (build-rawinput.log).
Unchanged smoke-all.sh --offline passes 2/2 (smoke-rawinput.log); console
sections are skipped, not claimed. Graphics, XInput and keyboard focused
regressions pass (graphics-resume.log, xinput-resume.log, keyboard-resume.log).
RawInput differential evidence is rawinput-resume.log. All thirteen unchanged
Play runtime modes pass with PRUN_WINEPREFIX=/tmp/vis-wine
(runtime-rawinput-retry.log). The first full-runtime invocation selected the
script's default /tmp/prun-wine despite WINEPREFIX and failed with exit 53;
that failure remains in runtime-rawinput.log. No tests were edited for retry.

Integration handoff: 6f4ec41 changes only the separate rawinput route/tests,
two focused mapped-input fixture files, four resolver lines and this inventory/
notes. It can follow the prior graphics/input commits without importing an
older user32 owner implementation. Console remains with vis; this checkpoint
makes no additional on-device claim. Full graphics/input acceptance remains
open, so LANE_DONE must not be created.

### gfxin — resumed inventory and optional fullscreen outputs

Resumed at 90538fe9 with a clean worktree. Re-read rebuild/playability rules and
read-only vis/prun handoffs. Vis now records Loading... on Xbox 0.5.158.8 and a
legitimate Steam-client prerequisite; that is vis evidence, not this lane's
console test. No console access or version reservation occurred here.

Compared the scoped import CSV with production routes and owning user32 exports.
Expanded research/gfxin-todo.csv from 51 to 66 entries to include omitted display,
monitor, coordinate and keyboard-layout queries. These remain record-backend or
explicit unsupported contracts; native CoreWindow lifecycle acceptance is open.
Updated stale graphics/XInput evidence without claiming physical input acceptance.

Found and reproduced an incorrect DXGI optional-output rejection: the existing
wrapper rejected GetFullscreenState(NULL,NULL). Wine's dxgi/swapchain.c:437
accepts independently optional outputs. New host regressions fail before the fix
and pass afterwards for neither, either, and both outputs. Windowed state and
NULL output identity remain truthful for CoreWindow. Fullscreen entry still fails
explicitly; no exclusive display mode is fabricated. Broader validation follows.

### gfxin — enumerated adapter factory ownership

Added graphics/adapter.h and a separate adapter host regression. EnumAdapters,
EnumAdapters1 and successful CreateSoftwareAdapter now return adapters retaining
the wrapped factory. GetParent cannot escape directly from those enumerated
adapters to a native factory. Descriptor/LUID queries, output enumeration,
interface checks and private data keep native results. Adapter1 is the supported
interface ceiling; unsupported interfaces return E_NOINTERFACE. Native D3D11
creation unwraps only registry-owned adapters, preserving native parameters,
outputs and HRESULTs. External/native adapter arguments remain unchanged.
The translation is typed; no guest record is forwarded to a system window API.

Regression covers both enumerators, software creation, absent indexes, null
outputs, parent identity, descriptor/LUID preservation, reference balances,
adapter lifetime beyond guest factory release, native device failure propagation,
and registry removal. This uses fake native COM objects off-device, not a GPU.
The original graphics regression remains intact and passes alongside it.

Also replaced invented zero refresh/scaling values in GetFullscreenDesc with
the actual native swapchain descriptor. Native query failures leave the caller's
output untouched and propagate. Regression covers rational refresh rate,
scanline/scaling values and device-removal failure without output mutation.

Validation so far: graphics-adapters.log passes both focused executables;
smoke-adapters.log passes unchanged offline smoke 2/2; runtime-adapters.log
passes all thirteen existing runtime modes; build-adapters.log signs successfully.
Those broad gates preceded the final descriptor/allocation-lifetime changes;
final focused rerun passed, and final build/gates follow. All are off-device.
No console deployment, physical controller acceptance, main menu or gameplay
claim. Native adapters returned by device GetAdapter and native output GetParent
still require ownership work. Pointer/display lifecycle remains open. Do not
create LANE_DONE until the whole lane's requested acceptance is proven.

Adapter boundary audit also found Megabonk's d3d12 ordinal #101 is
D3D12CreateDevice (Wine d3d12.spec and wine-coverage import audit agree).
Added the corresponding typed native adapter translation for named and ordinal
resolution. The single resolver addition passes the existing ordinal to the
separate graphics route. #102 remains native D3D12GetDebugInterface. Regression
checks ordinal/name selection, native and wrapped adapters, optional device
output, and exact native failure propagation. No D3D12 device is fabricated.
Final pre-ordinal signed build and unchanged smoke passed; final ordinal build
and runtime gates are being run. Backup remains in progress across shared lanes.

Final cb46cdc8 validation passes: build-adapters-d3d12.log signs the package;
runtime-adapters-d3d12.log passes all thirteen unchanged runtime modes;
smoke-adapters-d3d12.log reports 2 passed, 0 failed with console sections skipped;
graphics-adapters-d3d12.log passes both focused graphics executables.
No test threshold was weakened. Worktree is clean after the implementation commits.

The required backup completed with 20 verified bundles including wt_gfxin,
but fail=1 because copying lane aud's uncommitted archive to the shared store
returned Resource temporarily unavailable. This is not a missing gfxin bundle.
Retrying the standard backup after committing this validation note.

Read-only vis handoff: integrate 87080048, 63da29d6, cb46cdc8 selectively;
cb46cdc8 includes one graphics resolver ordinal argument in mlp_probe.h.
Keep vis's newer runtime/C++ and launcher integration. The adapter additions do
not resolve the legitimate Steam prerequisite currently blocking menu startup.
Next gfxin work: device-returned adapter/output-parent ownership, CoreWindow
pointer/display/recreation lifecycle, then controller/menu/run acceptance with
vis once real-client startup permits it. LANE_DONE remains absent.

Latest read-only vis notes supersede the pending XInput integration status:
console 0.5.158.9 again creates native D3D11/CoreWindow chains with S_OK and
shows Loading.... Dynamic XInput module-load failures are absent after owner
integration. Its controller count is pads=0, so this proves no physical input.
Vis's full console smoke passes 9/9. Its exact immediate stop remains the
legitimate Steam prerequisite; see /workspace/x64-run/BLOCKED-vis.md.
Updated XInput inventory evidence accordingly. This lane did not use the console.

### gfxin — output enumeration parent ownership (2026-10-28)

Resumed at f9dfc885 with a clean worktree. Read the required rebuild/playability
references and current vis/prun handoffs. Rechecked all 628 source CSV imports
against the scoped inventory: all graphics/HID/input DLL imports are listed;
research/gfxin-todo.csv retains 66 scoped static/dynamic entries. Its partial
statuses remain intentional. Current vis evidence still reaches Loading... and
stops at the legitimate Steam prerequisite; no new console use occurred here.

Added a separate output wrapper and host regression. Adapter EnumOutputs now
retains its bridge adapter, so output GetParent no longer exposes a native
adapter/factory path. Native descriptors, mode queries, HRESULTs and resource
operations remain native. Unsupported output interfaces return E_NOINTERFACE.
Native swapchain creation receives the underlying output for restrictions in
HWND, CoreWindow and composition paths; bridge pointers do not enter DXGI.

The regression verifies output/adapter lifetime after guest adapter release,
parent/interface identity, missing-index and null-output errors, native mode
count/error preservation, restriction translation and registry cleanup. Running
it with the previous adapter implementation fails at the wrapped-output identity
assertion (outputs-before.log); no production assertion was weakened. Final
focused results follow in graphics-outputs.log. Signed UWP package build passes
(build-outputs.log). Unchanged offline smoke passes 2/2 (smoke-outputs.log);
console sections are skipped. These are off-device tests with fake native COM
objects, not GPU/display/controller acceptance.

Remaining: device-returned adapters, swapchain-returned outputs, pointer/display
lifecycle and actual controller/menu/gameplay acceptance. Console stays with vis;
no version consumed. LANE_DONE remains absent because full area acceptance is
not proven. Nothing pushed or posted. Required backup follows this milestone.

### gfxin — swapchain output parent ownership

Closed both remaining output-return paths in the bridge swapchain:
GetContainingOutput and GetRestrictToOutput now rebuild the output/adapter/factory
parent chain. Native failures and successful absent restrictions remain native
results. Native parent lookup/wrapping failures release the acquired output and
return the failure with no fabricated parent. Device-returned adapters remain
open; this does not claim all native factory escape paths are resolved.

Extended the separate output fixture with both swapchain queries, native errors,
absent output, parent lookup failure, exact factory identity and reference cleanup.
The fixture fails against the previous swapchain at output identity
(chain-outputs-before.log) and passes after the fix
(graphics-chain-outputs.log). Signed build, unchanged runtime modes and offline
smoke are running. No console access, new version or on-device claim.

Final swapchain-output validation: signed package build passes
(build-chain-outputs.log), all thirteen unchanged runtime modes pass
(runtime-chain-outputs.log), unchanged offline smoke passes 2/2
(smoke-chain-outputs.log), and all three final graphics regressions pass
(graphics-chain-outputs-final.log). No test threshold changed. First required
backup has verified wt_gfxin with the enumeration milestone and continues across
shared lanes. A new required backup will run after this second local commit.
The remaining lane scope and on-console acceptance are not complete; no
LANE_DONE marker is created.

### gfxin — swapchain recreation checkpoint (validation blocked, 2026-10-28)

Resumed at 0ec3e6ad. Existing softfloat build-file deletions were present on
arrival and remain untouched. Read the required references and current vis/prun
handoffs. Rechecked 628 CSV imports against the 66-entry scoped inventory; no
static graphics/HID/DirectInput import is omitted. Latest vis console evidence
still shows Loading... and stops at the legitimate Steam prerequisite. This
session performed no console access and consumed no version.

Found a presentation lifecycle defect: m_guestPresentation was set on native
creation and never cleared when the guest released its bridge swapchain. Added
an optional per-chain release callback, captured at creation and invoked after
dropping the native chain reference. The launcher uses an atomic presentation
flag because guest Release may execute on a worker. A subsequent UI-thread
creation can now reach native DXGI validation; outstanding native buffer
references can still cause native failure. No successful recreation is claimed.
Allocation failure releases the native chain and presentation slot. The release
hook copy is allocated before native creation and reports E_OUTOFMEMORY on
failure. Extended the focused lifetime regression to check no notification on
an intermediate release, exactly one notification after native release, and
retention of the original callback across route reconfiguration.

This is an UNVALIDATED checkpoint. Current environment lacks MinGW, Wine and
LLVM tools that earlier sessions used. graphics/test.sh exits 127 for missing
x86_64-w64-mingw32-g++; signed build stops at missing llvm-dlltool. Unchanged
offline smoke reports 0 passed / 2 failed: dispatch lacks MinGW and import audit
lacks llvm-readobj. These are environment failures, not green gates. HTTPS apt
index recovery failed with SSL unexpected EOF; a temporary HTTP source also
failed with HTTP 500/unsigned-index rejection. Signature verification was never
disabled. No production/test assertion was weakened. Logs are retained under
/workspace/x64-run/runs/gfxin/resume-lifecycle/. Diff whitespace check passes.

Next: restore the toolchain, run the focused graphics regressions and signed
build, then all unchanged runtime modes and offline smoke before integrating
this checkpoint into vis. Device-returned adapters, pointer/display lifecycle
and real controller/menu/gameplay acceptance remain open. Do not create
LANE_DONE. No remote push or external post occurred. Required backup follows.

Checkpoint commit: a7e9bb31 (local only). Standard backup.sh is running across
shared worktrees; its log is resume-lifecycle/backup.log. Independently created
and git-verified a recovery bundle containing a7e9bb31, copied its 1,958,337
bytes to wt_gfxin-a7e9bb31-recovery.bundle.part.00 in the configured persistent
backup store, and verified SHA-256 after the copy. This verifies this lane's
checkpoint recovery, not completion of the all-worktree backup.

### gfxin — restored-toolchain validation (2026-10-28)

Resumed at 0a8088e5. Read RTK, required runbooks and read-only vis/prun notes.
The twenty existing SoftFloat build-file deletions remain untouched and unstaged.
MinGW, Wine and LLVM are now available; no package installation or signature
policy change was needed in this session. Rechecked the 628-import CSV: all
21 graphics/HID/DirectInput DLL imports occur in the 66-entry scoped inventory.
Updated the factory evidence to distinguish proven release notification from
unproven native recreation. No production or test source changed here.

The a7e9bb31 checkpoint now passes all three graphics host regressions,
including final-reference release notification and callback retention across
configuration changes. The signed launcher build succeeds. Unchanged offline
smoke passes 2/2, with console sections explicitly skipped. Evidence is under
/workspace/x64-run/runs/gfxin/resume-validation/{graphics,build,smoke}.log.
A new Wine prefix was created after the initial invocation refused to create
one beneath /tmp; its headless initialization reports missing 32-bit rundll32,
but all three x64 graphics test executables complete successfully.

Current vis evidence remains native CoreWindow swapchain success and Loading...,
then the enforced legitimate Steam-client prerequisite. No console use,
version reservation, physical input, menu or gameplay claim occurred here.
Device-returned adapter ownership, pointer/display lifecycle and real controller
acceptance remain open. LANE_DONE remains absent. Runtime gates and required
backup follow this checkpoint; final results are recorded below.

Final restored-toolchain gates: all thirteen unchanged Play runtime modes pass
(runtime.log, script exit 0). Deliberate missing-import, exception, timeout and
Steam refusal fixtures retain their expected failure outcomes. This validates
the existing release checkpoint off-device; native recreation still needs its
own console milestone. No test or production assertion was weakened.

### gfxin — CoreWindow character delivery (2026-10-28)

Resumed at cfc9da2d. Read RTK, the required rebuild/playability references and
read-only vis/prun handoffs. Twenty pre-existing SoftFloat build-file deletions
remain untouched. Rechecked the 628-import CSV against research/gfxin-todo.csv:
all 21 static graphics/HID/DirectInput DLL imports remain represented in the
66-entry scoped inventory. Native passthrough and ordinal routes were inspected
separately from named routes. Existing partial statuses remain intentional.

Found that CoreWindow KeyDown/KeyUp were wired but CharacterReceived was absent.
Added a typed callback and keyboard Feed character path. It posts WM_CHAR or
WM_SYSCHAR to the owner focus/foreground queue with native scan, extended,
Alt-context, previous-state, transition and bounded repeat fields. CoreWindow
KeyCode supplies UTF-16 code units; surrogate halves remain separate events in
order, without virtual-key or Unicode scalar conversion. Source contract:
https://learn.microsoft.com/en-us/uwp/api/windows.ui.core.characterreceivedeventargs.keycode
The handler marks only successfully queued events handled. No target, missing
queue, invalid code unit or owner queue refusal returns false. No keyboard
state is invented. IME composition is not provided by this event bridge.

Focused keyboard regressions pass, including control characters, surrogate
order, message bits, absent targets, queue failure and unchanged virtual-key
state. Extended the existing owner queue integration with actual cross-thread
ASCII WM_CHAR delivery. It passes against vis's current core.cpp/core.def built
read-only into this lane's external run directory. This branch's older packaged
record DLL still lacks PostMessageW; the existing diagnostic exposes queue=0
until integration with the owner queue. Vis's queue currently rejects non-ASCII
character messages with ERROR_NOT_SUPPORTED; the new callback preserves that
refusal. The isolated Feed fixture proves UTF-16 preservation at this boundary,
not end-to-end Unicode support in the owner DLL.

Evidence: /workspace/x64-run/runs/gfxin/text-input/{keyboard,queue,owner-build}.log.
Signed launcher build and unchanged offline smoke are running. No console use,
version consumption, physical input or menu/gameplay acceptance is claimed.
Current vis evidence remains Loading... after native CoreWindow swapchain S_OK,
then the legitimate Steam-client prerequisite. Device-returned adapter ownership,
pointer/display lifecycle and actual controller acceptance remain open.
LANE_DONE remains absent. Nothing pushed or posted; required backup follows
this implementation milestone after final gates.

Final focused gates: keyboard.log and queue.log pass. build.log completes the
signed package successfully. Unchanged smoke.log reports 2 passed, 0 failed,
with console checks explicitly skipped. No test threshold changed. Runtime
regression and standard backup follow this local checkpoint.

Implementation checkpoint: ed96a288 (local only). Final unchanged Play runtime
regression exits 0 with all thirteen modes retaining expected outcomes,
including intentional missing-import, exception, timeout and Steam refusal
cases; evidence is text-input/runtime.log. Standard backup.sh is running across
shared worktrees in text-input/backup.log. Its completed result must be checked
before claiming the backup is complete. Integration handoff for vis: apply this
checkpoint's six keyboard/main/inventory files plus notes to its newer owner
queue; retain the enforced genuine Steam prerequisite. Future owner
TranslateMessage support must coordinate with native CharacterReceived delivery
to avoid translating each physical key twice.

The standard backup has now SHA-256-verified wt_gfxin-20261028-075306.bundle
in the persistent store. Its lane/gfxin head is d2d3cd36c51a4b3b79090996e73fd0394d2028a9,
which contains the implementation and final validation notes. Shared-worktree
backup continues; no all-worktree completion claim is made. This subsequent
backup-status note is not part of that bundle. No LANE_DONE marker exists.

### gfxin — preserve guest swapchain buffer validation (2026-10-28)

Resumed at 938f6fcf. Read RTK, required rebuild/playability references and
read-only vis/prun notes. Existing twenty SoftFloat build-file deletions remain
untouched and unstaged. Reconciled the 628-import source CSV with the 66-entry
scoped inventory: all 21 static graphics/HID/DirectInput imports are represented.
Updated the factory inventory evidence before the implementation milestone.

Found that legacy CreateSwapChain raised BufferCount to two before validating
the guest descriptor. This masked invalid zero counts and one-buffer flip
requests. Validate original buffer bounds first: blit requires 1..16 and flip
requires 2..16, matching Wine 10.0 dxgi_validate_swapchain_desc in
/workspace/ref/wine-10.0-src/wine-10.0/dlls/dxgi/swapchain.c. Unsupported
counts return DXGI_ERROR_INVALID_CALL with null output and never reach the
native creation callback. Valid legacy one-buffer requests still convert to
two-buffer flip; native creation failures remain unchanged.

New host regression fails against the previous route (before.log, line 68).
It checks zero, 17 and UINT_MAX for all four swap effects, one-buffer flip
rejection, and valid lower/upper boundaries with native error propagation.
All three graphics fixtures pass in graphics-isolated.log. The first parallel
run exited zero and printed fixture passes but also a Wine debugger page-fault
diagnostic; the isolated rerun is the clean focused evidence, not that run.
Signed package build passes; unchanged offline smoke passes 2/2 with console
checks explicitly skipped. Logs: /workspace/x64-run/runs/gfxin/legacy-validation/.
Runtime gates and required backup follow this local implementation checkpoint.

No console use, version consumption, push or external post. Vis still records
native CoreWindow swapchain S_OK then the legitimate Steam prerequisite;
no new display/controller/menu/gameplay result is claimed. Device-returned
adapter ownership, pointer/display lifecycle and actual controller acceptance
remain open. LANE_DONE remains absent because the whole area is incomplete.

Final legacy-validation gates: signed build exits 0; all thirteen unchanged
Play runtime modes pass (script exit 0), retaining intentional missing-import,
exception, timeout and legitimate Steam refusal outcomes. Offline smoke exits 0
with 2 passed and 0 failed. Diff whitespace check passes. Implementation commit
is 1e208c30, local only. Standard backup.sh is running in backup.log; completion
is not yet claimed. No test threshold or Steam guard was weakened.

Recovery backup: created and git-verified /tmp/gfxin-10275498.bundle, copied it
to the persistent x64-backup store as wt_gfxin-10275498-recovery.bundle, and
verified matching SHA-256 a2684675791f8ac81b592151c0681687846290bfb032a9efcfce37a3c3a62dd5.
It contains implementation 1e208c30 and gate note 10275498. Standard all-worktree
backup continues separately; its overall completion remains unclaimed.
This subsequent status note is not contained in the recovery bundle.

### gfxin — ResizeTarget argument validation (2026-10-28)

Resumed at 5daea092. Read RTK, required rebuild/playability references and
read-only vis/prun handoffs. Twenty pre-existing SoftFloat deletions remain
untouched and unstaged. Reconciled all 628 CSV imports: the 66-entry scoped
inventory covers all 21 static graphics/HID/DirectInput imports. Updated factory
evidence in research/gfxin-todo.csv before this implementation milestone.

Corrected SwapChain::ResizeTarget to reject a null mode descriptor with
DXGI_ERROR_INVALID_CALL before refusing unsupported CoreWindow display mode
changes. Wine 10.0 dlls/dxgi/swapchain.c:151-159 uses this argument error.
A non-null request continues to return DXGI_ERROR_UNSUPPORTED; no desktop mode
change is forwarded or reported successful. Added regression coverage for both
paths and preservation of the caller's mode descriptor. The new assertion fails
against the previous route at test.cpp:122 (before.log). All three graphics
fixtures pass after the change. Unchanged offline smoke passes 2/2, with console
checks explicitly skipped. Evidence: /workspace/x64-run/runs/gfxin/resize-target/.
Signed build final result follows. No test threshold was weakened.

No console use, version consumed, push or external post. Latest saved vis
console evidence remains CoreWindow swapchain S_OK followed by the legitimate
Steam-client prerequisite. Device-returned adapter ownership, pointer/display
lifecycle and real controller/menu/gameplay acceptance remain open. LANE_DONE
remains absent because the whole requested area is incomplete.

Final ResizeTarget gates: signed package build succeeds; all thirteen unchanged
Play runtime modes pass with expected deliberate failure outcomes; offline smoke
passes 2/2; all three graphics fixtures pass. Diff whitespace check passes.
Implementation commit: 27515c76 (local only). Standard backup.sh is running;
its log is resize-target/backup.log. Overall backup completion is not yet
claimed. No fresh console or whole-area completion claim is made.

### gfxin — validate descriptors before CoreWindow handoff (2026-10-28)

Resumed at 4557fae1. Read RTK, required runbooks and read-only vis/prun
handoffs. The twenty pre-existing SoftFloat deletions remain untouched.
Reconciled the 628-import CSV against research/gfxin-todo.csv: all 21 static
graphics/HID/DirectInput DLL imports remain represented in 66 scoped entries.
Updated that inventory before implementation.

CreateSwapChainForHwnd previously dispatched structurally invalid descriptors
to the UI hook, which releases launcher buffers before asking native DXGI to
validate them. Added pre-dispatch effect/count/flip-sample validation, matching
Wine 10.0 dlls/dxgi/swapchain.c:63-100. Shared the check with legacy creation,
which also now rejects unknown swap effects before dispatch. Native DXGI still
owns format, usage and device capability errors. Unsupported behavior is not
reported successful. No opaque window forwarding was introduced.

New host regression fails against the previous route at test.cpp:94 (before.log).
It covers unknown effects, zero/overflow counts, one-buffer flip and invalid
flip multisampling without calling the UI hook, plus valid count boundaries
and unchanged native device-removal errors. Existing restriction translation
fixture now supplies a valid descriptor; its assertions remain unchanged.
Evidence: /workspace/x64-run/runs/gfxin/predispatch-validation/.
Final graphics/build/runtime results follow. Unchanged offline smoke passes
2/2 with console checks explicitly skipped. No console use or version consumed.

Saved vis evidence still shows native CoreWindow swapchain S_OK followed by
the enforced legitimate Steam prerequisite. Device-returned adapter ownership,
pointer/display lifecycle, native recreation and physical controller/menu/
gameplay acceptance remain open. No LANE_DONE, push or external post.

Final pre-dispatch gates: all three graphics fixtures pass in
 graphics-corrected.log; signed package build exits 0; unchanged Play runtime
suite exits 0 with deliberate failure outcomes retained; offline smoke passes
2/2. git diff --check passes. The initial post-change graphics run correctly
exposed the invalid descriptor in the restriction fixture; graphics-corrected.log
is the final complete passing evidence. No assertion or threshold was weakened.
Required standard backup follows this local implementation checkpoint.

Implementation checkpoint: 0c6cb9a9 (local only). Standard backup.sh is running
across shared worktrees; overall completion is not claimed. Independently
created and git-verified wt_gfxin-0c6cb9a9-recovery.bundle in the persistent
x64-backup store. Source and stored copy both have SHA-256
 d9c3a8244904ecefcc26549d252abb1592dea9eff0b0ac7c26e1570ef71ba597.
This incremental bundle requires existing checkpoint 4557fae1. It contains the
implementation and gate notes above; this later backup-status note is excluded.
The whole area remains incomplete; LANE_DONE remains absent.

### gfxin — reject invalid flip formats before UI handoff (2026-10-28)

Resumed at 91ea7add. Read RTK, required rebuild/playability references and
read-only vis/prun handoffs. Existing twenty SoftFloat build-file deletions
remain untouched and unstaged. Reconciled the 628-import source CSV against
research/gfxin-todo.csv before editing routes: all 21 static graphics/HID/
DirectInput imports remain represented in the 66-entry scoped inventory.

Completed the missing format portion of pre-dispatch flip validation. Wine
10.0 dlls/dxgi/swapchain.c:48-100 accepts UNKNOWN or the four explicit flip
formats (RGBA16 float, RGB10A2 UNORM, RGBA8 UNORM and BGRA8 UNORM). Invalid
formats previously reached the UI hook, which drops launcher backbuffers
before native validation. Both legacy and HWND factory paths now reject them
with DXGI_ERROR_INVALID_CALL and null output before dispatch. Native DXGI still
owns capability errors and UNKNOWN resolution. No rendering success is invented.

New regression failed against the previous route at test.cpp:143 (before.log).
It covers both flip effects, sRGB, typeless, compressed, depth and invalid enum
formats through both factory paths; accepted formats preserve native device
removal errors through the HWND path. All three graphics fixtures pass after
the fix. Unchanged offline smoke passes 2/2 with console checks explicitly
skipped. Logs: /workspace/x64-run/runs/gfxin/flip-formats/.
Signed package build and unchanged Play runtime gates are still running.

No console transaction or version consumed. Current saved vis evidence remains
native CoreWindow swapchain S_OK followed by the legitimate Steam-client
prerequisite at UnityPlayer+0x4f4398. Menu, physical controller acceptance,
five-minute stability and gameplay FPS remain unproved. Device-returned adapter
ownership and pointer/display lifecycle also remain open. This is a focused
implementation checkpoint, not whole-area completion; LANE_DONE remains absent.
Nothing pushed or posted. Required backup follows final local gates.

Final flip-format gates: signed launcher package build exits 0; all thirteen
unchanged Play runtime modes pass (script exit 0), including deliberate import,
exception, timeout and genuine Steam prerequisite failures. All three graphics
fixtures pass; unchanged offline smoke passes 2/2; git diff --check passes.
Implementation checkpoint: d809592 (local only). Standard backup.sh is running
across shared worktrees in flip-formats/backup.log; overall completion is not
yet claimed. No tests or thresholds were weakened.

Recovery backup verified: wt_gfxin-flip-formats-recovery.bundle in persistent
x64-backup contains implementation d8095924 and gate note 32652e57, requiring
existing checkpoint 91ea7add. git bundle verify passes; source and stored copy
share SHA-256 c92674d7ffcf84e3bc09a231e20e2a5066b737c3258323a0f24b2dc23bdb9410.
Standard shared-worktree backup continues; its overall completion remains
unclaimed. This subsequent backup-status note is excluded from that bundle.
LANE_DONE remains absent because graphics/input acceptance is incomplete.

### gfxin — convert HWND blit requests for CoreWindow (2026-10-28)

Resumed at 89d8caa1. Read RTK, rebuild/playability references and the read-only
vis/prun handoffs. Reconciled the 628-import CSV before implementation: all 21
static graphics/HID/DirectInput entries remain represented in the 66-entry
research/gfxin-todo.csv inventory. Twenty existing SoftFloat deletions remain
untouched and unstaged.

CreateSwapChainForHwnd accepted desktop blit descriptors but passed them directly
to the UI callback, which exclusively calls CreateSwapChainForCoreWindow.
Unlike legacy CreateSwapChain, that path never converted blit presentation to
flip. Apply the existing conversion policy at the HWND boundary too: DISCARD
and SEQUENTIAL become FLIP_SEQUENTIAL, with at least two buffers. Preserve all
other fields and the caller's descriptor; native creation still owns device
capability errors. Validate the converted flip descriptor before UI dispatch,
so unsupported MSAA resolve and incompatible flip formats never relinquish the
launcher buffers. This adds no resolve or format-reinterpretation layer.

The new host regression fails on the previous route at hwnd-flip-test.cpp:33.
It covers both blit effects and both flip effects, buffer boundaries, full
converted descriptor equality, unchanged caller descriptors, preserved native
device-removal errors and pre-dispatch multisample/format refusals. Existing
boundary assertions now require the converted count/effect and retain the
native error, dispatch-count and null-output assertions. No test was weakened.
All four graphics fixtures pass; unchanged offline smoke passes 2/2, explicitly
skipping console checks. Signed package build and runtime gates follow.
Evidence: /workspace/x64-run/runs/gfxin/hwnd-flip/.

No console use or version consumption. Current read-only vis handoff still
records native CoreWindow swapchain S_OK followed by the legitimate Steam
client prerequisite at UnityPlayer+0x4f4398. Further on-console menu/controller/
gameplay acceptance needs that execution/Steam handoff. Device-returned adapter
parent wrapping, pointer/display lifecycle, physical input and sustained gameplay
remain incomplete. LANE_DONE is absent. Nothing pushed or posted.

Final HWND conversion gates: signed launcher package build exits 0; all thirteen
unchanged Play runtime modes pass (script exit 0), including deliberate failure
and legitimate Steam prerequisite outcomes. Four graphics fixtures pass;
offline smoke passes 2/2; git diff --check passes. Implementation checkpoint:
1877bdc5 (local only). Standard backup.sh is running in hwnd-flip/backup.log;
its overall completion is not yet claimed. No whole-area completion claim or
LANE_DONE marker is made.

Recovery backup verified: wt_gfxin-hwnd-flip-recovery.bundle in persistent
x64-backup contains implementation 1877bdc5 and final gate note af99abaa,
requiring existing checkpoint 89d8caa1. git bundle verify passes; source and
stored copy share SHA-256
2e3530f4949e8d598e58c2a9cac21753da3c36e1724a5d084e1a0a25e2281b5d.
Standard shared-worktree backup continues separately; overall completion is
not claimed. This later status note is excluded from that recovery bundle.

### gfxin — CoreWindow pointer feed (2026-10-28)

Resumed at 0c054f6b. Read RTK, required runbooks and read-only vis/prun
handoffs. Reconciled the 628-import CSV before implementation: all 21 static
graphics/HID/DirectInput imports remain covered by 66 scoped inventory entries.
The twenty pre-existing SoftFloat deletions remain untouched and unstaged.

Added a separate uwph/pointer/feed.h and CoreWindow mouse move/press/release
integration. Coordinates scale from CoreWindow DIPs into the owning guest client
rectangle, then use owner ClientToScreen, SetCursorPos, GetCursorPos and
ScreenToClient exports. Reading back the cursor preserves owner clipping.
Five mouse buttons update the owning key registry and produce scalar mouse
messages where the owner exposes PostMessageW. Modifier flags use owner
GetKeyState. Activation/capture loss clears held button state. No opaque guest
HWND reaches system USER. Queue refusals leave CoreWindow events unhandled;
this tree's older owner DLL lacks PostMessageW, so it gets cursor/button state
but cannot receive queued messages until vis integrates its newer owner DLL.

Host regression passes for DIP scaling, offset/negative coordinates, clipping,
buttons, modifiers, malformed coordinates, queue refusal and deactivation.
Read-only integration passes against the saved vis owner DLL at
runs/gfxin/text-input/uwph-u32c.dll: injected cross-thread movement/buttons
reach its actual queue, cursor registry and GetAsyncKeyState. This is off-device
Wine evidence, not physical input or console proof. Existing keyboard tests
pass; unchanged offline smoke passes 2/2 with console gates explicitly skipped.
The first package compile exposed unavailable desktop API declarations in
UWP; replaced decltype references with explicit typed callback declarations.
Final package/runtime validation follows. Evidence: runs/gfxin/pointer-feed/.

No console use, version consumption, push or external post. Saved vis evidence
still reaches D3D11/CoreWindow swapchain S_OK, then the legitimate Steam
prerequisite. Menu/controller/gameplay acceptance remains blocked there.
Wheel, guest capture targeting, device-returned adapter parent wrapping and
actual presentation/input lifecycle remain open. LANE_DONE remains absent.

Final pointer-feed gates: final signed package build exits 0 with the complete
modifier-state integration; unchanged Play runtime suite exits 0, retaining
intentional import, exception, timeout and legitimate Steam prerequisite
failures. Pointer host regression, actual saved owner-DLL queue integration
and existing keyboard tests pass. Unchanged offline smoke passes 2/2;
git diff --check passes. Implementation facc4fa3 and contract documentation
8884e6e9 are local only. No tests or thresholds were weakened.

Standard backup.sh was invoked after the implementation milestone and remains
in progress across shared worktrees; overall completion is not claimed.
Independently git-verified and copied wt_gfxin-pointer-feed-recovery.bundle
into persistent x64-backup. Both source and stored copy have SHA-256
4fc8552a38f4644d55cb125598044c040354678a69e3affa9d85b5c9d1a0e5e5.
It contains facc4fa3 and 8884e6e9, requiring checkpoint 0c054f6b.
This later gate-status note is excluded. Whole-area acceptance is incomplete;
no LANE_DONE marker has been created.
