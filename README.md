# Xbox Native (xboxnative)

Run Windows x64 Steam games on an **Xbox Series S** (Dev Mode) inside a UWP "Game" app, using a Wine-style
loader plus our own POSIX/syscall host layer (`uwph`). Status: **early, experimental**.

## Honest current state
- Several owned games reach their menus or title screens on the console (Megabonk, Puyo Puyo Tetris 2, Say No! More,
  Slime Rancher, Going Under, Halo Wars: DE, Haven); none is yet marked **confirmed working**. See
  [docs/GAMES.md](docs/GAMES.md) for what each has shown and what blocks it. The Play runtime (`src/xbox-test/app/tests/play-game-host.sh`
  with an owned install) also runs games off-device under Wine for regression testing.
- Working on the console today (see `docs/xbox/LANES.md`, `src/xbox-test/uwph/*/README.md`): the launcher with Steam QR
  login, its own downloader (licensed depots only), per-game install location (internal or USB) and Steam Cloud sync;
  the uwph host (threads, TLS, exceptions, files, loopback TCP, W^X via `VirtualProtectFromApp`); the Play PE loader
  (64-bit; 32-bit maps as data only); CoreWindow D3D11 swap chain; gamepad input; audio; real system
  `d3d11`/`dxgi`/`ws2_32`/`bcrypt`. Known gaps: Direct3D 9/10/12, Vulkan and OpenGL for games that need them, and 32-bit
  execution (an x86 core exists off-device and is not wired in yet).
- Steam: owned games get an emulated Steam API (gbe_fork, fetched and pinned, never committed) or, for SteamStub
  titles, Valve's signed client with the owner's ownership ticket; achievements and cloud saves go to the real
  account. Games' own files are never patched on disk.
- "Playable" is defined by evidence, not announcement: [docs/PLAYABLE.md](docs/PLAYABLE.md).

## Game status

Which games run on the console, and the evidence for each, is tracked in [docs/GAMES.md](docs/GAMES.md)
(per-title status, last evidence date, what counts as working). Nothing is marked working until it meets
[docs/PLAYABLE.md](docs/PLAYABLE.md).

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
