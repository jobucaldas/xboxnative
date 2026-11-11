# Real Steam online play: what GameNative does, and what an owned game can get on Xbox

Issue #55's owner request: *"steamworks should let me play online for real; how
does GameNative do this? I want to actually have normal access to steam."*

Today a Play-launched game's Steamworks is answered by the in-memory gbe_fork
provider (the ownership rule): achievements, cloud, presence and leaderboards are
bridged from the launcher's own signed-in CM session, but lobbies, matchmaking,
P2P/relay networking, auth tickets to game servers, friends/invites, inventory
and workshop are offline. This document answers three questions with evidence:
how GameNative gets real Steam, which routes exist for an Xbox UWP app whose
games run in-process, and which route this project should take — plus the slice
of it that is implemented and verified off-device.

Nothing here claims online play works. No console run has happened for any part
of it, and the checks the owner can make are listed at the end.

## 1. How GameNative actually does it

Read-only review of GameNative at
[`dc24544101ef53543614041d7a8d8af68738090a`](https://github.com/utkarshdalal/GameNative/commit/dc24544101ef53543614041d7a8d8af68738090a)
(the revision [GAMENATIVE-STEAM.md](GAMENATIVE-STEAM.md) already pins), plus the
pinned Valve client package this repository fetches
(`steam_client_win64.vdf` → `bins_win64.zip`). GameNative is an Android app
running Windows games under Wine/Box64, not an Xbox app, so its process model is
different from Play's — but its Steam modes are the useful evidence.

### Three Steam modes, and only one of them is "real Steamworks"

| Mode | What runs | Game's Steamworks answered by |
|---|---|---|
| `isLaunchRealSteam` | the real `steam.exe` GUI in the Wine prefix, `-silent -vgui -tcp -applaunch` | Valve's own client, in a separate process |
| `isLaunchHeadlessSteam` | a withheld `steamhost-*.tzst` bundle: `steam.exe` + `steamhost64.exe` | Valve's own client, headless, in separate processes |
| `isLaunchBionicSteam` | a native Android `libsteamclient.so` loaded into the **app** process, plus Proton's `lsteamclient.dll` in the Wine subprocess | Valve's own client engine, in the app process, over IPC |

Everything GameNative does for **login, library, downloads, cloud and
achievements** is its own JavaSteam/SteamKit client — that is not the game's
Steamworks and is not what makes a game's lobbies work. The game's Steamworks is
answered by Valve's client in all three modes. There is no fourth mode where an
emulator serves the game and online play works.

### The public half

- **Token logon into the client's own account store.**
  [`SteamTokenLogin.kt`](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/utils/SteamTokenLogin.kt)
  writes the refresh token into `config.vdf` under
  `Software\Valve\Steam\ConnectCache`, obfuscated with the client's `MTBF`
  scheme ([`SteamTokenHelper.kt`](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/utils/SteamTokenHelper.kt):
  `0x02 0x00 0x00 0x00`, then per 4-byte block `obf[k2 % 17] ^ k1 ^ block` with
  `k1 += 0x25fe6761`, `k2 += 1`, then a checksum block). For the newer client it
  also writes `%LOCALAPPDATA%\Steam\local.vdf` through a helper
  (`steam-token.exe`) that does what `CryptProtectData` does on Windows. This is
  the piece that makes the real client log on without a password prompt.
- **The bionic IPC endpoints.** `BionicProgramLauncherComponent.addRealSteamEnvVars`
  sets `WINESTEAMCLIENTPATH64` to the native `steamclient.so`,
  `_STEAM_SETENV_MANAGER=1`, `STEAM_BASE_FOLDER`, and — the important part —
  `Steam3Master=127.0.0.1:57343` and `SteamClientService=127.0.0.1:57344`, so the
  Wine-side `lsteamclient.dll` and the native client in the app process find each
  other on loopback.
- **Orchestration.** [`SteamBootstrap.kt`](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/SteamBootstrap.kt)
  spawns `libsteambootstrap.so` as a child process with `SB_ACCOUNT`,
  `SB_REFRESH_TOKEN`, `SB_STEAMID64`, `Steam3Master`, `SteamClientService` and a
  ready file, then polls the ready file for `INIT`/`READY`/`FAILED:`/`CLOSED`.
  [`SteamHostStatusWatch.kt`](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/ui/screen/xserver/SteamHostStatusWatch.kt)
  mirrors a `steamhost_status` word file (`starting`/`logon`/`connecting`/
  `retry`/`launching`/`launched`/`failed`) onto the boot splash.
- **A refresh-token seal.** `SteamHostAuth.kt` (FNV-1a 64 of the package name,
  then an xorshift-multiply stream) seals the token for the host binary. Public,
  and directly reusable in shape.
- **The "steampipe" asset is gbe_fork, not Valve.** `app/src/main/assets/steampipe/steam_api64.dll`
  (7,305,128 bytes, sha256 `513430ac8b869b0eed258d9e5feba5176a35446b3b61a63d01ab1d87683cdd1b`)
  contains `D:\a\gbe_fork\...` build paths and gbe's own `steam_settings`
  strings. It is the emulator, shipped for the modes where a game needs a
  `steam_api64.dll` next to it; it is not what makes online play work.
- **`SteamPipeServer.java` is a stub.** The public
  `com.winlator.steampipeserver.SteamPipeServer` (port 34865) answers fixed
  success for `MSG_INIT`/`MSG_IS_RUNNING` and does nothing for
  `MSG_RUN_CALLBACKS`. `SteamClientComponent` that would host it has its request
  handling commented out. It is not the real path and this project must not copy
  it (GAMENATIVE-STEAM.md already excludes Pluvia's version of the same idea).
- **Its fallback mode strips SteamStub from the game on disk.** When neither
  `isLaunchRealSteam` nor `isLaunchBionicSteam` is set,
  `XServerScreen.kt` writes `z:\Steamless\Steamless.CLI.exe "<game>.exe"` into a
  batch file and runs it under Wine before launching
  (`Skipping Steamless (launchRealSteam=…, launchBionicSteam=…)` is the line
  that says it did not). That is a DRM-stripping tool writing a modified game
  executable. the contributing rules forbid exactly this ("no Steamless or cracks"),
  so this is the one GameNative technique this project must not adopt even as a
  fallback — which is why the SteamStub path here serves an emulated
  `steamclient64.dll` in memory instead (`steam_client_package.h`,
  `play_runtime.h` `redirectExports`).

### The withheld half

`THIRD_PARTY_NOTICES` in the same revision states that `libsteambootstrap.so`
(the host that loads the native client, logs on with the refresh token and
creates the pipe) and the `steamhost-*.tzst` bundles (Valve's client plus a
headless `steamhost64.exe`) are proprietary with sources intentionally absent.
The public Kotlin tells you the *shape* of the handoff — env names, ready-file
states, the token seal — and nothing about how the host drives the engine, how
it answers the client's bootstrapper probes, or how it keeps the account's
session alive. That is the part that has to be rebuilt from the client's own
behaviour, and section 2 measures how much of it there is.

## 2. What Valve's client needs to come up in-process (measured)

Play runs the game inside the launcher process and refuses to spawn guests
(`CreateProcessW`/`ShellExecute` answer `ERROR_NOT_SUPPORTED`), so GameNative's
"run `steam.exe` next to the game" is not available. The remaining question is
whether Valve's signed `steamclient64.dll` can be the engine *inside* the
launcher process. Off-device, under Wine 11.0, with the pinned client package:

| Step | Result |
|---|---|
| `LoadLibrary("steamclient64.dll")` | loads (`tier0_s64.dll`, `vstdlib_s64.dll` beside it) |
| `CreateInterface("SteamClient021", &r)` | returns an object, `r = 0` |
| `Steam_CreateSteamPipe()` alone | **0**, `GetLastError = 2` (`ERROR_FILE_NOT_FOUND`); a `+file,+reg,+pipe` trace shows no named-pipe or shared-memory attempt at all |
| `Steam_CreateGlobalUser(&hUser)` first | **1** (`hUser = 1`) |
| `Steam_CreateSteamPipe()` after that | **2** (`hPipe = 2`) |
| `Steam_ConnectToGlobalUser(hPipe)` | **0** on every one of 12 attempts over 24 s |
| client's own `logs/connection_log.txt` | `Client version: no bootstrapper found`, then connectivity tests to `steamconnecttest.com` — no CM dial |
| after the host module exports `GetBootstrapperVersion` | `Client version: 5368717346`, and `SteamBootstrapper_GetEUniverse` is called — still no CM dial, still `ConnectToGlobalUser = 0` |

Three facts follow, and they are the whole feasibility answer for option (a):

1. **The client is an engine that can be hosted, but only in the right order.**
   `CreateSteamPipe` before `CreateGlobalUser` fails at once; `CreateGlobalUser`
   first yields a user and a pipe. The exported `Steam_*` entry points are the
   same ones `steam.exe` calls, and the client carries its own CM engine
   (`CCMInterface::YieldingConnect -- calling ISteamDirectory/GetCMListForConnect`),
   `CSingleProcessPipe` and `CSharedMemStream` ("IPC client is in my process,
   could/should be using an in-process pipe").
2. **The client probes the host module for a bootstrapper surface.** With no
   `GetBootstrapperVersion` export it logs `no bootstrapper found`; with one it
   reports a client version and calls `SteamBootstrapper_GetEUniverse`. The
   names are in its own string table (`GetBootstrapperVersion`,
   `SteamBootstrapper_GetEUniverse`, `SteamBootstrapper_GetLoggingDir`). A host
   must provide them.
3. **A connected global user requires a logged-on account, and that is the gate
   that did not move.** `ConnectToGlobalUser` answers 0 because the engine has
   no session. The client reads its account from `config.vdf`
   (`Software\Valve\Steam\ConnectCache`, `MTBF`) and `local.vdf` — the files
   GameNative fills with the refresh token and a `CryptProtectData` blob. The
   probe used a bogus `ConnectCache` entry, so it could not have logged on; what
   it does establish is that nothing short of a real account store moves the
   client past `ConnectToGlobalUser`, and that the CM dial is behind that gate,
   not behind pipe names.

So option (a) is not "map the DLL and call `SteamAPI_Init`". It is: host the
engine in-process, satisfy its bootstrapper probes, put the account's refresh
token into the client's own store in the client's own format, let it log on to
the CM, and then serve the game's `steam_api64.dll` through the same pipe
`steam.exe` would have. Every one of those steps is reproducible in principle,
and none of them has been done here.

## 3. The options for an Xbox UWP app

### (a) Host Valve's real client in the launcher process

**What it buys.** Every Steamworks interface at once, including the ones no
bridge can realistically reproduce: `ISteamNetworkingSockets` over SDR,
`ISteamMatchmaking`, friends/invites, inventory, workshop, game-server auth
tickets — because the game would be talking to the same client a PC player
talks to.

**Feasibility.** Architecturally plausible (section 2), with four unknowns that
each need console work:

| Unknown | Why it is hard | Evidence today |
|---|---|---|
| Account store | the client wants `ConnectCache` in its obfuscation and `local.vdf` in `CryptProtectData`; the launcher has the refresh token and would have to write both in the client's format, or let the client log on with a token it accepts | the token obfuscation is public (GameNative's `SteamTokenHelper`); `CryptProtectData` is available in the UWP process; neither has been tried here |
| Bootstrapper surface | the client resolves `GetBootstrapperVersion` and friends from the host module and refuses to dial a CM without them | measured: exporting `GetBootstrapperVersion` changes the client's log line; the full surface and its contract are unknown |
| Client version / ABI | the client is updated by Valve; a pinned package goes stale, and `steam_api64.dll` in the game must match the client's IPC version | the pinned package is fetched by `steam_client_package.h` and verified by signature; nothing pins the game-side ABI |
| Steam service | the client starts `SteamService.exe` for elevated work (`[AppID %u] Failed to start SteamService.exe with "%s"`) and expects `SteamClientService`; a UWP app cannot spawn it | not attempted |

**ToS / policy.** Valve's Steam Subscriber Agreement licenses the client for use
on a personal computer; an Xbox console in Dev Mode is not one, and the client
would be running outside its supported platform. This project's ownership rule
permits *running* the genuine client — nothing is patched, stripped or cracked,
and the games are owned — but that rule is the repository's, not Valve's. The
honest position is that (a) carries a real account-risk question the owner has
to decide, and that shipping it to users would be a policy decision, not a
technical one. It also does not bypass any Xbox security or Dev-Mode
restriction, which rule 3 forbids and which nothing here does.

**Effort.** Large and open-ended: the four unknowns above, plus the console's
AppContainer networking rules for the client's UDP/TCP/WebSocket CM transports,
plus a client update story. Weeks, with a real chance of a wall.

**Risk.** Highest of the three: an account ban is a plausible outcome of an
unsupported client on an unsupported platform, and the failure modes (silent
logon refusal, version mismatch) are hard to debug without Valve's logs.

### (b) Extend the launcher's CM bridges, service by service

**What it buys.** Real Steam data for the specific interfaces a game uses,
delivered through the provider the game already talks to. Presence and
leaderboards are already done this way (#112 and the branch before it).

**Feasibility, per family.** The launcher holds the account's CM session, so the
protocol side is a matter of message shapes; the hard part is delivery, because
the provider owns the callback queue the game reads (`LobbyCreated_t`,
`LobbyEnter_t`, `LobbyDataUpdate_t`, `LobbyChatUpdate_t`, `P2PSessionRequest_t`).

| Family | CM side | Delivery side | Verdict |
|---|---|---|---|
| Lobbies (`ISteamMatchmaking`) | 14 `CMsgClientMMS*` messages, request/response plus pushed forms — mapped and implemented this branch | needs the provider's callback queue or ownership of the interface | **medium**; the wire layer is done, delivery is the open gate |
| Friends / invites | `CMsgClientMMSInviteToLobby` (6621), persona state, `GameLobbyJoinRequested_t` | same queue problem | medium |
| Auth tickets | `CMsgClientRegisterAuthTicketWithCM` (5502), `CMsgClientAuthList` (5431/5432) | gbe signs its own tickets with a fake key, so its ticket is not a Valve ticket and CM will not accept it; a real ticket means the launcher owns `GetAuthSessionTicket` | medium, and only in the "launcher owns the interface" direction |
| P2P (`ISteamNetworking`) | `CMsgClientP2PConnectionInfo` (5434) rendezvous | the relay is Valve's; the game expects the API to do the transport | large |
| SDR (`ISteamNetworkingSockets`) | certificates (`CMsgClientNetworkingCertRequest`, 5621), relay tickets, POP selection | a full transport: encryption, relay auth tickets, ping/pop | **very large**; gbe has no SDR protos compiled in, so this is a from-scratch transport |
| Inventory / workshop | GC services, published Web API | provider reads files | workshop is reachable via the Web API; inventory is a GC service |

**ToS / policy.** Clean: this is the launcher's own signed-in session doing what
a client does, on the account's own behalf, for games the account owns. No Valve
file is touched, nothing is emulated to Valve, and the provider stays the
in-memory emulator it already is (rule 3's "substitution happens in memory").

**Effort.** Lobbies are the best value: they are the one family where the CM
protocol is small, well documented in the published protos, and the game's
expectations are narrow. SDR is the opposite — it is a transport, not a bridge.

**Risk.** The callback queue. A bridge that cannot deliver `*_t` structs is a
bridge the game never sees. Two ways out, both real work: hook the provider's
queue (needs internal knowledge gbe does not document), or own the interface
object and answer it (a per-interface replacement, which is a bigger change than
observing one export). The leaderboard bridge avoided this because
`UploadLeaderboardScore` is fire-and-forget and the read path goes through a
file the provider already reads.

### (c) Hybrid

Host the client for the transport-shaped interfaces (SDR, P2P, matchmaking
servers) and keep CM bridges for the data-shaped ones (presence, leaderboards,
cloud, achievements). This is what a mature solution looks like, and it inherits
(a)'s policy question for the parts it hosts. It is not a shortcut around the
callback-queue gate either: a hosted client serves the game through a pipe the
game's `steam_api64.dll` talks to, which means the game's own `steam_api64.dll`
has to be Valve's, not gbe's — a per-game decision, and a different staging path
from `stage_steam_provider` today.

### Recommendation

**Do (b), starting with lobbies, and treat (a) as a research track the owner
must approve on policy grounds before any console work.** Reasons:

1. It is the only route that is unambiguously inside rule 3 and Valve's terms:
   the launcher's own session, the account's own games, nothing patched.
2. It is incremental and each slice is verifiable: presence and leaderboards
   prove the pattern (observe the export, ask the CM, deliver through a surface
   the provider already reads).
3. The lobby wire layer is the part that can be built and tested off-device
   today; the delivery gate is a known, bounded problem (own
   `ISteamMatchmaking` for the game, or feed the provider's queue), not an
   unknown like the client's account store.
4. (a) is not ruled out — the measurements in section 2 make it a real option —
   but it needs an owner decision about running an unsupported client on the
   account, and it needs the account-store and bootstrapper work before it can
   even log on.

## 4. The slice implemented in this branch

Two portable headers with native tests, plus the CI registration:

| File | What it is |
|---|---|
| `src/xbox-test/app/steam_host.h` | the in-process client host: the `SteamClient021` → `CreateGlobalUser` → `CreateSteamPipe` → `ConnectToGlobalUser` sequence Valve's own launcher uses, the retry policy around the connect (it answers 0 until the engine has a session), pipe release on failure, and the bootstrapper-export check the client performs before it will dial a CM |
| `src/xbox-test/app/tests/steam_host_test.cpp` | drives it against a fake client that reproduces the measured behaviour (global user and pipe succeed, connect answers 0 until a session exists), and checks every failure stage names the call that refused |
| `src/xbox-test/app/steam_lobbies.h` | the CM side of `ISteamMatchmaking`: `CMsgClientMMS*` wire shapes for create/join/leave/list/data/chat/owner/server/invite/status, plus a bridge policy that tracks the lobby the game is in across pushed `LobbyData`, `UserJoinedLobby`/`UserLeftLobby`, chat and game-server messages |
| `src/xbox-test/app/tests/steam_lobbies_test.cpp` | a fake CM service answering in the real shapes: every request carries the app id, a refused create/join is reported as the game must see it, the bridge adopts the CM's member list and owner, a pushed message for another lobby is ignored, the owner leaving drops ownership until the CM says who has it, and transport failure is reported, never thrown |

`steam_host.h` is the option-(a) skeleton: it is the sequence, not a working
host. It does not load the DLL, provide the bootstrapper exports, write the
account store or dial a CM — those are the console-side work section 2 measures.
`steam_lobbies.h` is the option-(b) wire layer: it is what the launcher would
send and how it would keep its view of the lobby, not the delivery to the game.

What is deliberately **not** here: nothing in `launch.h` or
`play_runtime.h` changed, so no game is wired to either header yet, and the
provider still answers lobbies from its LAN network. Wiring the lobby bridge
means deciding the delivery gate first.

## What is verified, and what is not

Verified off-device:

- The host sequence and its failure reporting
  (`app/tests/steam_host_test.cpp`, run by CI): the measured behaviour of the
  real client — `CreateGlobalUser` then `CreateSteamPipe` succeed,
  `ConnectToGlobalUser` answers 0 until a session exists — is the fake the test
  drives, and every stage names the call that refused.
- The lobby wire shapes against a fake CM service
  (`app/tests/steam_lobbies_test.cpp`, run by CI): create/join/leave/list/data/
  chat/owner/server/invite/status round-trip in the published
  `CMsgClientMMS*` shapes, every request carries the routing app id, and the
  bridge policy behaves as described.
- The measurements in section 2 under Wine 11.0 with the pinned client package:
  `CreateSteamPipe` alone fails with `ERROR_FILE_NOT_FOUND`; `CreateGlobalUser`
  first yields a user and pipe; `ConnectToGlobalUser` stays 0; the client logs
  `no bootstrapper found` until the host exports `GetBootstrapperVersion`, after
  which it reports a client version and calls `SteamBootstrapper_GetEUniverse`.
- GameNative's public/withheld split at the pinned revision, and that its
  `assets/steampipe/steam_api64.dll` is gbe_fork.

Not verified:

- That Valve's client can log on from inside the launcher process at all. The
  account-store path (refresh token → `ConnectCache`/`local.vdf`) has not been
  written or tried; the probe deliberately used a bogus entry.
- That the client's bootstrapper contract is limited to the three names found in
  its string table. Exporting `GetBootstrapperVersion` changed its log line and
  produced one `GetEUniverse` call; what it does with the answers is unknown.
- Anything about the console: whether the UWP AppContainer lets the client open
  its CM transports, whether `CryptProtectData` behaves, whether the pinned
  client runs at all on the console's Wine-equivalent host.
- That Steam answers any `CMsgClientMMS*` message on the launcher's session, or
  that a game's lobby flow would accept the answers if it did. No lobby message
  has been sent from this codebase.
- That the callback-queue gate can be opened without replacing the provider's
  interface object.

## The console checks the owner can make

Nothing here claims online play works. When a build with a lobby bridge is on
the console, these are the checks, in order:

1. **The session answers at all.** Launch Megabonk (3405340) through Play and
   look for the launcher log line
   `lobbies: created lobby#<id> (<n>/<max>, type <t>)` after opening the game's
   host/co-op screen. If there is no line, no lobby message was sent.
2. **A friend can see the lobby.** With a second Steam account on a PC, open the
   game's server browser or the friend's Steam overlay: the lobby should appear
   in the game's lobby list, and the owner's Steam friends list should show
   "In-Game: Megabonk" with the lobby joinable. This is the first end-to-end
   evidence that the CM accepted the create.
3. **A friend can join.** The friend joins the lobby; the console log should
   show `lobbies: joined lobby#<id> (<n> members)` on the friend's side and a
   `LobbyChatUpdate` path on the console's side, and the game's own lobby screen
   should list the friend.
4. **Leaving is clean.** The friend leaves; the console's lobby view should drop
   the member, and the game's screen should follow.
5. **Nothing regressed.** Achievements, cloud, presence and leaderboards still
   behave as they do today (their log lines are unchanged).

If step 2 fails while step 1 succeeds, the CM accepted the message but the
game never saw the answer — that is the delivery gate, not the wire layer, and
it is the next thing to fix.
