# Megabonk Steam API path (lane stm, 2026-10-26)

## Decision and acceptance status

The selected design is the complete, unmodified genuine Valve Steam client
running on Xbox inside launcher -> Wine -> uwph -> UWP Game mode, with the
original Megabonk steam_api64.dll. The October 27 directives supersede the
historical pinned-DLL helper proposal and every off-console client option.
GameNative's proprietary helper is excluded. Host Wine is a scratch oracle only.

Current evidence and concrete integration steps are maintained in
[GAMENATIVE-STEAM.md](GAMENATIVE-STEAM.md). Updated steam.exe and CEF are AMD64;
SteamService.exe is i386. Host service-free login surface is established, but
neither authenticated service-free operation nor Xbox feasibility is established.
Valve [retired -no-browser and -noreactlogin](https://steamcommunity.com/groups/SteamClientBeta/discussions/3/3710433479207750727/).
CEF remains part of the process requirements. João accepts the 5 GB Game-mode
allowance; older memory-budget work is superseded.

The exact missing external integration input is the prun/uwph owner's isolated
guest parent/child process implementation and callable Xbox fixture seam.
CreateProcessW still returns ERROR_ACCESS_DENIED in the reserved execution area.
Run the existing process, named-pipe and lifecycle fixtures on Xbox first; then
stage genuine Steam/CEF, trace missing calls, render Valve login, authenticate,
show the owned library, download 3405340 and prove original API init and callbacks.
Internal Wine object IPC must work without AF_UNIX or prohibited listeners.
Verify through locked play-test.sh.

No owned library/download, successful original Steam API or console client run
is established. Plan an additional genuine client sign-in; no supported launcher
session import is established. The saved scratch QR is expired. No LANE_DONE.
The analysis and dated experiments below are historical evidence; they do not
replace this selected design. An always-success initializer, synthetic ownership
or fabricated callbacks cannot meet acceptance.

## Inputs and reproducibility

`/workspace/megabonk-from-console/GameAssembly.dll` has SHA256
`4350a7ae25ba7aec35677c213fdceabbb638676bb00ab59131d7d9c37b6e3d9e`, identical
to the full owned download. The console-copy directory is incomplete: it lacks
the Steam API plugin and IL2CPP metadata. Analysis therefore uses the complete
`/workspace/steam-owned/games/megabonk` tree, pinned by the existing smap analyzer:

- Original API DLL: `1add7f151fa644870a735ae86e68d1f019f296130d8e7c0a7ed3ecc7482dccbc`.
- Metadata: `17b506a77616b943b414b9a42d35e3acbdaf71eec5dc6c470fc4da338010d550`.

No game/Valve binary was modified. No account/session file was read or copied.
Commands (analysis dependencies are in tools/steam-map/requirements-analysis.txt):

```sh
python3 tools/steam-map/inspect_game.py /workspace/steam-owned/games/megabonk
python3 tools/steam-map/inventory_game.py /workspace/steam-owned/games/megabonk
python3 tools/steam-map/test_validate.py /workspace/x64-run/runs/smap/valve
```

Checked-in `steam-api-path/startup.json` reproduces the existing startup oracle.
`steam-api-path/calls.json` adds direct call sites, tail jumps and native export
name references. These are **static evidence, not recorded calls**. The analyzer
omits indirect calls, delegates, reflection, inlining and bodies without a PE
unwind range. IL2CPP sharing can leave several names for one address. It excludes
cross-module folded targets from game-to-Steam edges; API types are filtered to
avoid treating shared value-type constructors as Steam initialization. A complete
successful-startup runtime call sequence remains unmeasured.

## Confirmed startup and Steamworks.NET requirements

| Stage | Original code | Meaning |
|---|---|---|
| Restart | SteamManager.Load call `0x52adf5` to wrapper `0x5e63e0` | `SteamAPI_RestartAppIfNecessary`; return-true branch is separate |
| Init | Load call `0x52ae04` to managed Init `0x5eb1c0` | managed Init invokes InitEx `0x5ea980` |
| Native initializer | `SteamInternal_SteamAPI_Init`, export RVA `0x8af0` | actual native entry; this DLL has **no SteamAPI_Init export** |
| Version negotiation | 25 NUL-separated versions in startup.json, in use order | includes SteamUser023, SteamUtils010, SteamInput006, SteamNetworkingSockets012, user stats 012, apps 008 and UGC 020 |
| Context after success | CSteamAPIContext.Init `0x5b6f50` | 27 compiled version literals, adding SteamClient021 and timeline V001 |
| Failure | Load branch `0x52afb4` | logs initialization error, returns; no Application.Quit on this branch |
| Update / teardown | SteamManager.Update / OnDestroy | managed RunCallbacks / Shutdown, conditional on initialization |

Steamworks.NET uses its managed callback dispatcher and native manual-dispatch
exports. A replacement implementing only legacy RegisterCallback/RunCallbacks
would not satisfy this build. Preserve real pipe/user handles, callback IDs,
payload sizes, call-result correlation and callback-buffer lifetime. Interface
validation is not proof every interface is used during gameplay.

The original native symbols include `SteamAPI_ManualDispatch_Init`,
`SteamAPI_ManualDispatch_RunFrame`, `SteamAPI_ManualDispatch_GetNextCallback`,
`SteamAPI_ManualDispatch_FreeLastCallback` and
`SteamAPI_ManualDispatch_GetAPICallResult`. The analyzer finds references to all
five, not proof that all five execute on the failed-init branch.

[GAME-CALLS.md](steam-api-path/GAME-CALLS.md) lists all 43 retained direct game
call sites with RVAs. These include SteamStatsManager.Init RequestCurrentStats;
GetStat/SetStat; GetAchievement/SetAchievement; leaderboard lookup/download and
result-reading; user identity, friends/avatar image reads, rich presence,
timeline events and current language. These are conditional/later game paths,
not an assertion that all run before the main menu. Callback registrations use
generic/delegate paths; their exact runtime sequence is not established by this
direct-edge inventory. In particular, a compiled StoreStats wrapper alone would
not prove an observed startup StoreStats call.

The previous smap run reached a rendered main menu **off-device under Wine** with
the original API DLL and failed Steam initialization. It did not establish a
Steam session, Xbox rendering, achievements or cloud. This corrects REBUILD.md's
older blanket startup-DRM assumption for these exact game hashes only.

## Existing pieces and missing pieces

- `tools/steam-map`: one pinned client build 1788652215; 340 mapped vtable
  entries, immutable hash/fingerprint checks, six read-only allowed call plans.
  It is not an executable ABI or steamhost. LogOn is denied today; credential
  revocation/destruction remains permanently denied. Keep this gate intact.
- `/workspace/x64-run/PLAN-0600.md`: headless steamhost is future work after
  loader execution. `research/WINE-BASE-REPORT.md` describes smap, not a helper.
- Launcher `steamdl.h`: authenticated CM websocket, request jobs, heartbeat,
  ownership/library/download services. QR login and downloaded ownership do not
  provide the native game's Steam pipe/user/context, IPC server or interfaces.
- `/workspace/ref/GameNative`: reference integration exists for another host;
  its Android mode and token sealing do not establish an Xbox client ABI.
  No bundled helper was executed, no real account token was used here.
- UWP references: PPSSPP Common/MemoryUtil.cpp uses VirtualProtectFromApp;
  uwp-crossbuild build-app.sh validates manifests and packages app layouts;
  Xenia has its separate UWP manifest. Existing launcher already supplies the
  GUI/MTA/CoreWindow/package scaffold. None supplies a Steam client session.
  No manifest, memory protection, windowing or package changes are needed for
  this offline probe; adding capabilities would not create a desktop Steam IPC
  environment. AF_UNIX and high-port restrictions from REBUILD.md remain.

## Historical options, superseded by October 27 directives

| Option | Legitimacy under the lane's rules | Effort / recommendation |
|---|---|---|
| Original API + pinned Valve client hosted inside the Xbox runtime | Matches the requested real-client/ownership path if genuine login and interfaces work; no Valve patching | High: anonymous ABI validation, host imports/IPC, real app context, callback delivery, then login. Superseded: use the complete genuine client, including CEF |
| Real PC Steam helper with a narrow authenticated IPC bridge | Rejected by the Xbox-only directive; do not implement | Medium/high; companion PC dependency, authenticated transport, context/callback forwarding. Not a drop-in proxy: local object pointers cannot cross IPC. No documented general Steamworks remote-client protocol was established |
| Own API DLL + CM session + ownership/app ticket | Ownership proof can be genuine; substituting local success and unimplemented interfaces is not the requested real-client path | Very high: independent Steamworks client, state/cache/handles, callback semantics, stats/cloud/leaderboards. Ticket alone is insufficient. Do not implement as an init shortcut |
| SteamCMD session / launcher library login reused as native API session | Genuine login/download capability; no established game's native API channel | Does not solve initialization; reject as sufficient proof |
| Always-success shim, emulated ownership, Steamless/Goldberg | Explicitly outside user authorization | Reject; not implemented |

Valve's [API overview](https://partner.steamgames.com/doc/sdk/api) requires a
running client, correct app ID/user context and an active license. Initialization
also establishes interface state. An app-ID development hint grants no license.
Its [authentication guide](https://partner.steamgames.com/doc/features/auth)
describes tickets as identity/ownership evidence for peers or backends, not a
replacement for those interfaces. Encrypted ticket validation needs the title's
private symmetric key; this project has no authority to obtain Megabonk's key.
A CM-issued ownership ticket is a different artifact and does not by itself
provide the native Steamworks runtime. No assertion is made that implementing
protocol compatibility is inherently a bypass; the missing behavior must be
implemented and verified rather than replaced with guessed success.

## Small host probe

`tools/steam-map/init_probe.cpp` calls the original DLL's actual initializer
using the 25 recorded static version literals, and shuts down only on real
success. It sets development app-ID environment hints for 3405340, never forces
a result, never logs in and never calls RestartAppIfNecessary. It is a diagnostic
PE under Wine, **not** a loader route, shim, successful recorded-call unit test,
or Xbox Steam-session implementation.

Built with MinGW (`-Wall -Wextra -Werror`, static runtime); original DLL hash was
verified by the smap analyzer. Run in smap's existing Steam-free Wine prefix with
DP_PASS and TS_AUTHKEY removed from the child environment. Actual result:

```text
Valve SteamInternal_SteamAPI_Init appid=3405340 result=1 error=Could not determine Steam client install directory.
```

Probe exit 10 denotes genuine native initialization failure. Evidence:
`/workspace/x64-run/runs/stm/init-probe.txt`, `init_probe.exe`, `versions.bin`.
No success slice was substituted. The historical anonymous-helper next step is
superseded by the full-client process/IPC sequence at the top of this report.

## Actual Xbox Play evidence

Ran unchanged `xbox-test/tools/play-test.sh 3405340 60
/workspace/x64-run/runs/stm/play-3405340` while holding the shared CONSOLE.lock
across the whole transaction; helpers reused the inherited descriptor. No package
install or version change was performed by this lane. Installed launcher was
`Grok.XboxTest_0.5.100.0_x64__dk715arakrne0`, newer than this worktree's source.
Synthetic open/press commands used the real launcher detail/Play handler.

Command exited **1**, `NO LAUNCH RESULT`. The current session reports recursive
dependency loading, GAP total=628 missing=458 win32u=120, then mapped PE images
and `MLP mapped: TLS/unwind/load-config retained but NOT initialized; entry NOT
called`. There is no current `launch: RESULT` or Steam-init success. A downloaded
launch_result.json has timestamp `2026-10-26T18:09:10Z`, before this session, and
is **stale**, not current-run acceptance evidence. Preserve it as such.
Screenshots, current boot.session.log and launch_probe.log remain in the run
directory; command transcript is `/workspace/x64-run/runs/stm/play-test.txt`.
The trace does not establish whether failure was a crash, hang or missing result;
this lane does not diagnose or change prun's execution layer.

## Resume verification and blocker

Rechecked source: launch.h passes a non-null mapped image to wxl::run;
host/wxl_mapper.h returns from that mapped branch before initialization or entry.
The source still emits gameCodeExecuted=false. Eight focused inventory and
hash-gate tests pass on resume. Valve's API overview and authentication guide
linked above were re-opened on 2026-10-26; no documented ticket-only substitute
for the running client/interface context was found.

The lane stops with BLOCKED-stm.md, rather than claiming a successful init slice.
The inherited brief's lane/upmp name is a copy error; commits remain on lane/stm.
The external BLOCKED-upmp.md retains that other lane's report and links this
independent Steam blocker. LANE_DONE is intentionally absent.

Fresh unchanged full smoke exits 0: **9 passed, 0 failed**, including actual
Xbox test-PE TLS/unwind checks. Evidence: /workspace/x64-run/runs/stm/smoke-final.txt
and smoke-final/. This regression check does not change failed Steam/Play
acceptance. No completion marker was created.

### stm — prerequisite correction, 2026-10-27

Cross-lane evidence now supersedes the historical mapping-only execution blocker:
lane/prun reports actual Xbox Play on launcher 0.5.157.4 executing guest entry,
TLS/DllMain and Unity CreateWindowExW. Its next failure is
`gdi32.dll!GetDeviceCaps` at UnityPlayer+0x7389dd. Evidence:
/workspace/x64-run/runs/prun/play-owner-integrated/ and lane/prun LANE_NOTES.md.
This was reviewed as another lane's evidence, not rerun by stm. No Steam DLL
code ran in that test. The current stm source remains the older mapping probe.

The independent Steam blocker remains: no isolated Xbox guest child-process
seam, authenticated genuine client context or working original API callbacks. Original Valve init
has only failure evidence. Four inventory tests and four pinned-client validator
tests pass again. Prior stm smoke remains 9/9; no fresh console test is claimed.
No runtime, Valve binary, account session or test gate changed. Acceptance stays
incomplete; LANE_DONE stays absent. Stop under the explicit blocker instruction.
