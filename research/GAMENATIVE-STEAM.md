# GameNative / Pluvia Steam integration

## Latest directive and complete installed-binary check (2026-10-27 20:15)

João accepts the 5 GB Game-mode allowance. Memory-budget investigation and
measurement gates in older dated sections below are superseded; do not plan
memory work. Xbox remains the required location for the real client. Host Wine
remains only a scratch oracle. The supplied [64-bit status research](steam64-status.md)
is copied here with its obsolete off-console conclusion explicitly superseded.

Read-only `llvm-readobj --file-headers` inspection covered all 147 installed MZ
files with `.exe`, `.dll`, `.sys`, `.ocx` or `.cpl` extensions in the updated
scratch Steam directory. [Per-file machine types and SHA256 hashes](real-client-evidence/all-client-pe.json)
show 99 AMD64 and 48 i386 binaries. This is a local installation snapshot,
not a claim about every future Valve build or which helpers execute.
`steam.exe`, current `cef.win64/steamwebhelper.exe`, `steamclient64.dll`,
`steamerrorreporter64.exe`, `gameoverlayui64.exe`, `steam_monitor.exe`,
`steamsysinfo.exe` and `streaming_client.exe` are AMD64.
`SteamService.exe`, `WriteMiniDump.exe`, `drivers.exe`, `x86launcher.exe`,
unsuffixed overlay/error reporters and several 32-bit driver-query helpers are
i386. The complete inventory also retains legacy CEF variants. Presence does
not establish necessity: trace exercised helpers before requesting WoW64.

The process model remains the first gate. The reserved `entry_kernel.h`
CreateProcessW route still returns ERROR_ACCESS_DENIED. Concrete sequence:

1. Execution owner provides guest process isolation and a callable console seam;
   run the existing process, named-pipe and cancellation/reconnect fixtures there.
2. Complete internal Wine object IPC and lifetime handling without AF_UNIX or
   prohibited local listeners; trace actual outbound network requirements.
3. Launch genuine AMD64 Steam and CEF on Xbox; trace missing APIs and spawned
   helpers. Test ordinary service-free operation, without simulating service
   success. Add x86 support only for an observed indispensable helper.
4. Render genuine login on Xbox, authenticate once, display the owned library
   and download Megabonk 3405340. No supported reuse of launcher authentication
   is established; plan another genuine sign-in. Never inject or copy tokens.
5. Run the original steam_api64 in that guest namespace; prove initialization,
   ownership and callbacks through locked `play-test.sh 3405340` and unchanged
   smoke gates. No successful console Steam result is currently established.

## Current decision and required execution-owner input (2026-10-27)

The production design is the genuine Valve client running **on Xbox**, inside
launcher -> Wine -> uwph -> UWP Game mode. This is a design candidate, not an
established feasible implementation. No off-console client or game-push design
satisfies the requirement. The GameNative proprietary helper is excluded.

| Requested observation | Evidence and remaining gate |
| --- | --- |
| Genuine client starts | Host Wine login surface established; Xbox launch unproved. |
| Owned library and client download of 3405340 | Not tested after authentication. Requires genuine client sign-in. |
| Original Megabonk steam_api64 reaches a logged-in client | Pre-login probe fails; positive initialization, subscription and callback proof absent. |
| x64-only client path | Updated steam.exe, steamclient64.dll and steamwebhelper.exe are AMD64. Service-free login surface works on host; authenticated service-free operation remains unproved. |

Rechecked Valve's [retirement announcement](https://steamcommunity.com/groups/SteamClientBeta/discussions/3/3710433479207750727/):
`-no-browser` and `-noreactlogin` support ended; the announcement explicitly ties
the library UI to browser support. Keep CEF in the process implementation. Neither `-silent` nor the observed `-cef-disable-gpu` eliminates its process
requirements. Do not replace the client with steamclient64.dll alone.

**Specific missing integration input:** the prun/uwph execution owner must supply
a supported guest process-hosting implementation and callable Xbox test seam
for a parent executable plus child executable/arguments in one Wine namespace.
`xbox-test/uwph/host/entry_kernel.h` still returns `ERROR_ACCESS_DENIED` from
CreateProcessW. This lane is prohibited from changing the reserved runtime
execution routes. Changing that error to success would not provide isolation.
The existing process, named-pipe and lifecycle fixtures are the handoff contract;
more host fixtures cannot substitute for this missing implementation.

Concrete dependency order:

1. Execution owner supplies the seam and passes all three existing fixtures on
   Xbox, including identical image bases, isolated state and IPC completion.
   Any proposed hosting mechanism must use documented Xbox developer features.
2. Implement the failing process and Wine server operations, preserving handle
   ownership, cancellation and cleanup without AF_UNIX or high-port listeners.
   Distinguish local listener restrictions from outbound Steam connectivity.
3. Stage account-free genuine client files and run the updated AMD64 entry on
   Xbox. Trace exercised imports; test ordinary service-free operation before
   deciding whether an indispensable x86 component requires WoW64.
4. Render a fresh Valve QR on Xbox, let João authenticate there, show his owned
   library and download 3405340 through that client. No supported import of our
   launcher's existing login is established; plan an additional sign-in. Let
   the genuine client manage its own session; do not export or inject secrets.
5. Run the original Megabonk API in the same guest namespace; require successful
   init, logged-on ownership and callbacks. Then run locked Play and verify
   five-minute game stability.

Latest bounded Play transaction failed to acquire CONSOLE.lock (exit 99), before
any console access. Host log:
`/workspace/x64-run/runs/stm/console-client-readiness.txt`. No console Steam run,
fresh QR, authenticated result or smoke result follows from that attempt.
The existing QR is expired and must not be shown as usable. LANE_DONE is absent.

## Execution-owner milestone reconciliation (2026-10-27)

The latest `/workspace/wt/prun/LANE_NOTES.md` records genuine Xbox Play reaching
Unity window creation and stopping at `gdi32.dll!GetDeviceCaps`. It explicitly
claims no Steam/DRM execution. This is useful game-loader progress, but does not
provide the isolated guest child processes needed by Steam/CEF. This lane's
`entry_kernel.h` still rejects `CreateProcessW`. The process-fixture handoff
above remains required; no positive Steam result follows from that Play gate.
No additional console run was made for this documentation reconciliation.

## Additional executable IPC gate (2026-10-27)

`tools/steam-map/process-fixture/named_pipe.cpp` now supplies the next account-free
prerequisite: a child with handle inheritance disabled discovers a message-mode
named pipe by Windows path, sends a fixed request and receives a reply. The
parent uses overlapped operations, checks child exit 37, disconnects and closes
its handles. Host Wine passes; evidence is
`real-client-evidence/named-pipe-host.txt`. This is an original compatibility
fixture, not a replacement Steam API or a client implementation. No network
listeners, credentials or Valve binaries participate.

Concrete integration step: run this fixture after the address-isolation fixture
on Xbox, implementing named pipe lookup and overlapped I/O in the internal
Wine server namespace. Cancellation, reconnect, crashes and real Steam protocol
remain separate gates. Do not route these calls to unsupported desktop process
creation or declare Xbox compatibility from this host result. A fresh Play
attempt could not acquire CONSOLE.lock (exit 99); no console result was obtained.

## Current Xbox process prerequisite: executable proof

The production location remains **Xbox only**, inside launcher -> Wine -> uwph
-> UWP Game mode. Host Wine supplies observations, not a production Steam
session or a source of pushed games. The genuine updated x64 client remains
the primary design candidate; its feasibility on Xbox is not yet established.

Added an original, account-free fixture at
`tools/steam-map/process-fixture/process_ipc.cpp`. Two AMD64 executables use
the same preferred image address with ASLR disabled. Fresh disposable host
Wine passes image isolation, distinct PIDs, inherited mapping/event/pipe
handles, shared-memory updates, pipe peek/read, notification and parent wait
with child exit 37. Evidence: `real-client-evidence/process-fixture-host.txt`.
This tests anonymous inherited IPC only, not Steam's named-pipe discovery,
callbacks, Chromium, cancellation, memory limits or any console execution.

Two tempting host process APIs do not establish an Xbox solution:

- Microsoft's [FullTrustProcessLauncher requirements](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslauncher)
  specify Windows Desktop Extension SDK and `runFullTrust`. Adding that desktop
  extension to the Xbox manifest is not a supported route to Steam processes.
- Microsoft's [multi-instance UWP documentation](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/multi-instance-uwp)
  restricts multi-instancing to desktop targets. Its newer `uap10` attribute does
  not document an Xbox exception. A [Microsoft-hosted support response](https://learn.microsoft.com/en-gb/answers/questions/24162/uwp-unable-to-launch-apps-with-desktop4-supportsmu)
  also explicitly excludes Xbox. These are platform limits, separate from our
  current CreateProcessW denial; changing that return value cannot solve them.

Concrete next implementation sequence for the Wine/uwph execution owner:

1. Run this fixture through the console guest execution seam, preserving both
   image addresses and isolated writable globals. Supply a documented Xbox
   process hosting mechanism or a native guest isolation implementation that
   passes these checks. A thread plus relocated PE images is insufficient:
   per-process address-space, TLS, PEB, handles and exception state are required.
   This is presently an architectural prerequisite, not a solved small import.
2. Add named-pipe discovery/request/reply, disconnect/cancellation and shared
   object lifetime to the in-process Wine server. Keep transport internal;
   no AF_UNIX, desktop host process access or dependence on forbidden ports.
3. Stage the complete unmodified updated x64 Steam installation without scratch
   account/session files. Trace the first genuine process/import/IPC failure.
   Test the client's ordinary per-user fallback without SteamService before
   deciding whether its observed x86 service needs WoW64. Do not return fake
   service success or treat steamclient64.dll as a standalone logged-in client.
4. Render Valve's login on Xbox and authenticate there. No supported import of
   the launcher's existing CM login was found; plan another genuine sign-in.
   Prove library ownership, client-managed download of 3405340, then original
   Steam API init, subscription and callback pumping in that guest namespace.
5. Record UWP current/peak memory for client login/library/download and concurrent
   Megabonk startup/play, against 5120 MB. Host login PSS is only an observation.
   Budget CEF: retired `-no-browser` cannot eliminate it. Compare `-silent` and
   `-cef-disable-gpu` only using measured behaviour of the current client.
6. Run locked `play-test.sh 3405340` and unchanged smoke gates after integration.
   A fixture pass or a successful launch-attempt script exit is not Steam proof.

This resume's console attempt did not acquire CONSOLE.lock (exit 99). No fresh
Play result exists. Login QR remains expired; do not present the saved PNG as a
usable challenge. Owned-library/download and positive API observations remain
pending authentication, while Xbox process hosting requires implementation
independently of authentication. No LANE_DONE or completion claim.

Investigation: 2026-10-27. Scope: public source, unmodified Valve binaries,
owned Megabonk app 3405340. No reference code was executed, patched, posted,
or pushed. No account credentials or ticket values were read for this study.

## Source provenance and limits

Public repositories are fetched under `/workspace/ref/GameNative` and
`/workspace/ref/Pluvia`. Both already existed; `git fetch --depth=1 origin`
refreshed their public refs without changing their working files. Findings below
pin GameNative `dc24544101ef53543614041d7a8d8af68738090a` and Pluvia
`467a631f9006893597f1ef5f1aa9d4d3c5744874`. The newly fetched GameNative
`origin/master` was `eaa715c`; its tree also contains no bootstrap implementation.
Links below identify the studied commits, not a moving default branch.

**The public repository does not contain the complete real-client mechanism.**
GameNative's [THIRD_PARTY_NOTICES, Steam Client Bootstrap Shim and Launcher
Compatibility Helpers](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/THIRD_PARTY_NOTICES#L116)
explicitly says the bootstrap and Windows headless-host sources are withheld.
The Android shim is a proprietary ARM64 binary. Windows `steam.exe` /
`steamhost64.exe` are proprietary downloads, with package-specific restrictions.
The notice says they log the genuine client on using the user's entitlement;
that is a maintainer claim, not source verification of the hidden implementation.
The `.gitignore` excludes `app/src/main/cpp/steambootstrap/steam_bootstrap.c`.
Its CMake file names that absent source. Neither the Kotlin declarations nor
OpenSteamClient's older headers establish a current Windows token-logon ABI.
We must not treat the missing implementation as a working open-source bridge,
or remove its package restrictions to make the binary run here.

## Login and download are separate from runtime Steamworks

GameNative's [SteamService.kt](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/service/SteamService.kt#L3560)
uses JavaSteam. Credential login creates `AuthSessionDetails`, supplies the
authenticator for Steam Guard, starts `beginAuthSessionViaCredentials`, and waits
for the polling result. QR login uses `beginAuthSessionViaQR`, publishes the
changing challenge URL, and polls until the user approves. Both return account
name and access/refresh tokens; CM `SteamUser.logOn(LogOnDetails)` receives the
refresh token as `accessToken`. Saved refresh credentials restore that CM login.
The JavaSteam callback manager handles network callbacks and license/PICS data.
These callbacks are not the game's Steamworks callback queue.

Pluvia's [SteamService.kt, login and QR paths](https://github.com/oxters168/Pluvia/blob/467a631f9006893597f1ef5f1aa9d4d3c5744874/app/src/main/java/com/OxGames/Pluvia/service/SteamService.kt#L700)
uses the same JavaSteam authentication pattern. Some reference login/token utility
code contains sensitive logging or token-bearing command lines. Those patterns
must not be copied into this stack. This report names mechanisms only.

GameNative's [encrypted-ticket request](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/service/SteamService.kt#L5707)
calls `SteamApps.requestEncryptedAppTicket(appId)`, requires `EResult.OK`, and
caches the serialized encrypted-ticket protobuf for 30 minutes. Emulator-mode
settings receive a base64 representation. That is not proof of an authenticated
native Steam user/pipe, and does not make arbitrary Steamworks interfaces work.
An encrypted app ticket, depot key, manifest request code, and game auth-session
ticket are different objects; none is interchangeable with a live client context.

Pluvia's [downloadApp](https://github.com/oxters168/Pluvia/blob/467a631f9006893597f1ef5f1aa9d4d3c5744874/app/src/main/java/com/OxGames/Pluvia/service/SteamService.kt#L473)
delegates each selected depot to JavaSteam `ContentDownloader.downloadApp`, using
the logged-in client, branch, install and staging directories. The dependency's
manifest/chunk internals are not implemented in this repository.

Current studied GameNative uses [GameDownloadService.kt](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/service/download/GameDownloadService.kt#L539)
to resolve branch manifest IDs from PICS, request `getDepotDecryptionKey` for the
owning app (DLC/shared-depot owner first), require an accepted 32-byte key, and
obtain `SteamContent.getManifestRequestCode`. CDN server/auth-token lookups and
refresh callbacks feed its [NativeSteamDownload facade](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/service/download/NativeSteamDownload.kt#L34).
That facade passes the plan to JNI/native workers for CDN manifest and chunk
download, decryption, decompression, verification, resume and file output. It
describes a `.DepotDownloader` manifest store. The referenced Rust engine files
are absent from this local public tree; worker internals are not verified here.
Denied ownership/depot-key requests remain failures, not synthesized grants.

## What the Windows game sees

There are distinct modes. Calling them all a real Steam bridge would be wrong.

### Pluvia pipe replacement (excluded)

[SteamUtils.replaceSteamApi](https://github.com/oxters168/Pluvia/blob/467a631f9006893597f1ef5f1aa9d4d3c5744874/app/src/main/java/com/OxGames/Pluvia/utils/SteamUtils.kt#L53)
replaces the game's `steam_api.dll` / `steam_api64.dll` with assets from
`steampipe/`. [SteamClientComponent](https://github.com/oxters168/Pluvia/blob/467a631f9006893597f1ef5f1aa9d4d3c5744874/app/src/main/java/com/winlator/xenvironment/components/SteamClientComponent.java#L36)
starts [SteamPipeServer](https://github.com/oxters168/Pluvia/blob/467a631f9006893597f1ef5f1aa9d4d3c5744874/app/src/main/java/com/winlator/steampipeserver/SteamPipeServer.java#L77)
on TCP 34865. Its init and is-running responses are hard-coded 1; restart returns
0; register/unregister/run-callback cases do nothing. There is no demonstrated
Valve-client user context, ownership check, or callback implementation in that
server. We will not use it to claim successful Steam initialization.

### GameNative API replacement / coldclient (excluded)

[SteamUtils.kt, replacement and coldclient configuration](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/utils/SteamUtils.kt#L277)
backs up and replaces API DLLs, or extracts `experimental-drm-20260116.tzst` over
the Steam client tree. The latter supplies `steamclient{,64}.dll`,
`steamclient_loader_x{32,64}.exe` and optional injection DLLs. `ColdClientLoader.ini`
specifies executable, working directory, arguments, app ID, client DLL paths and
optional injection directory. `steam_settings` includes identity, language,
interface versions, achievements and encrypted-ticket settings. App IDs are also
written into `steam_appid.txt` beside the API DLL and in its settings directory
(`ensureSteamSettings`, around line 1263). GameNative's own workshop code explicitly
identifies the matching client/settings mode as `gbe_fork`.
These modes, Steamless/unpacking, and any synthetic init response are excluded.
Possessing a genuine encrypted app ticket does not change that exclusion.

### GameNative real Windows client (closest match)

[SteamUtils.restoreSteamApi](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/utils/SteamUtils.kt#L944)
restores original game API DLLs, executable and backed-up Steam client files.
It creates ACF app metadata, user configuration and controller layout. Its
`autoLoginUserChanges` writes HKCU `Software\Valve\Steam` `SteamPath`,
`InstallPath`, `SteamExe`, `AutoLoginUser`, plus `config/loginusers.vdf`.
`SteamTokenLogin` additionally prepares `config/config.vdf` ConnectCache for
token login; its sensitive diagnostics/serialization must not be replicated.

[XServerScreen.kt, headless launch](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/ui/screen/xserver/XServerScreen.kt#L5200)
sets `PROTON_DISABLE_LSTEAMCLIENT=1` and `PROTON_LIMIT_ADDRESS_SPACE=1`;
passes `STEAMHOST_ACCOUNT`, sealed `STEAMHOST_TOKEN`, `STEAMHOST_STEAMID64`,
`STEAMHOST_APPID`, `STEAMHOST_GAME_CMD`, `STEAMHOST_GAME_DIR`,
`STEAMHOST_INSTALL_DIR`, optional launch option, offline and input flags; then
runs `C:\Program Files (x86)\Steam\steam.exe` (the headless replacement host).
`SteamHostAuth.seal` uses the Android package name; it is obfuscation, not a
credential vault. We neither reproduce nor defeat the package restriction.

[extractSteamFiles](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/ui/screen/xserver/XServerScreen.kt#L6791)
extracts `steamhost-20261021.tzst`: Valve Windows client build 1788652215 plus
the custom host, preserving config/userdata/appcache/game files. The GUI option
instead launches Valve's `steam.exe -silent -vgui -tcp -nobigpicture
-nofriendsui -nochatui -nointro -applaunch <appid>`.
The **live Valve engine** must supply global-user attachment, client discovery,
IPC and callbacks. The headless host's exact exported-symbol calls, token
registration signatures, registry `ActiveProcess` publication, pipe creation,
game spawning and callback pump are not in public source. A status file saying
`launched` does not establish SteamAPI initialization in the game.

### GameNative Android/Bionic client (architecture mismatch)

[BionicProgramLauncherComponent.addRealSteamEnvVars](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/com/winlator/xenvironment/components/BionicProgramLauncherComponent.java#L824)
sets `WINESTEAMCLIENTPATH64` and `WINESTEAMCLIENTPATH` to Linux client libraries;
Proton `lsteamclient.dll` bridges Wine calls to the native client. The native
library is `usr/lib/libsteamclient.so` (Android ARM64), not a Windows x64 DLL.
Bootstrap settings are `_STEAM_SETENV_MANAGER=1`, `BREAKPAD_DUMP_LOCATION`,
`STEAM_BASE_FOLDER`, overlay-layer disable, `SteamOS=1`, `STEAMVIDEOTOKEN=1`,
`Steam3Master=127.0.0.1:57343`, `SteamClientService=127.0.0.1:57344`.
Wine gets `SteamUser`, `SteamAppUser`, `SteamClientLaunch=1`, `SteamEnv=1`,
`SteamPath`, `ValvePlatformMutex`, `STEAMID`, and matching `SteamAppId` /
`SteamGameId`. These values select an identity and endpoint; they confer no license.

[SteamBootstrap.kt](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/SteamBootstrap.kt#L119)
actually spawns `libsteambootstrap.so` as a separate process for the app, passes
client library and ready-file paths, and passes credentials in its environment.
It polls `INIT`, `READY`, `FAILED:*`, `CLOSED`. Some Java/JNI comments and the
third-party notice still describe the older in-process version; the Kotlin
implementation is stronger evidence for current process ownership. The launcher
requests app preparation/PICS/ticket prewarm to avoid subscription stalls.
The absent C source owns the actual login, native pipes and callback pumping.

`extractSteamFiles` also writes Wine HKCU `Software\Valve\Steam\ActiveProcess`
`ActiveUser`, `SteamClientDll`, `SteamClientDll64`, `Universe=Public` for Bionic.
This does not replace the engine. Windows Steam handles are process/client
contexts, not arbitrary integers that our launcher may invent.

## Mapping to Xbox Steam

| Mechanism | Our tree / required work |
|---|---|
| QR and persistent CM login | `xbox-test/app/main.cpp`: `begin_qr`, `poll_qr`, `cm_build_client_logon`, `user_session`; already real Steam authentication. Do not export stored tokens into research or test logs. |
| Owned library / depot download | `xbox-test/app/steamdl.h`: logged-in CM PICS/depot-key requests, manifest request code, HTTPS CDN, AES/decompression, chunk and final SHA1 verification. Already implemented. |
| App ticket | No equivalent native-client ownership context is established by current downloader/login. Add genuine requests only as required by the chosen client path. Never equate ticket retrieval with init success. |
| Original game API and calls | `tools/steam-map/megabonk-startup.json`, `research/steam-api-path/`: original Valve DLL; managed InitEx calls `SteamInternal_SteamAPI_Init` with 25 interface versions. 43 statically identified direct game calls include callbacks/stats/achievements; static calls are not a startup trace. |
| Valve Windows engine | `tools/steam-map/fetch.py`, `validate.py`, pinned build map; four unmodified Valve DLLs available outside Git. Existing gate is static only. New anonymous helper below begins runtime validation. |
| Native engine logon | Missing tested Windows token-registration/logon ABI and account-safe lifecycle. Public GameNative source deliberately omits it. Launcher CM login alone cannot fill this gap. |
| Client discovery | Prior original-DLL init returned `Could not determine Steam client install directory.` Need real Steam install/ActiveProcess data owned by a live authenticated client, not fake PID/user values. |
| Pipes and callbacks | No native Valve peer or game Steamworks callback pump in this lane. Preserve original API DLL callback registration/dispatch; headless engine needs its own verified pump. Pluvia callback no-ops are unusable. |
| Proton native bridge | ARM64/Bionic library cannot execute on x64 Xbox. Linux .so and Proton lsteamclient need unavailable Android/Linux host ABI. Prefer original Windows x64 client under Wine/uwph. |
| Endpoints / UWP | Do not copy 57344: console drops ports 57344+. AF_UNIX unavailable. Windows client IPC/named-object support must be exercised through Wine/uwph; changing a port string alone cannot supply it. |
| Play execution | prun owns `launch.h`/`wxl_mapper.h`. Its newer console evidence executes Unity; this older stm tree must not overwrite that runtime. Our helper remains separate until the real client lifecycle is proven. |

## Implementation and validation

`tools/steam-map/anonymous_host.cpp` is a new anonymous-only, client-backed
lifecycle experiment. It hashes all four Valve files in-process using BCrypt,
keeps read handles that deny replacement while loading, constrains DLL search,
checks the factory address and each called object's exact vtable/method RVA.
It attempts the pinned factory, `CreateGlobalUser`, `GetIClientUser`,
`BLoggedOn`, `RunFrame`, `Steam_BGetCallback` and `Steam_FreeLastCallback`.
The first lifecycle signatures are corroborated by the local
OpenSteamClient `IClientEngine.h` and the pinned binary map. Their runtime ABI
remains experimental until executed. No token or login method is exposed;
credential-revocation slots are never callable through this program. No fake
`steam_api64`, registry user, pipe handle, ownership response, or init result.

`run_anonymous.py` reuses the full existing static validator, copies only those
four DLLs into a private stage, compiles with warnings as errors, and runs with
a fresh isolated Wine prefix and an allowlisted child environment. A timeout
or crash is failure. Exit 0 means anonymous lifecycle only, **never successful
SteamAPI_Init**. Its lifecycle output explicitly retains `steamInitProven=false`.
With `--game-api`, it also pins the original game DLL, derives the exact 25
versions from the existing game inventory, publishes its actual PID/client path
in the disposable registry with `ActiveUser=0`, and invokes original native init.
The optional anonymous API experiment always returns failure (10), even if init
were accepted; no owned session exists. It never invents a positive user identity.

The existing `init_probe.cpp` now also resolves the original DLL's
`SteamAPI_SteamUser_v023`, `SteamAPI_ISteamUser_BLoggedOn`, `SteamAPI_SteamApps_v008`
and `SteamAPI_ISteamApps_BIsSubscribedApp`. After native init succeeds, it pumps
120 frames of genuine `SteamAPI_RunCallbacks`, checks online status and the app
subscription, then shuts down its own context. Exit 0 now requires all three:
native init 0, online user, and subscription to 3405340. This is a diagnostic
consumer of the original API, not a replacement DLL or an authenticated host.
No claim of a delivered game callback follows from merely calling the pump.
Valve documents [callback pumping and initialization](https://partner.steamgames.com/doc/api/steam_api),
[online user state](https://partner.steamgames.com/doc/api/ISteamUser#BLoggedOn), and
[app subscription checks](https://partner.steamgames.com/doc/api/ISteamApps#BIsSubscribedApp).

### Fresh results, 2026-10-27

- Off-device Wine 10, fresh disposable prefixes: anonymous helper exits **0**;
  all file/factory/object pins pass; actual global user and pipe creation succeeds;
  ten engine frames execute and five genuine native callbacks are retrieved/freed.
  `loggedOn=false`, as required. No account was used.
- Same live engine plus original owned game API: exits **10**, native init
  returns **3**, error **`No SteamUser023`**, `authenticated=false`. This gets past
  the prior install-directory error. It proves discovery/runtime progress, not a
  functioning owned-app session. Valve warns about missing `SDL3.dll`,
  `video64.dll`, and a driver-query executable; the four-file static lower bound
  is not a complete production client tree. Wine adapter enumeration also emits
  error 13. Process-exit worker-thread warnings remain visible in raw evidence.
- Native missing/non-Valve-file refusal: **2/2 pass** before loading Valve code.
  Existing game inventory tests **4/4** and full pinned-map tests **4/4** pass.
  MinGW builds pass `-Wall -Wextra -Werror`; no assertions were disabled.
- Actual Series S launcher **0.5.157.4**, one whole transaction under inherited
  `CONSOLE.lock` fd 9: unchanged `play-test.sh 3405340 120` exits **0** and records
  `gameCodeExecuted=true`. Unity stops at `gdi32.dll!GetDeviceCaps`,
  `UnityPlayer+0x7389dd`, before the Steam stage. This is a real Play test of the
  installed prun runtime, **not execution of our new off-device helper**. Synthetic
  one-shot input was labelled. No package or runtime source was changed.
- Fresh unchanged canonical `xbox-test/smoke-all.sh`: **9 passed, 0 failed**,
  including console host/TLS checks under its existing lock protocol.

Raw off-device evidence and selected console results are retained in
`research/gamenative-steam-evidence/`. Full Play screenshots/logs live at
`/workspace/x64-run/runs/stm/gamenative-play`; smoke transcript is
`/workspace/x64-run/runs/stm/gamenative-smoke.txt` with its own artifact-directory
path. These results do not prove Steam init on Xbox.

## Legitimate completion path

`tools/steam-map/run_owned.py` packages the owned-session oracle. After João
signs into the genuine Valve desktop client interactively and leaves it running
in an existing Wine prefix, run:

```sh
python3 tools/steam-map/run_owned.py --prefix /absolute/path/to/prefix \
  --game-api /workspace/steam-owned/games/megabonk/Megabonk_Data/Plugins/x86_64/steam_api64.dll
```

Supply paths only, never credentials. The runner hashes the original API before
staging it, builds the native probe with warnings as errors, and uses the exact
game interface list. It neither starts Steam nor changes its registry discovery
state, and it never calls `wineserver -k` on the supplied prefix. The child
environment excludes launcher tokens and portal credentials. Raw Wine/client
output is discarded; only fixed init/online/subscription fields are printed.
Exit 0 requires native init 0, logged-on user and app subscription. Pumping 120
callback frames does not prove receipt of a specific event. This remains an
off-device diagnostic, not a Play integration or standalone authenticated host.

Prefer an authenticated, unmodified Windows Valve client owning the game process,
original game API DLL, and real callback dispatch. A complete Valve-client install
and João's interactive sign-in in that same Wine prefix provide a test oracle
without transferring launcher tokens. The custom headless alternative requires
an independently validated bootstrap ABI (or source legitimately supplied by its
maintainers); do not reuse their restricted helper as a portable open component.

Once authenticated native context exists, use the original DLL probe with the
game's exact interface list, require native init result 0, then callback/stat
evidence. Integrate the proven helper into prun's Play lifecycle: initialize
before the Wine game load, retain the client while the game lives, publish only
real discovery state, pump callbacks, and shut down without token revocation.
Then run `flock /workspace/x64-run/CONSOLE.lock` around actual launcher Play and
unchanged smoke. Off-device tests cannot satisfy the console acceptance.

**Specific missing external input:** a usable Valve desktop-client installation
with João interactively signed in, in a known Wine prefix, for the original-DLL
owned-session probe. No such executable was found in the workspace search or
standard Wine/Steam home locations. The console launcher's CM session is real,
but is not that native client session. Alternatively, legitimately supplied
GameNative Windows bootstrap source/current login ABI would enable the headless
implementation without guessing adjacent credential-revocation methods. Public
notices expressly withhold that source. No password/token should be sent in chat
or copied into evidence. Pending this input, do not set `LANE_DONE`; authenticated
logon and Play integration remain unfinished.

## Superseding design: genuine client ON Xbox (João, 2026-10-27 12:09)

The production design is launcher -> Wine -> uwph -> genuine Valve client and
Megabonk, entirely inside the Xbox Game-mode application. The host Wine prefix
is only a disposable diagnostic oracle. A PC client and game pushing are not a
fallback design. Earlier requests for an existing host installation are superseded:
this lane must install the official client itself.

### Official installer observation

Fetched `https://cdn.akamai.steamstatic.com/client/installer/SteamSetup.exe` over
HTTPS into `/workspace/x64-run/runs/stm/real-client/SteamSetup.exe`.
SHA256: `7d3654531c32d941b8cae81c4137fc542172bfa9635f169cb392f245a0a12bcb`.
Read-only NSIS extraction shows `Steam.exe` and `bin/SteamService.exe` have PE
Machine `0x014c` (i386), as does the installer. This proves the bootstrap needs
x86 support; it does not establish the architecture of the self-updated client.
Valve's [32-bit OS support notice](https://help.steampowered.com/en/faqs/view/49A1-B944-48B8-FF00)
says 32-bit OS support ended January 1, 2026. That statement alone does not prove
that every current Windows client executable is x64. Inspect all downloaded PE
headers after update before selecting the Xbox bootstrap path.

Host has Wine 10 amd64 but lacks wine32. Initial official installer attempt in a
new prefix did not install Steam. Debian package retrieval failed on HTTP 500 and
then HTTPS TLS EOF; certificate and repository-signature verification were not
disabled. Package logs are under the external `real-client` evidence directory.
No QR login, owned library, download or authenticated SteamAPI_Init is evidenced
by these installer steps.

### Xbox implementation sequence and gates

1. Finish official installation/update in the scratch Wine prefix. Inventory
   executable architectures, file hashes, DLL imports, registry discovery,
   child-process tree and IPC endpoints before login. Keep private client state
   outside git/evidence. Capture only a visible login/QR screen at the requested
   `steam-login-qr.png` path; do not export authentication responses or tokens.
2. Prefer the updated genuine x64 client entry if Valve distributes one. Test it
   as a real application, not merely `LoadLibrary(steamclient64.dll)`.
   `steamclient64.dll` is an engine library; the previous anonymous lifecycle
   proves neither authenticated bootstrap nor a standalone client.
   A service binary is not a substitute for the client's login/library process.
   If indispensable PE32 bootstrap/runtime code remains, uwph needs actual WoW64
   guest execution with x86 ABI, ntdll and handle interoperability; the existing
   PE32 inspection mapper does not execute code. Coordinate this dependency with
   pe32; do not claim that x64 hardware alone solves AppContainer execution.
3. Replace the explicit guest `CreateProcessW` failure at
   `xbox-test/uwph/host/entry_kernel.h:19` through the Wine process model, not by
   invoking prohibited desktop host processes. Implement separate guest address
   spaces/module/TLS/exception state, process lifetime, inherited handles and
   environment. CEF renderer/GPU/helper children must work through this model.
   A thread calling another PE entry is not equivalent to a separate process.
4. Implement wineserver request/reply and handle transfer through an in-process
   broker compatible with UWP. AF_UNIX cannot be used. Preserve named-pipe,
   event, shared-memory and synchronization semantics needed by genuine Steam
   discovery and game IPC. Do not substitute TCP for a pipe without adapting
   both Wine endpoints. Reported usable low loopback ports are a possible
   transport only after a console capability probe; avoid ports >=57344 and do
   not depend on desktop loopback exemptions. Never expose authentication IPC
   to the network. Verify pipe security, cancellation and process teardown.
5. Bring the genuine login/library UI through Wine windowing and the console
   CoreWindow/compositor, including text/controller navigation and HTTPS with
   certificate checks. Use the client's own QR or credential flow. No supported
   mechanism has been established for importing the launcher's CM refresh token.
   Plan for one additional interactive Valve-client sign-in; whether it is
   strictly unavoidable is not proven. Let the client retain its own login in
   private LocalState; console sign-in persistence must be tested separately
   from scratch-prefix sign-in. Do not copy session material from the host.
6. Exercise normal owned-library display and client-managed install of 3405340
   before replacing the launcher's JavaSteam download path. Keep client and game
   in the same guest Wine prefix/process namespace. Let the client publish its
   real SteamPath/ActiveProcess state; verify original game API InitEx, logged-on
   SteamUser023, BIsSubscribedApp(3405340) and actual callbacks. No fabricated
   ownership, synthetic success, replacement DRM DLL or private login ABI.
7. Measure total Xbox app memory, including launcher, Wine/server, Steam/CEF,
   mapped game images and Unity/D3D resources. Record current and peak memory
   during login, library, download, game startup and a five-minute run against
   the 5120 MB limit. Host RSS is only comparative evidence. Refuse to call the
   design feasible until the combined console workload fits with measured
   headroom; avoid concurrent download and game launch if measurements require.
8. Evaluate `-cef-disable-gpu` and `-silent` one at a time with the shipped build.
   GPU disable is documented in [Valve support](https://help.steampowered.com/en/faqs/view/6C3E-9504-CC20-AF49),
   but software rendering may increase CPU use. `-silent` is not evidence that
   Chromium is absent. The [Valve Developer Community command-line page](https://developer.valvesoftware.com/wiki/Command_line_options#Steam)
   could not be fetched (403) during this run. Do not assume historical
   `-no-browser` works in the current client, supports QR/library, or preserves
   an authenticated game session. Test accepted flags, child processes, memory
   and Steam API behavior explicitly. Do not disable client security sandboxes
   merely to reduce memory.
9. Integrate only after the scratch oracle works, then prove the client itself
   runs on Xbox. Under CONSOLE.lock, run Play/play-test.sh 3405340, capture real
   native init/ownership/callback evidence and unchanged smoke checks. A Play
   result stopping in Unity before Steam is not Steam acceptance. Retain
   LANE_DONE absent until this gate or a specific external-input handoff.

Feasibility remains **unproven**, not ruled out by the old bootstrap alone.
The immediate work is installation and process/IPC discovery; the largest known
Xbox gaps are guest process isolation, browser windowing and wineserver/Steam
IPC. None is solved by the four-DLL anonymous engine experiment.


### Successful official installation/update (supersedes setup failure above)

Alternate signed Debian mirrors (`ftp.debian.org`, `security.debian.org`) succeeded;
wine32:i386 installed without bypassing TLS or repository verification. The
incomplete first prefix was left alone. A fresh `prefix-full` installed through
`wine SteamSetup.exe /S`, then genuine Steam updated itself normally. The update
log reports updater built October 21, 2026 and `steam_client_win64` selected.
Final PE inventory, after both update stages completed:

| Component | Machine | Consequence |
|---|---|---|
| steam.exe | AMD64 | Genuine modern client entry can avoid x86 bootstrap on Xbox |
| steamclient64.dll | AMD64 | Native engine, still needs client process/session |
| bin/cef/cef.win64/steamwebhelper.exe | AMD64 | Browser processes still require process support |
| bin/SteamService.exe | i386 | Test whether ordinary client/game operation needs service |

Pinned hashes and complete PE inventory: `research/real-client-evidence/client-pe.json`.
Intermediate inventories during extraction were mixed versions; only the final
post-restart inventory supports this conclusion. The bootstrap installer remains
x86; install/update on scratch is diagnostic, not a PC-client production design.
For Xbox, prefer staging the genuine complete x64 installation and testing its
normal update/restart lifecycle; do not alter Valve files or assume service
functionality is optional before observing library/install/game behavior.

The genuine login window rendered under Wine with `-cef-disable-gpu`. Its first
capture showed a blurred QR with spinner, so it was not published as a scannable
QR. No authentication, owned library or game initialization is established yet.

The QR finished loading and was saved to the requested external path
`/workspace/x64-run/runs/stm/steam-login-qr.png` (0600; not committed). Asked João
to scan using his mobile Steam app. Some Wine UI text is blank; the QR itself
is visible. Before authentication, the original API probe against this same
running prefix returns native result 1, not logged on/not subscribed and
`steamInitProven=false`. Owned library/download/API success require the user's
interactive authentication next. Do not interpret a login screen as success.

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

## Genuine client service-free scratch experiment (2026-10-27)

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

## Executable socket-free lifecycle gate

Added original `tools/steam-map/process-fixture/pipe_lifecycle.cpp`. A disposable
host Wine prefix passes connect cancellation, read cancellation, server reuse,
reconnect after disconnect and peer closure during an outstanding read. Evidence:
`research/real-client-evidence/pipe-lifecycle-host.txt`. Build uses warnings as
errors. This single-process test complements, rather than replaces, the separate
cross-process isolation and discovery tests. It neither implements uwph IPC nor
proves termination cleanup, Steam traffic or Xbox compatibility.

Concrete next integration step: execute all three fixtures through the console
Wine loader, then implement failing named-object operations in the in-process
server with completion/error and handle-lifetime semantics preserved. Do not
replace pending completion with false success. Actual guest process isolation is
still prerequisite to Steam/CEF, and belongs to the execution owner. The lane
must not edit the reserved launch.h/wxl_mapper.h runtime routes.
