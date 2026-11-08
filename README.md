# Xbox Native (xboxnative)

Run Windows x64 Steam games on an **Xbox Series S** (Dev Mode) inside a UWP "Game" app, using a Wine-style
loader plus our own POSIX/syscall host layer (`uwph`). Status: **early, experimental**.

## Honest current state
- **Megabonk (Steam appid 3405340) has not run on the console yet.** Off-device, under Wine, the Play runtime
  (`src/xbox-test/app/tests/play-game-host.sh` with an owned install) now runs the game's own code to its first screen
  (the photosensitivity warning) through Play's loader, user32 record core, Wine donors and CoreWindow DXGI adapter,
  with a native Wine window standing in for the CoreWindow. Steam fails naturally (no client). That is evidence for
  the runtime, not a console result: the console run, input, audio and Steam (achievements, cloud saves) remain open.
- Working on the console today (see `docs/xbox/LANES.md`, `src/xbox-test/uwph/*/README.md`): the launcher with Steam QR
  login and its own downloader; the uwph host (threads, TLS, exceptions, files, loopback TCP, W^X via
  `VirtualProtectFromApp`); a PE mapper (64-bit and, as data only, 32-bit); CoreWindow D3D11 swap chain; gamepad; real
  system `d3d11`/`dxgi`/`ws2_32`/`bcrypt`. Known gaps: input delivery to the guest, audio (zero samples), Steam API path,
  32-bit execution (an x86 core exists off-device but is not wired in), Direct3D 9.
- Steam: no Steam API provider runs on the console yet. Play loads a game's original `steam_api64.dll` unmodified
  and its `SteamAPI_Init` fails naturally; `steamclient64.dll` is always refused. Games that tolerate a missing client
  can proceed. Next: an emulated API (gbe_fork) for owned games with achievements and cloud saves synced to the real
  account, a [playability](docs/PLAYABLE.md) requirement.
- "Playable" is defined by evidence, not announcement: [docs/PLAYABLE.md](docs/PLAYABLE.md).

## Game milestones

**Confirmed working: 0.** Last evidence update: **2026-11-08**. Megabonk is the first target;
any independently verified playable game is a milestone. These are the requested test targets,
not a compatibility claim. A series is not marked working because one entry works.

| Game | Steam app ID | Status | Latest evidence / next gate |
|---|---:|---|---|
| Megabonk | 3405340 | **Launch progress** | Console, launcher 0.5.178.0 to 0.5.198.0: reaches the main menu on the Xbox Series S. The owner confirmed audio and controller. Steam goes through the staged gbe_fork provider: real cloud saves download, and achievements are prepared. Fixes along the way: the greyed-menu hang (frame-latency waitable, [#50](https://github.com/jobucaldas/xboxnative/pull/50)), log-driven stutter ([#48](https://github.com/jobucaldas/xboxnative/pull/48)), and Present stalls (3 buffers, measured `avg=16.7ms p99=17.2 max=17.4` over 600 frames, [#53](https://github.com/jobucaldas/xboxnative/pull/53), not yet merged). Next gate: a recorded run of at least five minutes from the menu into gameplay, with a real pad, per [PLAYABLE](docs/PLAYABLE.md). Evidence: [#46](https://github.com/jobucaldas/xboxnative/pull/46), [#50](https://github.com/jobucaldas/xboxnative/pull/50), [#53](https://github.com/jobucaldas/xboxnative/pull/53). |
| Danganronpa: Trigger Happy Havoc | 413410 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Danganronpa 2: Goodbye Despair | 413420 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Danganronpa Another Episode: Ultra Despair Girls | 555950 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Danganronpa V3: Killing Harmony | 567640 | **Blocked: console storage** | Console, launcher 0.5.243.0, download dry run: depot 567648 is not in this account's licenses and is now skipped instead of failing the install ([#78](https://github.com/jobucaldas/xboxnative/pull/78)). The licensed depots need 22.3 GB; the console had 5.9 GB free. Next gate: install with enough free space, then launch. |
| Danganronpa V3: Killing Harmony Demo Ver. | 589120 | Queued — not tested (demo) | Demo evidence will not certify the full game. |
| Hyperdimension Neptunia Re;Birth1 | 282900 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Hyperdimension Neptunia Re;Birth2 Sisters Generation | 351710 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Hyperdimension Neptunia Re;Birth3 V Generation | 353270 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Hyperdimension Neptunia U: Action Unleashed | 387340 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Megadimension Neptunia VII | 460120 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Cyberdimension Neptunia: 4 Goddesses Online | 632350 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Megadimension Neptunia VIIR | 774511 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Neptunia Shooter | 1037590 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Haven | 983970 | **Launch progress** | Console, launcher 0.5.242.0: renders its intro, but has no controller input. Its controller plugin `InControlNative.dll` failed to map on `shlwapi.dll!StrCmpW`. 0.5.243.0 with [#81](https://github.com/jobucaldas/xboxnative/pull/81): the plugin maps and starts, the swap chain resizes to 1024x768 ([#82](https://github.com/jobucaldas/xboxnative/pull/82)), and the game runs at 33 ms per frame. Next gate: the owner confirms pad input, then gameplay recorded per [PLAYABLE](docs/PLAYABLE.md). |
| Hades | 1145360 | **Blocked: console storage** | Console, launcher 0.5.243.0, download dry run: depot 1145362 is not in this account's licenses and is now skipped ([#78](https://github.com/jobucaldas/xboxnative/pull/78)). The two licensed depots need 11.3 GB; the console had 5.9 GB free. Next gate: install with enough free space, then launch. |
| Going Under | 1154810 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Fields of Mistria | 2142790 | **Blocked after game execution (off-device)** | Wine, Play: maps with the vcruntime140 route ([#61](https://github.com/jobucaldas/xboxnative/pull/61)), runs its own code, Steam fails naturally, SDL3 creates its windows; stops at `No matching GL pixel format available`. The game renders with OpenGL (Rust `glow` via SDL3 WGL), so it needs a guest OpenGL driver. No console run of this build. Console, launcher 0.5.230.0: game code runs, then `LoadLibrary(OPENGL32.DLL)` fails (no OpenGL on the console) and the game crashed on a thread Play did not create, which took the launcher down. 0.5.231.0 contains such crashes (the launcher reports and restarts); OpenGL remains the blocker. `OPENGL32.DLL` is now a Play-owned module ([#75](https://github.com/jobucaldas/xboxnative/pull/75)). Console, launcher 0.5.242.0: the load succeeds, SDL stops at the pixel-format error, and the game's `exit(1)` is contained; the launcher stays up ([#76](https://github.com/jobucaldas/xboxnative/pull/76)). Plan: Mesa's d3d12 driver, [research/OPENGL-PLAN.md](research/OPENGL-PLAN.md). |
| Puyo Puyo Tetris 2 | 1259790 | **Launch progress** | Console, launcher 0.5.230.0: installs through the launcher. The Steam DRM passes with Valve's signed client and the owner's ownership ticket. The game renders and reaches its animated main menu. On the way it needed: DirectInput from the pinned Wine source (static import and COM creation), Crashpad's named pipe in the AppContainer namespace, and gbe's `PlaySoundA`. In that run the game later exited cleanly (`ExitProcess 0`, cause not yet known), and the launcher returned to the game list. The owner played a story match on the console; the game then hung at "Saving replay... Please wait!". Write diagnostics are in [#73](https://github.com/jobucaldas/xboxnative/pull/73). Next gate: a reproduction with those diagnostics, then a recorded match per [PLAYABLE](docs/PLAYABLE.md). |
| Mega Man Legacy Collection | 363440 | **Blocked: 32-bit** | Console, launcher 0.5.230.0: the PE32 image maps for inspection (173 imports, 0 relocation errors) and stops before any code runs: 32-bit execution is unsupported on the console. An off-device x86 execution core with a PE32 loader is in progress ([#80](https://github.com/jobucaldas/xboxnative/pull/80)); it has not run this game. |
| Mega Man Legacy Collection 2 | 495050 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Say No! More | 1191900 | **Launch progress** | Console, launcher 0.5.242.0: reaches its main menu. Mono's null write during initialization was its JIT writing to memory the console maps W^X; Play now emulates guest RWX ([#72](https://github.com/jobucaldas/xboxnative/pull/72)). Earlier fixes: `GetUserObjectInformationW`, `PathFileExistsW`, IMM calls, `SendMessageTimeoutW`, `WaitForInputIdle`, multimedia timers. Next gate: gameplay with a real pad, recorded per [PLAYABLE](docs/PLAYABLE.md). |
| Halo Wars: Definitive Edition | 459220 | **Launch progress** | Console, launcher 0.5.242.0: reaches its title screen. Fixes on the way: CFG-built runtime DLLs map as under Wine ([#71](https://github.com/jobucaldas/xboxnative/pull/71)), TLS slot 0 for its MSVC /GA executable ([#77](https://github.com/jobucaldas/xboxnative/pull/77)), and the first `WM_SIZE` from `CreateWindowEx` (the game divided by its zero-sized window otherwise, [#79](https://github.com/jobucaldas/xboxnative/pull/79)). Next gate: menus and a match with a real pad. |
| Slime Rancher | 433340 | **Launch progress** | Console, launcher 0.5.243.0: Unity reaches the main menu, drawn whole. Before [#82](https://github.com/jobucaldas/xboxnative/pull/82), its resize to 1920x1080 failed and the menu showed zoomed in an 800x600 chain. Next gate: gameplay with a real pad, recorded per [PLAYABLE](docs/PLAYABLE.md). |

### What counts

- **Queued / not tested:** no real launcher Play evidence. Ownership, downloading, store badges and
  an assumed engine or graphics API do not establish compatibility.
- **Blocked:** a real attempt reached a failing prerequisite; record the first observed blocker,
  exact launcher version/source and evidence PR. Do not infer later failures that execution never reached.
- **Launch progress:** game code, a window or a main menu has appeared, but the full playability
  criteria are not yet met. Record the partial milestone without calling the game working.
- **Confirmed working:** meet the existing [playability requirements](docs/PLAYABLE.md), applied to
  the individual title: real launcher Play and correlated evidence; visible, stable menu for at least
  five minutes; controller navigation into a run; nonzero audio output; successful legitimate Steam
  integration; and at least five minutes of playable gameplay with measured frame rate.

For every promotion, record the exact build/source, date, input method (real pad or labelled synthetic),
observed duration, frame rate, audio/Steam checks, limitations and evidence PR. Raw account-bearing
captures and game files stay private. Host smoke, Wine fixtures and CI are separate regression gates,
not game milestones. Record failures honestly; never weaken a gate to increase the count.

## Design
launcher → Wine-style loader → ELF/PE shim → `uwph` (POSIX/syscall host in UWP, in-process wineserver, no AF_UNIX).
Rationale and checklists: [research/WINE-BASE-REPORT.md](research/WINE-BASE-REPORT.md) (use Wine's `*.spec` files as the
checklist; donor PE-only DLLs), [research/D3D-LAYERS-REPORT.md](research/D3D-LAYERS-REPORT.md) (D3D9/D3D11 options),
[research/X86-32BIT-PLAN.md](research/X86-32BIT-PLAN.md) (staged 32-bit plan; stage 1, an off-device x86 interpreter core with PE32 loader and import thunks, is in `src/xbox-test/app/host/x86/` and tested by `src/xbox-test/app/tests/x86-core.sh`; it is not yet wired into the launcher, so 32-bit games still only map for inspection).
Backlog: [docs/BACKLOG.md](docs/BACKLOG.md). Rebuild notes and lessons: [docs/xbox/REBUILD.md](docs/xbox/REBUILD.md).

## Layout
Application, test, and tool sources live under `src/`, with tests beside the code they exercise.
Root-level documentation, research, dependency pins, patches, and licensing stay separate.
Build outputs and fetched third-party sources are ignored at any source-tree depth.

| Path | What |
|---|---|
| `src/xbox-test/app` | Launcher (grid, detail screen, Steam QR login, downloader, Play) |
| `src/xbox-test/uwph` | POSIX/syscall host (`host`), `user32`, `hid`, `display`, spike-dispatch |
| `src/xbox-test/dp` | Xbox Device Portal scripts (install/launch/logs/screenshot) |
| `src/xbox-test/kit`, `vrx`, `shared`, `tools` | test kit, throwaway probe package, shared headers, `play-test.sh` |
| `src/xbox-test/smoke-all.sh` | Full regression (needs a console for the on-device sections; `--offline` for the rest) |
| `src/tools/xbox` | Import/gap analysis against Wine's spec files |
| `src/research` | Research analysis scripts; their reports and data remain in root `research/` |
| `research`, `docs` | Reports, evidence logs, backlog, lane notes |
| `deps.pin`, `src/tools/fetch-*.sh`, `patches/` | Pinned external dependencies (not copied here) |

## Build
Linux host. Needs `clang-cl`/`lld-link` (LLVM 19), the xwin-provided Windows SDK/CRT, `makemsix`, `osslsigncode`, `python3`.
```sh
src/tools/fetch-uwp-crossbuild.sh            # clones the pinned build wrapper (default /workspace/ref/uwp-crossbuild, or $UWP_CROSSBUILD)
bash src/xbox-test/app/build.sh              # launcher -> src/xbox-test/app/out/xbtest.msix (signed with a throwaway local test cert)
bash src/xbox-test/uwph/host/build.sh        # uwph host test package -> src/xbox-test/uwph/host/out/k32.msix
```
If `makemsix` is missing, build the pinned Microsoft packer from source (native Clang,
CMake 3.29+, Ninja, pkg-config, Python 3 and ICU development files are required):
```sh
bash src/tools/build-msix-packaging.sh       # -> out/msix-packaging/bin/makemsix (git-ignored)
export PATH="$PWD/out/msix-packaging/bin:$PATH"
```
Packaging and XML validation stay enabled. The local C++17 compatibility patch supports
current ICU headers. `MSIX_SOURCE_DIR` / `MSIX_BUILD_DIR` can move the source/build caches;
`CC`, `CXX` and `JOBS` select the native compiler and parallelism. On split-output systems
such as NixOS, expose ICU's development `.pc` files through `PKG_CONFIG_PATH`, and ensure
`clang-cl` uses its real builtin-header resource directory (including `cpuid.h`).

The current builds do **not** need Boxedwine sources. `src/tools/fetch-boxedwine.sh` fetches the pinned upstream
(`deps.pin`) for future work (x86 engine); see [patches/README.md](patches/README.md).

The launcher packages a Steam API provider for owned games: the pinned gbe_fork release (`gbe-fork-win` in deps.pin,
LGPL-3.0), fetched and verified by `src/tools/fetch-gbe-fork.sh` during `build.sh` (needs `7z`); it is never committed.

Steam Cloud saves of a provider-served game download before launch and upload after exit (`steam_cloud.h`): Auto-Cloud
files go to the game's known folders, files saved through the Steam API go to gbe_fork's remote-storage folder, and
compressed copies are inflated and SHA-1 checked. The game's page shows the cloud status and a "Y: Sync cloud" button
(not available while a game is running). Each file decision is logged in `launch_probe.log` with its cloud name and
local path.

Wine-built donor DLLs (avrt, hid, setupapi, shcore, version) are never committed. Build them from the pinned Wine 10.0 source
(needs mingw-w64, flex, bison; about a minute):
```sh
bash src/tools/build-wine-donors.sh          # -> out/wine-donors/*.dll + SHA256SUMS + SOURCE.txt (git-ignored)
```

## Test
```sh
python3 -I src/tools/tests/test_source_layout.py
python3 -m unittest discover -s src/tools/xbox/tests -p 'test_*.py'   # some tests skip without local game fixtures
bash src/xbox-test/app/tests/session-restore.sh
bash src/xbox-test/kit/check-install-guide.sh
src/xbox-test/smoke-all.sh --offline                                    # off-device sections
# On a console (Dev Mode, Device Portal enabled; DP_PASS in the environment, never committed):
src/xbox-test/tools/play-test.sh 3405340                                # "Play Megabonk via the launcher"
```
See `src/xbox-test/dp/README.md` for Device Portal setup and `src/xbox-test/kit/INSTALL.md` for installing on a console.

## Rules of the road
Owned games only, never piracy: your own account via our launcher's login, official depots. An emulated Steam API
(gbe_fork) may serve an owned game's Steam calls, with achievements and cloud saves synced to your real account. Game
and Valve files stay unmodified on disk (no Steamless, no cracks), and there is no Xbox security bypass.
No game files, Valve/Microsoft binaries, or credentials in this repository. See [AGENTS.md](AGENTS.md).

## Licence
GPL-3.0-or-later ([LICENSE](LICENSE), [NOTICE](NOTICE) for third-party pieces and the compatibility notes). Originated in the
fork `jobucaldas/Boxedwine-xbox-64` (a fork of Boxedwine, GPL-2.0-or-later, compatible through "or later"); only our own
additions were imported here. No third-party binaries are committed: anything built from other projects' source is built at
setup time from a pinned source (see `deps.pin`, `src/tools/fetch-*.sh`, `src/tools/build-wine-donors.sh`).

## CI
`.github/workflows/ci.yml` runs offline checks on every pull request: shell syntax, Python compile, the offline unit tests,
and guards against committed secrets and binaries. The on-device sections of `src/xbox-test/smoke-all.sh` need a console and
cannot run in Actions; state their result in the PR.
