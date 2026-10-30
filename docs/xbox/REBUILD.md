# REBUILD.md — lessons from before the box reset (Sun 2026-10-25 ~20:02 BRT)

The box was reset: /workspace/Boxedwine-xbox-64 (with its local-only `x64-spike` branch), worktrees,
x64-run, Megabonk files, tailscale, Claude/Codex logins, DP_PASS and TS_AUTHKEY were lost.
GitHub (jobucaldas/Boxedwine-xbox-64) only has `master`. Nothing is recoverable. This file is the
memory of what was learned. Surviving on the console: the installed Boxedwine dev app + its results.
Surviving in the agent store: earlier screenshots/proof videos (grok-bot/agents/*/attachments).

RULES: no pushes anywhere, no posts to other people's repos, no secrets in logs/files.
Back up work with `x64-run/backup.sh` (git bundles into the persistent agent store) after each milestone.

## (a) Console results (Xbox Series S, OS 26100.9445, Dev Mode, Device Portal https://<ip>:11443)
Console was at 192.168.15.96 (LAN IP can change); reached via Tailscale subnet route through "Encom"
(a machine on João's LAN). Device Portal user: jobucaldas. Password = DP_PASS (João provides).

What makes the app stay open and run on the console:
- App must IGNORE `-ServerName:<class>` activation args (that is what made it close on click before).
- GUI subsystem exe (not console), MTA threading, AppContainer, launch id `Family!App`
  (PackageFamilyName!AppId).
- Sign the console user jobucaldas in via `PUT /ext/user` on Device Portal before launching.
- App type must be Game in Dev Home (João confirmed it is) -> memory limit 5120 MB (Game mode).
- Pack/sign on Linux: `makemsix pack` + `osslsigncode sign -pkcs12` (this works; verified). Cert
  .cer must be given to Device Portal if it asks. Reference build setup: gianlucamazza/uwp-crossbuild
  (cloned at /workspace/ref/uwp-crossbuild).

Verified on real console:
- D3D11 feature level 11_0 OK; D3D12 OK.
- CoreWindow swap chain at 1080p, 120/120 frames drawn.
- W^X works via VirtualProtectFromApp (write first, then flip to execute) -> JIT/Windows game code ok.
- Exception handling, threads, file storage (LocalState), loopback TCP, gamepad API OK.
Failures / restrictions:
- AF_UNIX sockets REFUSED -> Wine server cannot use unix sockets; needs in-process wineserver / other channel.
- XAudio2 played 0 samples (audio unresolved, queued as bug).
- Host layer (uwph) self-test exited with error but output was lost (rerun with file logging).
- Ports 57344+ dropped (don't use for listeners / tests).
- Logs: the app writes a live on-screen log plus ladder-native.log in LocalState; pull via Device Portal.

## (b) Design
launcher -> Wine's own loader -> ELF shim -> `uwph` (POSIX host layer for UWP/AppContainer)
with an IN-PROCESS wineserver (no AF_UNIX), native D3D11 passthrough (Wine d3d11 -> real D3D11 on
console), and our own TLS provider (console has no OpenSSL/GnuTLS; Wine HTTPS via own TLS layer,
cert checks ON; reached Steam servers on PC; root certs + Windows-side hookup were left to do).
Windows-style sockets were implemented in the host layer's Windows side.

Steam (legitimate only; NO emulators (no gbe_fork), NO Steamless, NO DRM bypass/patching of Valve files):
- Own launcher login (QR) + own downloader (downloaded Megabonk: 69 files, 523 MB, all hashes OK).
- DRM via a headless `steamhost64.exe` that loads Valve's UNMODIFIED client (not steamwebhelper; no
  browser packages at all), pinned to build 1788652215 (same as GameNative; Valve still serves it).
  João approved static analysis of ONE pinned build to map the internal interfaces, like GameNative.
  Helper must refuse to run if build hash mismatches; permanently block functions that revoke saved
  login tokens (sits next to sign-in fn; GameNative had a bug that revoked tokens). Order:
  1 map interfaces, 2 validate vs file, 3 anonymous run, 4 real sign-in + Megabonk check, 0 browsers.
- Earlier "SteamAPI_Init PASS" was INVALID (gbe_fork steam_api64.dll). Don't repeat.
- `steamcmd` is NOT usable as the game's Steam session (no steam_api channel).
- Steam client blocked on this PC network by CRL check over plain http (port 80): fix = fetch the
  CRLs over HTTPS and verify (all checks on). After that steamcmd anonymous login took 10-83 s.
  Valve client under Wine connects slowly: wait >= 6 min before declaring failure.
- GameNative's "Bionic" mode = Valve Android ARM lib -> not applicable to Xbox.
- Public interface map (OpenSteamClient) matches an older client, not the current one.
- Do NOT touch João's real token/account until the map is validated. Test non-anti-cheat games only.

## (c) Megabonk
- Unity 2023.2, launch with `-force-d3d11`. Needs Steam DRM startup check to pass (steam_api64.dll real).
- Files must be re-downloaded via João's Steam account (needs his QR login).
- First goal on console: any frame (Unity splash), then logs on screen; then gameplay.

## Toolchain rebuilt (2026-10-25)
wine 10.0 (Debian 13; `wine` is 64-bit; wine32 not installed), mingw-w64 (posix alternatives set),
osslsigncode, cmake, ninja, socat, tailscale 1.104 (not joined), node + claude/codex CLIs in ~/.local/bin
(not logged in), `makemsix` built from microsoft/msix-packaging with --pack, gcc instead of clang,
C++17 (set in CMakeLists.txt and lib/xerces/CMakeLists.txt) -> /usr/local/bin/makemsix.
Build tree: /workspace/tools/msix-packaging. Clones: /workspace/x64-run/clone.sh (idempotent).
makemsix `sign` produced no output file in a quick test; osslsigncode works.

## Next steps (priority)
1. Console test app (native ladder) rebuilt from scratch: GUI/MTA/AppContainer, ignores -ServerName,
   on-screen log, ladder-native.log. Reference: Boxedwine-uwp-upstream + uwp-crossbuild.
2. Get Megabonk first frame on console. 3. Steam helper chain. 4. back up after every milestone.

## Backups
- `x64-run/backup.sh` -> git bundles (local-only commits, i.e. not on any remote) + uncommitted diff/untracked
  + notes, into /cursor/stores/store-a55f5844-.../x64-backup (persistent; survived the reset). Run after each milestone.
- Store quirk: single files > ~64 MB fail to close (EINVAL); script splits into 32 MB parts and sha256-verifies.
- `x64-run/backup-loop.sh` runs backup every 6 h (no cron here; restart it after a box reset).
- Local branch `x64-spike` in Boxedwine-xbox-64 holds all local work. NEVER push. Upstream ref: Boxedwine-uwp-upstream
  (worleydl/Boxedwine-uwp); fork parent is danoon2/Boxedwine.

## Linux UWP app build preflight

Run `tools/check-uwp-build-env.sh` before `xbox-test/app/build.sh`. The checker follows that
script's actual Python, MinGW, linker, OpenSSL, packer and signer commands and requires
`UWP_CROSSBUILD` (or an adjacent `../uwp-crossbuild`) to contain executable `scripts/build.sh`.
Tool paths can be overridden with `PYTHON3_PATH`, `MINGW_GCC_PATH`, `MINGW_GXX_PATH`,
`MINGW_DLLTOOL_PATH`, `LLD_LINK_PATH`, `OPENSSL_PATH`, `MAKEMSIX_PATH` and `OSSLSIGNCODE_PATH`.
The shared wrapper checkout, when available, can be selected with `UWP_CROSSBUILD`.
The checker reports tools present; it does not prove SDK/CRT usability, compilation, package
validity or Xbox runtime acceptance. See `docs/xbox/package-input-audit.md` for build inputs.
Wine is used by off-device runtime tests, not by this app/MSIX build path.
