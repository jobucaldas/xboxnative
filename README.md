# xboxnative

Run Windows x64 Steam games on an **Xbox Series S** (Dev Mode) inside a UWP "Game" app, using a Wine-style
loader plus our own POSIX/syscall host layer (`uwph`). Status: **early, experimental**.

## Honest current state
- **Megabonk (Steam appid 3405340) is not running game code yet.** The launcher's Play button downloads and maps
  the game's files and reports precisely what is missing; no Megabonk instruction has executed on the console.
- Working on the console today (see `docs/xbox/LANES.md`, `xbox-test/uwph/*/README.md`): the launcher with Steam QR
  login and its own downloader; the uwph host (threads, TLS, exceptions, files, loopback TCP, W^X via
  `VirtualProtectFromApp`); a PE mapper (64-bit and, as data only, 32-bit); CoreWindow D3D11 swap chain; gamepad; real
  system `d3d11`/`dxgi`/`ws2_32`/`bcrypt`. Known gaps: user32 message loop, input, audio (zero samples), Steam API path,
  32-bit execution (needs an x86 engine), Direct3D 9.
- "Playable" is defined by evidence, not announcement: [docs/PLAYABLE.md](docs/PLAYABLE.md).

## Design
launcher → Wine-style loader → ELF/PE shim → `uwph` (POSIX/syscall host in UWP, in-process wineserver, no AF_UNIX).
Rationale and checklists: [research/WINE-BASE-REPORT.md](research/WINE-BASE-REPORT.md) (use Wine's `*.spec` files as the
checklist; donor PE-only DLLs), [research/D3D-LAYERS-REPORT.md](research/D3D-LAYERS-REPORT.md) (D3D9/D3D11 options),
[research/X86-32BIT-PLAN.md](research/X86-32BIT-PLAN.md) (32-bit plan, held until Megabonk is playable).
Backlog: [docs/BACKLOG.md](docs/BACKLOG.md). Rebuild notes and lessons: [docs/xbox/REBUILD.md](docs/xbox/REBUILD.md).

## Layout
| Path | What |
|---|---|
| `xbox-test/app` | Launcher (grid, detail screen, Steam QR login, downloader, Play) |
| `xbox-test/uwph` | POSIX/syscall host (`host`), `user32`, `hid`, `display`, spike-dispatch |
| `xbox-test/dp` | Xbox Device Portal scripts (install/launch/logs/screenshot) |
| `xbox-test/kit`, `vrx`, `shared`, `tools` | test kit, throwaway probe package, shared headers, `play-test.sh` |
| `xbox-test/smoke-all.sh` | Full regression (needs a console for the on-device sections; `--offline` for the rest) |
| `tools/xbox` | Import/gap analysis against Wine's spec files |
| `research`, `docs` | Reports, evidence logs, backlog, lane notes |
| `deps.pin`, `tools/fetch-*.sh`, `patches/` | Pinned external dependencies (not copied here) |

## Build
Linux host. Needs `clang-cl`/`lld-link` (LLVM 19), the xwin-provided Windows SDK/CRT, `makemsix`, `osslsigncode`, `python3`.
```sh
tools/fetch-uwp-crossbuild.sh            # clones the pinned build wrapper (default /workspace/ref/uwp-crossbuild, or $UWP_CROSSBUILD)
bash xbox-test/app/build.sh              # launcher -> xbox-test/app/out/xbtest.msix (signed with a throwaway local test cert)
bash xbox-test/uwph/host/build.sh        # uwph host test package -> xbox-test/uwph/host/out/k32.msix
```
The current builds do **not** need Boxedwine sources. `tools/fetch-boxedwine.sh` fetches the pinned upstream
(`deps.pin`) for future work (x86 engine); see [patches/README.md](patches/README.md).

Wine-built donor DLLs (avrt, hid, setupapi, shcore, version) are never committed. Build them from the pinned Wine 10.0 source
(needs mingw-w64, flex, bison; about a minute):
```sh
bash tools/build-wine-donors.sh          # -> out/wine-donors/*.dll + SHA256SUMS + SOURCE.txt (git-ignored)
```

## Test
```sh
python3 -m unittest discover -s tools/xbox/tests -p 'test_*.py'   # some tests skip without local game fixtures
bash xbox-test/app/tests/session-restore.sh
bash xbox-test/kit/check-install-guide.sh
xbox-test/smoke-all.sh --offline                                    # off-device sections
# On a console (Dev Mode, Device Portal enabled; DP_PASS in the environment, never committed):
xbox-test/tools/play-test.sh 3405340                                # "Play Megabonk via the launcher"
```
See `xbox-test/dp/README.md` for Device Portal setup and `xbox-test/kit/INSTALL.md` for installing on a console.

## Rules of the road
Legitimate Steam path only: your own account via our launcher's login, official depots, Valve's unmodified client.
No DRM bypass, no emulators/cracks (no Steamless, no gbe_fork), no Xbox security bypass. You must own the games you run.
No game files, Valve/Microsoft binaries, or credentials in this repository. See [AGENTS.md](AGENTS.md).

## Licence
GPL-3.0-or-later ([LICENSE](LICENSE), [NOTICE](NOTICE) for third-party pieces and the compatibility notes). Originated in the
fork `jobucaldas/Boxedwine-xbox-64` (a fork of Boxedwine, GPL-2.0-or-later, compatible through "or later"); only our own
additions were imported here. No third-party binaries are committed: anything built from other projects' source is built at
setup time from a pinned source (see `deps.pin`, `tools/fetch-*.sh`, `tools/build-wine-donors.sh`).

## CI
`.github/workflows/ci.yml` runs offline checks on every pull request: shell syntax, Python compile, the offline unit tests,
and guards against committed secrets and binaries. The on-device sections of `xbox-test/smoke-all.sh` need a console and
cannot run in Actions; state their result in the PR.
