# Discord in the games Play runs (issue #54)

What the owned games do with Discord, what a UWP app on Xbox can legitimately do about it, and what Play does
today. Milestone 1 (games run, presence is simply absent) is implemented and tested off-device. Milestone 2
(forwarding Rich Presence to the owner's Discord account) is designed here and **not** implemented, because the
research found no route Play can take on its own; see [Milestone 2](#milestone-2-rich-presence-to-the-owners-account).

## What the owned games ship

Scanned `/home/jobu/steam-owned/games/*` for Discord files, Discord strings in every `.dll`/`.exe`, and the
imports/exports of what turned up. Only two titles use Discord at all.

| Game | Discord file | Flavour |
|---|---|---|
| Megabonk | `Megabonk_Data/Plugins/x86_64/discord_game_sdk.dll` | Game SDK (C ABI) |
| Slime Rancher | none installed; `Managed/Assembly-CSharp.dll` P/Invokes it | legacy Rich Presence C API (`discord-rpc`) |
| Halo Wars DE, Fields of Mistria, MMLC, Puyo Puyo Tetris 2, Say No! More | none | — |

**Megabonk — Game SDK.** `discord_game_sdk.dll` exports `DiscordCreate`, `DiscordVersion` and
`rust_eh_personality` (it is the Rust build). It imports exactly two shell32 names, `SHELL32!ShellExecuteExW` and
`SHELL32!SHGetKnownFolderPath`, plus kernel32/advapi32/ole32/ws2_32/ktmw32, and carries the strings
`\\?\pipe\discord-ipc-` and `discord:///library/<client_id>/launch`. `GameAssembly.dll` (the IL2CPP output) also
mentions Discord, so the game's own code drives it.

**Slime Rancher — legacy Rich Presence.** No `discord-rpc.dll` is installed next to the game, but
`Assembly-CSharp.dll` contains the P/Invoke surface for it: `Discord_Initialize`, `Discord_RunCallbacks`,
`Discord_UpdatePresence`, `Discord_ClearPresence`, `Discord_Respond`, `Discord_UpdateHandlers`,
`Discord_Shutdown`, the `discord-rpc` library name, and the `DiscordRpc+OnReady/OnError/OnDisconnected/OnJoin/OnRequest/OnSpectate`
callback types. With no DLL present the P/Invoke throws and the game carries on without presence, as it already
does under Play.

## How both SDKs reach a Discord client

Neither SDK talks to Discord over the network for presence. Both connect to the **local Discord desktop client**
over a named pipe and ask it to set the activity:

- Discord RPC docs, IPC transport: `\\?\pipe\discord-ipc-{n}` on Windows
  ([RPC](https://docs.discord.com/developers/topics/rpc)). The same page: "All Discord clients have an RPC server
  running on localhost that allows control over local Discord clients" and "Discord's RPC server supports IPC
  (Inter-Process Communication) as transport for native applications and games."
- `discord-rpc`'s `BaseConnection::Open` builds `L"\\\\?\\pipe\\discord-ipc-0"`, calls `CreateFileW` with
  `GENERIC_READ|GENERIC_WRITE`, `OPEN_EXISTING` and no sharing, and on `ERROR_FILE_NOT_FOUND` advances the digit
  up to `discord-ipc-9`; `ERROR_PIPE_BUSY` waits 10 s on `WaitNamedPipeW` and retries
  ([connection_win.cpp](https://github.com/discord/discord-rpc/blob/master/src/connection_win.cpp)).
- When no client is running, `Discord_Initialize` does **not** call the error callback: it starts the I/O thread,
  which retries with backoff, and `Discord_RunCallbacks` returns immediately while the connection is null
  ([discord_rpc.cpp](https://github.com/discord/discord-rpc/blob/master/src/discord_rpc.cpp)). That is the
  "Discord is not running" behaviour a game already tolerates on desktop.
- The Game SDK's `DiscordCreate` returns the `InternalError` result (4) with no device object when it cannot reach
  a client; the result enum is `0 Ok`, `1 ServiceUnavailable`, `4 InternalError`, `27 NotRunning`
  ([Game SDK](https://docs.discord.com/developers/developer-tools/game-sdk)). The Game SDK is archived upstream —
  "The Game SDK has been archived" — and Discord recommends the Social SDK for new projects.

## What a UWP app on Xbox can legitimately do

| Route | What the sources say | Usable here? |
|---|---|---|
| Local RPC over `discord-ipc-N` | Needs the Discord desktop client on the same machine. A console has none, and packaged apps may only name pipes under `\\.\pipe\LOCAL\` ([UWP IPC](https://learn.microsoft.com/en-us/windows/uwp/communication/interprocess-communication): "Named pipes (in packaged *and* unpackaged apps) must use the syntax `\\.\pipe\LOCAL\` for the pipe name"). | No client to connect to. |
| OAuth2 + REST Rich Presence | Discord documents the authorization code, implicit, client credentials, bot and webhook grants — **no device-authorization grant** in the general OAuth2 page — and there is no REST endpoint that sets a user's own activity ([OAuth2](https://docs.discord.com/developers/topics/oauth2), [User resource](https://docs.discord.com/developers/resources/user)). `rpc` and `rpc.activities.write` are "only available to approved partners". | No. |
| Discord Social SDK on console | Documented and supported: Xbox Series X\|S on GDK 241000–260400 is **Generally Available** ([platform compatibility](https://docs.discord.com/developers/discord-social-sdk/core-concepts/platform-compatibility)). Account linking uses the **device authorization flow**: the game requests a device code, the player scans a QR code or enters a user code at `discord.com/activate`, and the game polls `/oauth2/token`; endpoints are `POST /api/v10/oauth2/device/authorize` and `/oauth2/token` ([account linking on consoles](https://docs.discord.com/developers/discord-social-sdk/development-guides/account-linking-on-consoles)). Presence scopes are `openid` + `sdk.social_layer_presence` ([OAuth2 scopes](https://docs.discord.com/developers/discord-social-sdk/core-concepts/oauth2-scopes)). | Only for a game that itself ships the Social SDK, and only with "middleware approval" and "an approved developer for the target console". |
| Xbox ↔ Discord account linking in the Xbox shell | Discord's own Xbox integration links a Discord account through Discord's *User Settings > Connections* and lets the player join a Discord voice call from the console's *Parties & Chats* ([Discord blog](https://discord.com/blog/xbox-voice-integration-announcement)). It is voice, it belongs to the Xbox shell, and no public API for third-party apps to read or set that presence is documented. | No. |

Nothing above requires or permits scraping tokens, patching Discord or Valve binaries, or bypassing Xbox
restrictions, and none of it is done here.

## Milestone 1: games run, presence is simply absent (implemented)

A Discord-SDK game must start and play on the console with no Discord client to reach. Play already had the two
pieces that make that true; this change makes the outcome observable and proves it off-device.

**The pipe answers with the error the SDK handles.** Play's kernel32 route maps a guest's `\\.\pipe\<name>` to
`\\.\pipe\LOCAL\<name>` (the AppContainer namespace). Both SDKs build the *extended* form `\\?\pipe\discord-ipc-N`,
which names the same object and was not mapped, so the connect went to the wrong namespace. `local_pipe()`
(`src/xbox-test/uwph/kernel32/pipe_route.h`) now maps `\\?\pipe\<name>` the same way. With no server, the connect
returns `ERROR_FILE_NOT_FOUND` (2) — the error `BaseConnection::Open` advances past and `DiscordCreate` turns into
`InternalError`, exactly as on a desktop where Discord is not running.

**Nothing blocks.** The Game SDK, when the pipe is missing, tries to launch `discord:///library/<client_id>/launch`
through `ShellExecuteExW`. Play refuses `ShellExecute*` (`play_runtime.h` `shellLaunch`: `FALSE`,
`ERROR_NOT_SUPPORTED`, `hInstApp=5`) and answers `SHGetKnownFolderPath` from the system shell32. Measured against
the real `discord_game_sdk.dll` under Wine: with Wine's own shell32 `DiscordCreate` did not return within 60 s;
with `ShellExecuteExW` refused and the known folder answered as Play does, it returned `result=4` with the device
untouched in 0–5 ms.

**One log line per game.** `play_runtime.h` recognises a Discord SDK by module name (`discord_game_sdk.dll`,
`discord-rpc.dll`, …) or, for a renamed or Unity copy, by its exports (`DiscordCreate` or `Discord_Initialize`),
and emits once:

```
PRUN EVENT Discord SDK present (discord_game_sdk.dll); no Discord client on this console, so the game runs without Rich Presence
```

### Evidence

- `src/xbox-test/uwph/kernel32/test.sh pipe` → `kernel32 pipes: PASS (35 checks; off-device, LOCAL\ pipe namespace)`
  (three new checks for the `\\?\pipe\` form).
- `src/xbox-test/app/tests/play-runtime.sh` mode `discord-sdk` → `PLAY-TEST executed=1 finished=1 success=1 code=42`.
  The guest (`tests/play-discord.cpp`) loads our own stand-in `tests/play-discord-sdk.cpp` — never Discord's binary —
  which reproduces the `discord-ipc-0..9` walk of both SDKs and exports both APIs. It exits 42 only if
  `DiscordCreate` returned 4 with a null device, the pipe connect saw `ERROR_FILE_NOT_FOUND`, and the legacy
  initialize/run/presence/shutdown calls all returned; the shell then greps the log for the single line above.
- Console, earlier launcher run (`/home/jobu/steam-owned/games/megabonk/vis-player.log`): `mapping
  discord_game_sdk.dll`, `DllMain PROCESS_ATTACH discord_game_sdk.dll` — the real SDK mapped and attached without
  crashing. That log predates the log line, so the console check below is still needed.

### Console check

Launch Megabonk through the launcher and read the player log (`src/xbox-test/tools/play-test.sh 3405340`, then the
`vis-player.log` it produces). Expect the game to reach its menu as before and exactly one line:

```
PRUN EVENT Discord SDK present (discord_game_sdk.dll); no Discord client on this console, so the game runs without Rich Presence
```

## Milestone 2: Rich Presence to the owner's account (design only)

**Why this is not implemented.** The only documented way to put a game's activity on a Discord account from a
console is the Social SDK's device-authorization flow, and it is available to a game that links the Social SDK and
whose developer has "request middleware approval" and is "an approved developer for the target console". Play is
neither: it runs games it did not build, and the legacy Game SDK and `discord-rpc` APIs the owned games call have no
server-side substitute (no REST presence endpoint, no device grant outside the Social SDK, `rpc.activities.write`
approved-partners-only). Writing a device-code client for Play would be code that can never legitimately set
presence for a game that does not ship the Social SDK, so it stays a design.

Should a future title ship the Social SDK — or should this project become an approved console developer — this is
the shape it would take.

**Consent and login.** Reuse the launcher's QR screen (`main.cpp`, the Steam QR login). The launcher requests a
device code, shows the QR and the 8-character user code, and opens `discord.com/activate`; the player approves on a
phone. The consent screen is Discord's own — Play never asks for a password, and the scopes requested are the
minimum for presence: `openid` + `sdk.social_layer_presence`. `sdk.social_layer` (communication) is not requested.

**Flow.** `POST https://discord.com/api/v10/oauth2/device/authorize` with the application's client id and the
presence scopes → poll `POST /oauth2/token` with
`grant_type=urn:ietf:params:oauth:grant-type:device_code` at the returned `interval` until `access_token` or a
terminal error (`authorization_pending` continues, `slow_down` backs off, `access_denied`/`expired_token` end the
attempt). The account must be verified. The application must be registered as a **Public Client** for a device
without a secret to authenticate itself; Discord warns "Most games will not want to ship with this enabled."

**Token storage (AGENTS.md rule 4).** Access and refresh tokens live only in the package's `LocalState`, in a file
the launcher creates with the package's own ACL, never in the repo, never in a log, never in a screenshot. Log lines
name the *event* ("Discord token refreshed"), never the value. A refresh token is rotated on use and the old one
dropped; sign-out deletes the file. Nothing about this may be committed — not even a redacted sample.

**Presence payload.** The game's own calls are the source. `discord-rpc`'s `Discord_UpdatePresence` carries a JSON
activity (`details`, `state`, `timestamps.start/end`, `assets.large_image/large_text/small_image/small_text`,
`party.size/max`, `buttons`); the Game SDK carries the same fields in `DiscordActivity`. Both map one-to-one onto
the Social SDK's activity update. Two rules: only what the game itself publishes is forwarded (Play never invents
an activity), and an empty or cleared presence (`Discord_ClearPresence`) clears it rather than leaving the last one
stuck.

**What would be offline-testable**, if it is ever built: the device-code state machine (pending → slow_down →
approved/denied/expired) against a stub HTTP endpoint, the activity-JSON mapping above, and the token-file rules
(never logged, rotated on refresh, deleted on sign-out). None of that needs the network or the console.
