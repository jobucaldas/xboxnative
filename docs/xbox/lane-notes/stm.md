# Lane stm notes (exported from the local LANE_NOTES.md)

Lines this lane added to the shared LANE_NOTES.md between 75b719bc8 and ce3fde3ec.

### stm — resumed dependency verification, 2026-10-28

Confirmed the current lane/stm runtime at xbox-test/uwph/host/entry_kernel.h:19–20
still rejects CreateProcessW with ERROR_ACCESS_DENIED. The execution owner's
latest handoff supplies a Unity guest window record, not isolated child processes.
The existing external handoff in /workspace/x64-run/BLOCKED-upmp.md remains exact;
research steps 1–2, installed-binary inventory and host login-surface evidence
already exist. No repeated host experiment or console transaction was warranted.

The required next input is the execution owner's isolated parent/child console
seam and console results for the three existing process/IPC fixtures. Genuine
AMD64 Steam/CEF on Xbox, normal service-free operation, fresh genuine sign-in,
owned library/download and original API callbacks follow that input. The accepted
5 GB allowance adds no research or measurement gate. No successful Steam init,
authentication or console Steam execution is claimed. No LANE_DONE was created.
Unrelated working-tree deletions and blocker rename remain untouched.

### stm — resume dependency check, 2026-10-28

Re-read REBUILD.md, PLAYABLE.md, the latest directive reconciliation, installed
binary evidence summary, current process route and prun final handoff. The
required genuine client remains on Xbox inside launcher -> Wine -> uwph -> UWP
Game mode. The accepted 5 GB allowance requires no research or measurement gate.
The installed snapshot already verifies the AMD64 client/CEF and i386 service;
helper presence alone does not establish a runtime requirement.

The concrete missing external input remains an execution-owner implementation
of isolated guest processes and a callable Xbox parent/child seam. The current
entry_kernel.h CreateProcessW implementation still returns ERROR_ACCESS_DENIED.
Prun's completed Unity guest-window milestone explicitly ran no Steam/DRM code
and does not supply that seam. Reserved execution files remain untouched.

Next steps remain the existing three console process/IPC fixtures, internal Wine
object lifetime support, genuine AMD64 client/CEF launch, ordinary service-free
operation, Xbox sign-in and owned library/download, then original Steam API init,
ownership and callbacks through locked Play. One genuine sign-in is planned;
no supported launcher-session reuse is established. No expired QR scan requested.
No fresh host experiment, console transaction, API success or smoke pass is
claimed. LANE_DONE remains absent. Unrelated working-tree changes remain intact.

### stm — current directive reconciled with external handoff, 2026-10-27

Read the supplied steam64-status research and existing per-binary llvm-readobj
inventory. The prior milestone already verifies all 147 installed PE candidates
(99 AMD64, 48 i386), including the i386 SteamService.exe. No repeated binary
scan or memory investigation is needed. Corrected /workspace/x64-run/BLOCKED-upmp.md
to remove its stale memory-measurement acceptance gate and include the verified
inventory. The accepted 5 GB allowance is not a planning gate.

The genuine client remains required on Xbox. The reserved entry_kernel.h process
route still explicitly returns ERROR_ACCESS_DENIED. The specific external input
remains the execution owner's isolated guest process implementation and callable
console seam for the three existing process/IPC fixtures. This lane may not edit
that runtime area. Existing research records the subsequent AMD64 Steam/CEF,
service-free operation, genuine Xbox sign-in, owned library/download and original
Steam API callback steps. No supported launcher-session reuse is established;
plan one genuine client sign-in when Xbox login works.

No fresh console execution, authentication, usable QR or Steam API success is
claimed. No secrets were accessed. LANE_DONE remains absent. The pre-existing
BLOCKED-stm.md deletion and BLOCKED-stm.old.md remain unstaged.

### stm — latest directive: binary inventory and process-first scope, 2026-10-27 20:15

Read the supplied /workspace/x64-run/research/steam64-status.md and copied it to
research/steam64-status.md, marking its obsolete off-console/pinned-DLL conclusion
as superseded. The genuine Valve client must run on Xbox inside launcher ->
Wine -> uwph -> UWP Game mode. The 5 GB allowance is accepted: all older memory
research/planning gates in these notes are superseded. No new memory work ran.

Inspected every installed PE candidate with llvm-readobj --file-headers, reading
only binary extensions and recording relative paths, machine types and hashes.
research/real-client-evidence/all-client-pe.json records 147 binaries: 99 AMD64,
48 i386. Current steam.exe, CEF win64 helper and steamclient64.dll are AMD64.
SteamService.exe is locally verified i386, rather than inferred from reports.
Other helpers are individually recorded; installation presence is not a runtime
requirement. Trace indispensable helpers before adding x86 support.

Concrete next steps remain: execution owner supplies isolated guest processes
and console seam; pass the three existing process/IPC fixtures; implement internal
Wine object lifecycle without AF_UNIX/prohibited listeners; run genuine AMD64
Steam/CEF on Xbox; test normal service-free operation and trace actual failures;
authenticate through Valve UI; prove owned library and 3405340 download; prove
original API initialization, ownership and callbacks via locked Play and unchanged
smoke. The reserved CreateProcessW route still returns ERROR_ACCESS_DENIED.
No reserved runtime file changed. One additional genuine sign-in remains planned;
no supported launcher-session reuse is established. No tokens or session files
were inspected. Existing QR remains expired; no scan requested.

This update establishes binary architecture only. No fresh console run, login,
Steam API success or smoke result is claimed. LANE_DONE remains absent. Existing
unrelated BLOCKED-stm.md deletion and BLOCKED-stm.old.md remain unstaged.


### stm — named-pipe executable prerequisite, 2026-10-27 resume

Added tools/steam-map/process-fixture/named_pipe.cpp: original account-free
parent/child message-mode pipe discovery and request/reply with inheritance
disabled, overlapped server operations, child exit 37 and disconnect. Host Wine
passes; evidence is research/real-client-evidence/named-pipe-host.txt. Build uses
warnings as errors. Owned-probe boundary tests pass 4/4. This adds an executable
uwph acceptance gate without modifying prun-owned runtime files. Cancellation,
reconnect, client crashes and Steam's actual protocol remain untested.

Xbox remains the required client location. Run both process fixtures through
the console execution seam before staging the genuine x64 client. Process
isolation and internal named-object IPC still need implementation; the current
CreateProcessW route still returns ERROR_ACCESS_DENIED. No supported desktop
process shortcut is established. Library/download, positive original Steam API,
console memory peak below 5120 MB and locked Play remain acceptance gates.
One additional genuine client sign-in remains planned; no session injection.

Fresh locked Play attempt exited 99 acquiring CONSOLE.lock, before console
access; log: /workspace/x64-run/runs/stm/named-pipe-play.txt. No fresh console
result, refreshed QR, owned login or successful Steam initialization is claimed.
LANE_DONE remains absent. Pre-existing blocker deletion/rename stays untouched.


### stm — executable process prerequisite, 2026-10-27 resume

Xbox remains the only production client location. Added an original account-free
process/IPC fixture and build instructions in tools/steam-map/process-fixture/;
no prun-owned runtime files changed. Two x64 images with identical preferred
bases and ASLR disabled pass in a new disposable host Wine prefix: separate PIDs,
isolated writable globals, inherited mapping/event/pipe handles, shared update
to 42, pipe peek/read, parent wait and child exit 37. Evidence is committed at
research/real-client-evidence/process-fixture-host.txt. This is host-only proof,
not Steam, named-pipe discovery or an Xbox process implementation.

Research now cites Microsoft documentation: FullTrustProcessLauncher requires
the Desktop extension and runFullTrust; UWP multi-instance support is documented
for desktop, not Xbox. Neither is a supported shortcut to child processes on
the console. A thread or a false-success CreateProcess response cannot meet
the fixture. See research/GAMENATIVE-STEAM.md for source links and concrete gates.

Next steps: execution owner stages/runs both fixture images through Wine/uwph;
provides real guest address-space/process isolation; adds named-pipe/shared
object lifecycle in the socket-free Wine server; stages the unmodified x64
client; traces actual missing calls; tests ordinary service-free fallback before
adding WoW64; renders genuine login on Xbox; proves owned library/download and
original API ownership/callbacks; measures combined peak against 5120 MB; runs
locked Play and unchanged smoke. steamclient64 alone is not a logged-in client,
and retired -no-browser cannot remove current CEF requirements. Plan an additional
genuine client sign-in; no supported launcher-session import is established.

Fresh locked Play attempt returned 99 before console access because another
transaction held CONSOLE.lock. No fresh console result or smoke run is claimed.
Fixture compiles with warnings as errors and passes host Wine. Owned-probe
boundary tests pass 4/4; git diff whitespace check passes. Existing QR screenshot
is expired, no new scan requested, no secrets accessed. Xbox client execution,
owned library/download and successful Steam API remain unproved. LANE_DONE stays
absent. Existing blocker rename/deletion is preserved without staging it.


## stm — item 54 investigation, 2026-10-26

Branch lane/stm started clean at 75b719bc. Read REBUILD.md first, inherited notes,
PLAYABLE.md, PLAN-0600.md, smap map/validator and launcher CM/Play implementation.
No runtime execution files changed. The brief's closing lane/upmp and
BLOCKED-upmp names conflict with the assigned lane/stm; keep commits on lane/stm,
and provide both requested blocker filenames if acceptance remains impossible.

The partial console-copy GameAssembly hash matches the complete owned download;
its Steam API plugin and metadata are absent, so static analysis uses pinned
owned-download files. Added direct-call/native-export inventory tool and report
research/STEAM-API-PATH.md. Static calls are not a successful-startup runtime trace.
Megabonk calls SteamInternal_SteamAPI_Init through managed InitEx, not a legacy
native SteamAPI_Init export. Existing smap is a static gate, not steamhost.

Added off-device init_probe.cpp invoking the original Valve DLL with the game's
25 interface-version literals. MinGW build passes warnings-as-errors. Wine
returns native result=1, "Could not determine Steam client install directory."
Exit 10 is failure, never successful initialization. No login/token access or
forced init result. DP_PASS and TS_AUTHKEY removed from child environment.

Real console Play and canonical unchanged smoke are running. Current source
Play has no guest process and emits gameCodeExecuted=false; lane prun owns that
prerequisite. No stub can honestly meet Steam-success acceptance on this path.

### stm — offline milestone and failed Play

Inventory retains 43 direct game-to-Steamworks call sites and native export-name
references; four focused inventory checks plus four existing smap validator tests
pass. No shim-success unit test exists because no working client-backed shim
exists. Generated static inventory explicitly excludes indirect/inlined paths.

Actual Xbox launcher 0.5.100.0 Play ran under the shared lock, using labelled
synthetic input. The unchanged test exits 1, NO LAUNCH RESULT. Current logs use
recursive dependency mapping and say entry NOT called / TLS NOT initialized.
Downloaded result JSON predates this session and is stale; do not count it.
Evidence: /workspace/x64-run/runs/stm/play-3405340 and play-test.txt. No deployment
by this lane. No Steam success or Xbox game execution claimed.

First canonical smoke exits 1 (3 passed, 6 failed) due missing local guest fixtures
(curl exit 26, put HTTP 000), matching inherited pe32's prerequisite issue. Building
out/uwph and out/tls via existing host/build.sh before rerunning unchanged smoke.

### stm — final blocked handoff (2026-10-26)

Resumed at 82e58789 on lane/stm. Both fixture builds from the previous turn
completed. Fresh unchanged xbox-test/smoke-all.sh now exits 0: 9 passed,
0 failed. Evidence: /workspace/x64-run/runs/stm/smoke-final.txt and smoke-final/.
Console sections use the shared CONSOLE.lock throughout each transaction via
the existing smoke fd-9 protocol. Native console test PEs pass TLS, two threads,
DLL lifecycle and cross-image C++ catch; these do not prove game execution or
Steam initialization. The original failed smoke evidence remains preserved.

Four static inventory checks and four smap hash/gate tests pass again. No
successful recorded-call shim test exists because no client-backed shim exists.
Original Valve init still has only failed off-device evidence, and the actual
Xbox Play attempt still exits 1 with NO LAUNCH RESULT. Neither gate was weakened.

Recorded BLOCKED-stm.md in the worktree and /workspace/x64-run; appended a clearly
labelled Steam cross-reference to the brief's requested BLOCKED-upmp.md without
replacing that lane's report. Recommendation remains unmodified pinned Valve
client plus original API DLL, anonymous ABI gate first, genuine owned session
next. Missing real Play execution belongs to prun. No changes to execution,
launcher/login/downloader, game files, Valve files or test gates. No push.

Acceptance is incomplete; LANE_DONE is absent. Stop under the explicit blocker
instruction. Commit locally on the assigned lane/stm branch, then run backup.sh.

### stm — backup storage failure

Ran /workspace/x64-run/backup.sh after commit 973fe6c5. It exited 1 after
reporting ok=16 skipped=0 fail=1. The log reports the lane/stm bundle copied,
but the persistent store subsequently failed with "Transport endpoint is not
connected". Backup completion and continued bundle availability are therefore
not verified. Transcript: /workspace/x64-run/runs/stm/backup-final.txt.
Local commits remain intact. Restore the external store mount before retrying.

### stm — resume audit (2026-10-27)

Resumed on lane/stm at 44de82d2 with a clean worktree. Re-read REBUILD.md,
PLAYABLE.md, the Steam path report and blocker. Four inventory tests and four
pinned-client validator tests pass again. Current launch source still emits
`gameCodeExecuted=false`; the mapped branch still logs `entry NOT called`.
The prerequisites in BLOCKED-stm.md remain unresolved. No console access,
account-token access, runtime edits or new success claims during this audit.
Prior full smoke evidence remains 9/9; it was not rerun during this audit.

Retried backup.sh, transcript /workspace/x64-run/runs/stm/backup-resume.txt.
The retry is still running while this note is committed; only its final exit
and per-bundle verification can establish backup completion. LANE_DONE remains
absent because Steam/Play acceptance is incomplete. Stop under blocker rule.

### stm — blocked resume closure (2026-10-27)

Rechecked the existing handoff without touching the console or account session.
The branch still writes gameCodeExecuted=false. No anonymous client helper or
client-backed shim has been added; the same implementation prerequisites remain.
All eight inventory/hash-gate tests pass again. Previous full smoke remains 9/9;
no fresh console acceptance or Steam success is claimed. LANE_DONE stays absent.

The previous backup-resume transcript stops after four bundles, with no final
status, and no backup.sh process remains. That attempt cannot be counted as a
completed backup. Run backup.sh after this local closure commit; retain its exit
status separately. Stop under the explicit blocker instruction, with existing
BLOCKED-stm.md and the external BLOCKED-upmp.md cross-reference intact.

### stm — current resume audit (2026-10-27)

Re-read the rebuild/playability requirements and existing Steam report/blocker.
All four inventory tests and four pinned-client validator tests pass again.
The assigned branch remains lane/stm. Current Play still serializes
`gameCodeExecuted=false`; no client-backed helper has appeared in this tree.
No console access, account access, runtime changes or successful-init claim.
Existing BLOCKED-stm.md and external blocker handoff remain applicable;
LANE_DONE remains absent. Stop under the explicit blocked-lane instruction.
Retry backup.sh after this local audit commit; its transcript and exit status
are recorded separately, without assuming the persistent store has recovered.

### stm — prerequisite correction, 2026-10-27

Cross-lane evidence now supersedes the historical mapping-only execution blocker:
lane/prun reports actual Xbox Play on launcher 0.5.157.4 executing guest entry,
TLS/DllMain and Unity CreateWindowExW. Its next failure is
`gdi32.dll!GetDeviceCaps` at UnityPlayer+0x7389dd. Evidence:
/workspace/x64-run/runs/prun/play-owner-integrated/ and lane/prun LANE_NOTES.md.
This was reviewed as another lane's evidence, not rerun by stm. No Steam DLL
code ran in that test. The current stm source remains the older mapping probe.

The independent Steam blocker remains: no validated anonymous client helper,
real owned-app native context or working callback bridge. Original Valve init
has only failure evidence. Four inventory tests and four pinned-client validator
tests pass again. Prior stm smoke remains 9/9; no fresh console test is claimed.
No runtime, Valve binary, account session or test gate changed. Acceptance stays
incomplete; LANE_DONE stays absent. Stop under the explicit blocker instruction.

### DIRECTIVE 2026-10-27 11:10 (João)
First version = visuals + game logic. Audio is NOT a blocker (deferred). Prioritise: Unity window + D3D11 device, menu visible/stable, input starts a run, playable run at measured fps. Skip audio work. See /workspace/x64-run/PLAYABLE.md.

### stm — GameNative research and actual Valve lifecycle, 2026-10-27

Followed João's new directive instead of repeating the old prerequisite audit.
Fetched both public reference repos under /workspace/ref without modifying their
working files, posting or pushing. Added research/GAMENATIVE-STEAM.md with pinned
source citations for QR/credential/CM authentication, encrypted tickets, depot
manifests/downloads, replacement modes, real Windows headless/GUI modes, Bionic
Proton bridge, registry/environment, pipes and callback ownership. Both real
bootstrap implementations are explicitly source-withheld in THIRD_PARTY_NOTICES;
their internal login ABI cannot be recovered from the public Kotlin wrappers.
Rejected Pluvia's hard-coded init replies, GameNative's gbe_fork/coldclient modes,
Steamless and use of the restricted proprietary helpers outside their package.

Implemented anonymous_host.cpp and run_anonymous.py: full file pins before load,
immutable open DLL handles, constrained loader search, pinned factory/vtable/method
dispatch, real Valve global-user/pipe creation, BLoggedOn, RunFrame and callback
retrieval/release. Fresh Wine prefix, allowlisted environment, no account/token
input. New original-game API mode publishes the actual host PID/client paths and
anonymous ActiveUser=0 in that disposable registry. It calls original Valve init
with Megabonk's exact 25 interface versions and never claims anonymous ownership.
Existing read-only static validator remains unchanged; no login/revocation method
is exposed by this helper. No launcher/runtime route or game/Valve file changed.

Fresh OFF-DEVICE evidence: anonymous lifecycle exits 0; real global user and pipe,
ten engine frames, five genuine callbacks retrieved/freed, loggedOn=false.
Original API against the live anonymous engine exits 10 with native result=3,
`No SteamUser023`, progressing past missing install-directory discovery. This is
not Steam init success. Raw warnings retain missing SDL3.dll/video64.dll/query
executable, adapter error 13 and process-exit thread warnings; four pinned DLLs
are a static lower bound, not a complete production client distribution.

Strengthened init_probe.cpp: after native init 0, pump original callbacks and
require a logged-in SteamUser023 plus BIsSubscribedApp(3405340) before exit 0.
It accepts no credentials. Fresh no-client negative run still exits 10 with
`Could not determine Steam client install directory.` No authenticated positive
result exists. Both C++ builds pass warnings-as-errors. Native pin refusal tests
2/2, inventory 4/4, existing pinned-map validation 4/4 pass.

Actual Xbox Series S Play, installed prun launcher 0.5.157.4, under one inherited
fd-9 CONSOLE.lock transaction: unchanged play-test.sh 3405340 120 exits 0;
gameCodeExecuted=true, Unity stops at gdi32.dll!GetDeviceCaps +0x7389dd before
Steam. No deployment or claim that the new helper ran on Xbox. Synthetic input
was labelled. Fresh unchanged canonical smoke-all.sh exits 0: 9 passed/0 failed,
including native console host/TLS sections using its existing lock protocol.
Full evidence: /workspace/x64-run/runs/stm/gamenative-{play,smoke}*; portable
selected evidence is retained in research/gamenative-steam-evidence/.

Specific missing external input: a usable genuine Valve client installation and
João's interactive signed-in session in a known Wine prefix for the owned-session
oracle; alternatively legitimately supplied current Windows bootstrap source/ABI.
No desktop Steam executable found in workspace search or standard home locations.
Asked for paths only, never secrets. The console launcher's existing CM login is
not a native Valve user context. Do not guess token-login slots beside revocation
methods. Authenticated bootstrap, game callbacks and Play integration remain
unfinished. LANE_DONE remains absent. Preserve the pre-existing blocker deletion
and BLOCKED-stm.old.md rename; do not resurrect obsolete status files.

### stm — owned-session probe runner, 2026-10-27

Added run_owned.py to make the legitimate desktop-client alternative executable
once an interactively signed-in Wine prefix is supplied. It pins the original
Megabonk API, stages the exact interface versions and builds init_probe.cpp with
warnings as errors. It uses an allowlisted environment and prints only fixed
diagnostic fields, discarding raw Wine/client output. It does not start Steam,
read credential files, alter registry discovery, or stop the supplied wineserver.
No replacement API, synthetic ownership or guessed private login ABI was added.

Four runner boundary tests, four inventory tests and four pinned-validator tests
pass. The validator requires its documented client-directory argument; an initial
unittest-discovery invocation omitted that argument and failed, then the documented
invocation passed 4/4. A fresh disposable no-client Wine prefix returns native
init result=1, loggedOn=false, subscribed=false, callbackPumpFrames=0; runner exits
10 with steamInitProven=false. The fixture needed wineserver registry flush before
its new system.reg was visible; the first fixture attempt exited 2 before running
the probe. This was off-device only. Existing console Play/smoke evidence above
was not rerun for this diagnostic-only addition. No runtime/test gate changed.

Asked for the path of an existing genuine-client signed-in Wine prefix, never
passwords/tokens. That authenticated external session remains missing; the runner
does not solve native login or Xbox Play integration. No LANE_DONE created.

### DIRECTIVE 2026-10-27 12:07 (João): see top of lanes/stm.prompt (real Valve client under Wine, one QR scan acceptable, no token injection).

### HARD REQUIREMENT 2026-10-27 12:09 (João): Steam client must run ON the Xbox; no off-console design. See top of lanes/stm.prompt.

### stm — Xbox-only real-client design and official bootstrap, 2026-10-27

Applied João's 12:09 requirement: genuine client must run inside the Xbox stack.
Host Wine is a scratch oracle only. Superseded the earlier request for someone
else to supply a host installation. Downloaded Valve's official unmodified
SteamSetup.exe; SHA256 and PE observations are in research/GAMENATIVE-STEAM.md.
Installer and extracted Steam.exe/SteamService.exe are i386 (0x014c). Current
self-updated executable architecture remains to be observed; Valve's 64-bit OS
requirement alone does not settle it. steamclient64/service loading does not
replace an authenticated client process.

Started disposable host prefix installation. Wine 10 lacks wine32; first attempt
created a prefix but no Steam install. Package retrieval failed via HTTP 500 and
HTTPS TLS EOF; trying alternate signed Debian mirrors. No certificate/signature
checks disabled. No login/token files read, no account sign-in attempted, no QR
screenshot yet. Raw setup logs remain /workspace/x64-run/runs/stm/real-client/.

Added concrete Xbox steps to the research report: updated PE architecture gate;
actual WoW64 if indispensable; guest process isolation and CEF lifecycle;
in-process wineserver broker with pipe/handle/event semantics without AF_UNIX;
no dependence on ports >=57344; genuine QR login and private session persistence;
client library/download/ownership/callback gates; combined console memory peaks
under 5120 MB; measured flag experiments; final locked Play and unchanged smoke.
Current entry_kernel.h explicitly rejects CreateProcessW, a concrete process
support gap. No runtime execution files changed. Plan one additional client
sign-in; no documented launcher-token import was established, and host login
cannot be assumed to persist on the console. No host-client production design.

### stm — genuine Valve login screen reached (host scratch only)

Alternate signed mirrors succeeded; wine32:i386 installed. Fresh Wine prefix:
`/workspace/x64-run/runs/stm/real-client/prefix-full`. Official silent install
succeeded, genuine updater downloaded/extracted both stages and restarted.
Final updater reports October 21, 2026. Critical new observation: final
`steam.exe`, `steamclient64.dll` and `bin/cef/cef.win64/steamwebhelper.exe` are
AMD64. `bin/SteamService.exe` remains i386. Earlier intermediate x86 steam.exe
inventory was taken before update completion and is superseded. Final hashes
and full PE lists are committed under research/real-client-evidence/. Added
inventory_client.py: scans EXE/DLL headers and hashes only, never login state.

Genuine login UI rendered with -cef-disable-gpu; labels are blank in this Wine
rendering, but account/password boxes, sign-in button and QR are visible. After
the initial QR spinner cleared, saved the scannable login screenshot exactly at
/workspace/x64-run/runs/stm/steam-login-qr.png (mode 0600, outside git). Asked for
João's own mobile-app scan; client remains open. QR must be refreshed if expired.
No credential/token injection or copying. Saved session transfer is not planned.

Original Megabonk DLL probe in this same live prefix before sign-in returns
nativeInitResult=1, loggedOn=false, subscribed=false, callbackPumpFrames=0,
steamInitProven=false. Evidence: real-client/prelogin-probe.json. This is an
honest negative test, not a working Steam context. Owned library/download and
API positive test now specifically await interactive client authentication.
No fresh console Steam test was run; the prior Play still stops before Steam.
Four owned-runner boundary tests and Python syntax/diff checks pass. No
LANE_DONE: Xbox client process/IPC/UI/memory work remains substantial.

### Resume measurement: current login workload (2026-10-27)

The running scratch client remains on its own login screen. No owned-library,
Megabonk download or successful native API result is established. The QR expired;
clicked its refresh control and retained the requested private screenshot path.
A refresh spinner is not a usable QR; verify the image before asking for a scan.
Authentication is a specific external input for the positive scratch oracle,
not a reason to claim the Xbox implementation is complete.

Added `tools/steam-map/snapshot_client.py`, a read-only PID memory sampler. It
reads only process names and smaps_rollup, never command lines, environment,
registry or session files. Mapping-path selection found seven processes with
Steam modules. Evidence: `research/real-client-evidence/login-memory-host.json`.
Their combined PSS was 2,020,353 KiB (about 1.93 GiB); summed RSS was 2,342,956
KiB and double-counts shared mappings. This excludes unselected Wine/server and
host UI processes, contains no game, measures one instant and proves no peak.
No subtraction of this host number from the Xbox budget is valid.

Despite `-cef-disable-gpu`, CrGpuMain, CrRendererMain, network and storage utility
processes exist. GPU-disable does not remove the CEF process requirement. The
x86 SteamService.exe also runs in this successful login configuration. Its
necessity remains unproven; do not promise an x64-only service-free design.
`-silent` and historical `-no-browser` remain unverified experiments, not an
approved headless architecture. The Valve Developer Community command-line
page again returned 403 during source verification.

Next implementation gates, in dependency order:
1. Prototype two simultaneous genuine x64 guest processes through the Wine
   process model, with separate image/TLS/exception state and handle lifetime.
   Test overlapping preferred image addresses before attempting CEF. Current
   CreateProcessW returns ERROR_ACCESS_DENIED; a second thread is insufficient.
   UWP cannot be assumed to supply desktop child processes. If native address
   isolation cannot be provided under documented UWP features, report that as
   an architectural failure rather than relaxing the Xbox-only requirement.
2. Prove named-pipe request/reply, shared memory, events, cancellation and teardown
   through an in-process wineserver broker on Xbox without AF_UNIX or forbidden
   listeners. Then test genuine Steam discovery and original game API traffic.
3. Stage the complete unmodified current client, launch x64 steam.exe through
   launcher/Wine/uwph, and identify the first unsupported import or service call.
   Record the exact request before adding any compatibility implementation.
4. Authenticate on Xbox through Valve UI, test owned library and client-managed
   download of 3405340, then original API init, subscription and callbacks in
   the same guest namespace. Expect an additional client sign-in; no supported
   launcher-session import is established. Never transfer scratch session files.
5. Measure actual UWP current/peak memory for login, library, download, startup
   and a five-minute game run under 5120 MB, then locked Play and unchanged smoke.

Validation: owned-runner boundary tests pass 4/4 with tools/steam-map on
PYTHONPATH. An initial invocation from repository root lacked that import path
and failed before tests; the corrected discovery invocation passed. New sampler
syntax and git diff whitespace checks pass. No console mutation/test, runtime
edit, sign-in, successful Steam initialization or LANE_DONE is claimed here.

QR follow-up: two refresh attempts showed a spinner, then returned to the expired
refresh overlay. The current steam-login-qr.png is not scannable. The earlier
scan request cannot be completed with that image; restore a live QR before
requesting authentication again. No successful scan/sign-in is assumed.

### Primary-source correction and current-client import gate (2026-10-27)

Valve explicitly announced retirement of `-no-browser` and `-noreactlogin` on
December 8, 2022, targeting general release in January 2023:
https://steamcommunity.com/groups/SteamClientBeta/discussions/3/3710433479207750727/
The announcement describes the browser dependency of the client UI. This
supersedes treating a historical command-line list as evidence of a supported
headless design. Do not budget away CEF or use an obsolete client as a workaround.
`-silent` and `-cef-disable-gpu` do not establish a browser-free runtime.

Added reproducible static import inventory: `tools/steam-map/client_imports.py`
against the complete updated scratch installation. Evidence:
`research/real-client-evidence/client-imports.json`. Only four fixed executable
paths are read; no registry, credentials, session files or process arguments.
Both ordinary and delay imports are included; dynamically loaded dependencies
remain outside this lower bound. Counts include named and ordinal imports:

| Component | Machine | Static imports |
|---|---|---:|
| steam.exe | AMD64 | 342 |
| steamclient64.dll | AMD64 | 788 |
| steamwebhelper.exe | AMD64 | 665 |
| SteamService.exe | i386 | 266 |

Concrete uwph process gate must cover more than `CreateProcessW`:
- Engine imports `CreateProcessA/W`, `CreateProcessAsUserW`, process enumeration,
  `ReadProcessMemory`/`WriteProcessMemory`, job assignment and limits, file mapping
  and `PeekNamedPipe`. Browser helper also imports process mitigation queries and
  setters, job termination and process-handle counts. Inventory does not prove
  every call executes; trace each exercised call before deciding requirements.
- Implement process handles and IDs, exit/wait state and inherited pipe/mapping
  handles consistently across the Wine server. Do not expose real host process
  enumeration or memory access to guest processes. Job-object and mitigation
  behavior needs explicit compatibility results; false success hides failures.
- Keep the architecture gate focused on the updated x64 entry and CEF. The x86
  service's presence is observed, but its necessity for ordinary per-user library,
  downloading and API IPC is unresolved. Test without installing/elevating a
  service through the genuine client's normal fallback; never fabricate service
  success. Add WoW64 only if an exercised indispensable component requires it.
- First process fixture must use two executables with overlapping preferred
  image bases, inherited pipe handles, shared mapping/event exchange, child exit
  and parent wait. A common address-space loader needs an evidenced solution to
  process isolation before trying real Chromium; relocation alone is no proof.

Source check confirms `xbox-test/uwph/host/entry_kernel.h:19-20` still returns
`ERROR_ACCESS_DENIED` for CreateProcessW. This report does not modify prun-owned
execution routes. Fresh visual inspection of the live scratch login window
confirms the expired QR overlay remains after a refresh click. No current QR
scan is requested, and no login, owned library/download or API success is claimed.
The requested screenshot path retains its earlier expired image; it must not be
presented as a usable challenge. Xbox client execution remains unimplemented;
a user scan alone cannot resolve process/IPC support. No LANE_DONE.

### stm — service-free genuine login surface, host only

The required production design remains Steam on Xbox inside launcher -> Wine ->
uwph -> UWP Game mode. This experiment ran only on the Linux scratch host.
No Valve binary, account state, discovery registry entry or authentication token
was modified. The ordinary Windows service Start setting was changed temporarily.

Evidence: `research/real-client-evidence/service-disabled-host.json`. The updated
client was shut down using `steam.exe -shutdown`. Wine's `sc config` returned
without applying the requested startup change: a precise query still showed
Start=3. Set only `HKLM\System\CurrentControlSet\Services\Steam Client Service`
value Start to DWORD 4. A restart of Steam inside the existing Wine server still
ran the service despite that stored setting, so that trial was inconclusive.
After restarting this scratch prefix's Wine server, genuine Steam launched with
`-cef-disable-gpu`, rendered its login surface and CEF children, and `sc query`
reported Steam Client Service STOPPED while Start remained 4. No service image
was renamed or patched, and no service success was fabricated. Restored Start=3
after measurement; the live Wine server may retain its disabled configuration
until its next restart. The screenshot remains outside git at
`/workspace/x64-run/runs/stm/real-client/service-disabled.png`.

This establishes only a service-free login surface on host Wine. It does not
establish a pure x64 Wine installation, completed authentication, library display,
downloads, Steam API success or Xbox feasibility. Original Megabonk API probe in
the same prefix returned result=1, loggedOn=false, subscribed=false and zero
callback frames. The QR remains expired, so no new scan is requested and the
saved `steam-login-qr.png` must not be presented as usable.

Concrete Xbox consequence: prioritize the genuine updated AMD64 steam.exe and
CEF process/IPC gates before implementing x86 SteamService support. Once process
isolation works, test normal per-user operation with service absent, then owned
library, client-managed download of 3405340 and original steam_api64 callbacks.
Only those later observations can settle whether the x86 service is dispensable.
Never substitute steamclient64.dll or a stub service for the authenticated client.

For networking, Valve's [connectivity documentation](https://help.steampowered.com/en/faqs/view/669A-2F68-D1D1-A5EC)
provides the official troubleshooting entry point. Our console's rejected local
AF_UNIX sockets and dropped high listener ports must be treated separately from
outbound Steam traffic. Do not infer that an outbound remote port is blocked from
a failed local listener. Inventory actual connections without payloads or session
material before selecting a supported transport; no arbitrary port forwarding
or off-console client becomes the production design.

A fresh locked Play attempt exited 99 acquiring CONSOLE.lock, before any console
access; log: `/workspace/x64-run/runs/stm/service-gate-play.txt`. Xbox execution,
combined peak under 5120 MB and successful original Steam API through Play remain
unproved. No LANE_DONE. Authentication is missing for the positive scratch test;
Xbox guest process isolation and socket-free IPC require implementation regardless
of that external input.

### stm — pipe lifecycle acceptance fixture

Added original account-free `pipe_lifecycle.cpp`: pending connect/read cancellation
must complete with ERROR_OPERATION_ABORTED; the same server instance reconnects;
a closed peer completes a pending read with ERROR_BROKEN_PIPE. Host Wine passes
in a new disposable prefix; warnings-as-errors build passes. Evidence is committed
at research/real-client-evidence/pipe-lifecycle-host.txt. This single-process test
complements the existing process/discovery fixtures, not a guest process or Steam
implementation. Process-crash cleanup remains untested.

Fresh locked Play attempt exited 99 before console access; evidence:
/workspace/x64-run/runs/stm/lifecycle-play.txt. No console Steam result, owned
library/download, refreshed QR or authenticated API success is claimed. Existing
QR remains expired. Xbox-only client design and one genuine sign-in remain the
plan. Execution owner must first provide guest process isolation and run all
three fixtures through the Wine/uwph loader; this lane leaves reserved runtime
files untouched. Then stage genuine AMD64 Steam/CEF, trace missing calls, sign in
on Xbox, test library/download/original API callbacks and measure combined peak
below 5120 MB through locked Play. LANE_DONE remains absent.

### stm — Xbox-only decision and exact integration handoff, 2026-10-27

Reconciled the current acceptance state at the top of research/GAMENATIVE-STEAM.md.
Genuine Valve Steam on Xbox remains the primary design candidate; feasibility
is unproved. Host login is established, but owned library, download of 3405340,
positive original API and the console combined memory peak remain unproved.
Valve's no-browser retirement announcement was rechecked successfully. Current
AMD64 client/CEF and the service-free login observation supersede assuming that
32-bit steam.exe or browser-free operation is the mandatory starting point.

Specific missing input is the prun/uwph owner's guest process implementation and
callable parent/child execution seam. entry_kernel.h still denies CreateProcessW.
The lane must not edit reserved execution routes; no additional host fixture or
false-success stub can supply process isolation. Research records the concrete
fixture -> internal IPC -> genuine x64 client -> Xbox sign-in/library/download ->
original API -> measured locked Play sequence. One additional genuine sign-in
is planned; no supported reuse of launcher login is established. No scan is
requested because the saved QR is expired.

Fresh Play transaction used inherited lock descriptor 9 and a ten-second lock
wait, exited 99 before console access. Evidence:
/workspace/x64-run/runs/stm/console-client-readiness.txt. No console run or smoke
pass is claimed. No runtime code changed and LANE_DONE remains absent. Existing
BLOCKED-stm.md deletion and BLOCKED-stm.old.md remain untouched.

### stm — explicit external integration stop, 2026-10-27

Read the required rebuild/playable runbooks and reviewed the current execution
route again. Public reference research and host genuine-client login-surface
work are already complete; no new fixture or repeated login experiment is
needed before the execution-owner handoff. CreateProcessW still explicitly
returns ERROR_ACCESS_DENIED in the reserved uwph execution area.

Recorded the exact missing external integration input and existing executable
acceptance contract in /workspace/x64-run/BLOCKED-upmp.md, as requested by the
lane's impossible-acceptance rule. This is a dependency on the prun/uwph owner,
not a request for credentials and not a claim that Xbox feasibility is proven.
The research phrase “Host login is established” means the login surface only;
the genuine client has not authenticated. No fresh console test, QR, Steam API
success or smoke pass is claimed. Xbox remains the required client location.
No LANE_DONE is created. Existing blocker rename/deletion stays untouched.


### stm — remove stale browser-free helper guidance, 2026-10-27

Updated tools/steam-map/README.md to distinguish historical anonymous ABI
research from the selected genuine-client-on-Xbox implementation. Its old
unqualified CEF statement could incorrectly suggest a browser-free deliverable;
it now explicitly retains current-client browser processes and the combined
5120 MB budget. Historical commands and observations remain intact.

The existing research already covers the public GameNative/Pluvia sources,
excluded proprietary/replacement paths, genuine client installation and host
login surface, AMD64 component inventory, service-free login observation and
concrete Xbox integration sequence. No new client installation, authentication,
QR, console transaction or Steam API success occurred in this documentation
correction. The specific required external input remains the execution-owner
process isolation and callable console fixture seam described in
/workspace/x64-run/BLOCKED-upmp.md. Reserved runtime files remain untouched.
No LANE_DONE. Existing blocker rename/deletion remains unstaged.

### stm — reconcile latest prun console milestone, 2026-10-27

Reviewed the execution owner's latest /workspace/wt/prun/LANE_NOTES.md. Its
verified Xbox Play milestone reaches Unity CreateWindowExW and then stops at
GetDeviceCaps. The owner explicitly reports no Steam/DRM DLL execution. That
result supersedes older game-entry failures only; it does not supply the guest
parent/child process seam required by the genuine Valve client and CEF.

This lane's entry_kernel.h still rejects CreateProcessW with
ERROR_ACCESS_DENIED. The reserved execution routes remain untouched. The exact
external input remains an execution-owner implementation of isolated guest
processes plus a callable console fixture route in the shared Wine namespace,
then console passes for the three existing process/IPC fixtures. The owner's
Unity milestone alone is insufficient evidence to stage or authenticate Steam.

No new console transaction, authentication, usable QR, library/download test,
Steam API success or memory measurement occurred in this reconciliation. The
Xbox-only production design, additional genuine client sign-in and combined
5120 MB acceptance budget remain unchanged. Research steps 1–2 and host login
surface evidence already exist; implementation is incomplete. LANE_DONE stays
absent. Existing unrelated blocker deletion/rename remains unstaged.

### stm — reconcile API-path decision with Xbox-only directive, 2026-10-27

Corrected research/STEAM-API-PATH.md, whose opening still recommended the retired
pinned-DLL helper and whose options still described an off-console bridge as
potentially compliant. The selected path is now the complete genuine client
with CEF on Xbox; older experiments are explicitly historical. Reverified Valve's
no-browser retirement announcement. Existing GameNative research and concrete
process/IPC, genuine login/library/download, original API and combined 5120 MB
steps remain in research/GAMENATIVE-STEAM.md. No production PC-client alternative.

The required external input remains the execution owner's isolated guest process
implementation and callable console fixture seam. Reserved runtime files are
untouched. No new authentication, QR, console run, API success or smoke result
is claimed. Existing expired QR must not be shown as usable. LANE_DONE remains
absent; the unrelated blocker deletion/rename is left unstaged.

### stm — final execution-owner handoff checked, 2026-10-27

Reviewed prun's completed acceptance report: console package 0.5.157.4 reaches
Unity's guest window record, stops at GetDeviceCaps, and explicitly runs no
Steam/DRM code. Its unchanged smoke result is the prun owner's evidence, not
a new stm test. The current stm CreateProcessW route still returns
ERROR_ACCESS_DENIED. The required isolated guest parent/child execution seam
is therefore still missing, despite the completed game-window milestone.

No further host fixture or repeated expired QR can resolve this reserved runtime
dependency. The concrete external input is implementation and console execution
of the three existing process/IPC fixtures by the execution owner, as specified
in research/GAMENATIVE-STEAM.md and /workspace/x64-run/BLOCKED-upmp.md. The
next genuine-client step after that input is unmodified AMD64 Steam plus CEF
on Xbox, ordinary service-free operation, genuine sign-in, owned library and
3405340 download, original API callbacks, combined peak below 5120 MB and
locked Play acceptance. Feasibility remains unproved; no off-console design.

No new console transaction, authentication, QR, Steam API success or smoke
result is claimed. No tokens were accessed or injected. One additional genuine
sign-in remains planned. LANE_DONE remains absent. Existing unrelated
BLOCKED-stm.md deletion and BLOCKED-stm.old.md remain unstaged.

## BLOCKED-stm.md

# stm blocked: genuine Steam initialization prerequisites

Lane: /workspace/wt/stm, branch lane/stm, item 54.

The requested successful Steam initialization through launcher Play is not
achieved. This is an implementation prerequisite failure, not a finding that
all legitimate solutions require a bypass.

1. Off-device original Valve steam_api64.dll initializer for appid 3405340
   returns result=1, error="Could not determine Steam client install directory."
   Probe exit=10. No validated anonymous steamhost, authenticated client app
   context, native pipe/user handles or callback bridge exists in this tree.
   The existing smap gate permits read-only validation; it is not a running client.
   Launcher QR/CM download login alone does not supply these native interfaces.
2. Actual Xbox Play on installed launcher 0.5.100.0 exits 1, "NO LAUNCH RESULT".
   Current logs report "entry NOT called" and TLS NOT initialized. This source
   branch is a mapping probe with gameCodeExecuted=false. Its execution seam
   belongs to lane prun and is explicitly outside this lane's edit scope.
   The downloaded result JSON predates the run and cannot count as fresh proof.

Evidence: /workspace/x64-run/runs/stm/{init-probe.txt,play-test.txt,play-3405340/}.
Analysis and alternatives: research/STEAM-API-PATH.md; static inventory contains
43 direct call sites, not a complete recorded successful startup sequence.
Eight focused inventory/map checks pass. No successful shim unit test exists.

Resume prerequisites: validate the pinned Valve helper anonymously; establish
João's legitimate owned-app client context only after that gate passes; provide
prun's real game execution path; then test callback forwarding and actual init
through Play. Keep original Valve binaries and hash/revocation gates intact.
No account tokens were used, no fake init success was added, and no security or
DRM bypass was attempted. LANE_DONE must remain absent.

Final unchanged full smoke: exit 0, 9 passed / 0 failed. Evidence:
/workspace/x64-run/runs/stm/smoke-final.txt. Steam/Play acceptance remains blocked.

### stm — backup storage failure

Ran /workspace/x64-run/backup.sh after commit 973fe6c5. It exited 1 after
reporting ok=16 skipped=0 fail=1. The log reports the lane/stm bundle copied,
but the persistent store subsequently failed with "Transport endpoint is not
connected". Backup completion and continued bundle availability are therefore
not verified. Transcript: /workspace/x64-run/runs/stm/backup-final.txt.
Local commits remain intact. Restore the external store mount before retrying.

### stm — prerequisite correction, 2026-10-27

Cross-lane evidence now supersedes the historical mapping-only execution blocker:
lane/prun reports actual Xbox Play on launcher 0.5.157.4 executing guest entry,
TLS/DllMain and Unity CreateWindowExW. Its next failure is
`gdi32.dll!GetDeviceCaps` at UnityPlayer+0x7389dd. Evidence:
/workspace/x64-run/runs/prun/play-owner-integrated/ and lane/prun LANE_NOTES.md.
This was reviewed as another lane's evidence, not rerun by stm. No Steam DLL
code ran in that test. The current stm source remains the older mapping probe.

The independent Steam blocker remains: no validated anonymous client helper,
real owned-app native context or working callback bridge. Original Valve init
has only failure evidence. Four inventory tests and four pinned-client validator
tests pass again. Prior stm smoke remains 9/9; no fresh console test is claimed.
No runtime, Valve binary, account session or test gate changed. Acceptance stays
incomplete; LANE_DONE stays absent. Stop under the explicit blocker instruction.
