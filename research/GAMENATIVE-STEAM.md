# GameNative / Pluvia: Steam integration comparison

This comparison explains which public-source ideas inform the
[legitimate Xbox Steam architecture](STEAM-API-PATH.md), and which do not meet
its requirements. It does not supply an implementation, a client ABI map or
proof that either project's complete runtime can be reproduced on Xbox.

## Source revisions and limits

Read-only source review, checked 2026-11-02:

- GameNative [`dc24544101ef53543614041d7a8d8af68738090a`](https://github.com/utkarshdalal/GameNative/commit/dc24544101ef53543614041d7a8d8af68738090a).
- Pluvia [`467a631f9006893597f1ef5f1aa9d4d3c5744874`](https://github.com/oxters168/Pluvia/commit/467a631f9006893597f1ef5f1aa9d4d3c5744874).

Links below refer to those revisions, not moving default branches. These are
comparison sources, **not dependencies pinned in `deps.pin`**. No reference
binary was executed or imported for this documentation update. Source statements
about withheld components are maintainer descriptions, not verification of their
implementation or of a current Valve token-login ABI.

## Authentication/download is not native Steamworks

GameNative's
[`SteamService.kt`](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/service/SteamService.kt#L3628)
and Pluvia's
[`SteamService.kt`](https://github.com/oxters168/Pluvia/blob/467a631f9006893597f1ef5f1aa9d4d3c5744874/app/src/main/java/com/OxGames/Pluvia/service/SteamService.kt#L784)
use JavaSteam authentication sessions for credential/QR login. Pluvia also
[delegates depot downloads to `ContentDownloader`](https://github.com/oxters168/Pluvia/blob/467a631f9006893597f1ef5f1aa9d4d3c5744874/app/src/main/java/com/OxGames/Pluvia/service/SteamService.kt#L494).
Network login, library/download services and their callback managers are not the
game's native Steamworks interface context or callback queue.

Xboxnative implements its launcher CM session and downloader in C++
[`main.cpp`](../xbox-test/app/main.cpp) and
[`steamdl.h`](../xbox-test/app/steamdl.h), not through JavaSteam. Neither an
existing launcher session nor a downloaded ticket proves original game API init.
Do not copy token-bearing logging, command-line or session-transfer patterns
from reference projects.

## Distinct runtime paths

- **Pluvia's replacement path is excluded.** Its
  [`SteamPipeServer`](https://github.com/oxters168/Pluvia/blob/467a631f9006893597f1ef5f1aa9d4d3c5744874/app/src/main/java/com/winlator/steampipeserver/SteamPipeServer.java#L70)
  returns fixed success for init/is-running and leaves callback cases without
  delivery behavior. This is not evidence of a genuine Valve user context,
  ownership or callbacks. API replacement/emulation cannot satisfy this project.
- **GameNative's headless Windows path is not a reusable public implementation.**
  Its [launch orchestration](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/ui/screen/xserver/XServerScreen.kt#L5233)
  passes configuration to a helper. The
  [Launcher Compatibility Helpers notice](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/THIRD_PARTY_NOTICES#L252)
  explicitly identifies the helper sources as withheld and the binaries as
  proprietary. Public orchestration does not reveal or validate the complete
  bootstrap, logon, discovery, process and IPC implementation. Do not import the
  restricted helper or defeat package restrictions.
- **GameNative's Android bootstrap is not a Windows x64 solution.**
  [`SteamBootstrap.kt`](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/app/src/main/java/app/gamenative/SteamBootstrap.kt#L126)
  starts a native bootstrap process and monitors a ready/status file. Its
  [Steam Client Bootstrap Shim notice](https://github.com/utkarshdalal/GameNative/blob/dc24544101ef53543614041d7a8d8af68738090a/THIRD_PARTY_NOTICES#L116)
  identifies the source as intentionally absent and the binary as proprietary.
  Android/Linux client libraries and host interfaces do not establish Xbox
  compatibility. A helper status of `READY` does not prove game Steamworks init.

## Architectural consequence

Retain the **complete, unmodified genuine Windows client on Xbox** as the design
to validate, not an isolated client DLL, replacement Steam API, proprietary helper
or off-console production session. The missing process/IPC implementation and
original API acceptance remain independent of launcher login. Public-source
comparisons cannot close those gates; use the source routes, prerequisite tests
and evidence boundaries in [STEAM-API-PATH.md](STEAM-API-PATH.md).
