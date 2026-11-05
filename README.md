# xboxnative

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
  32-bit execution (needs an x86 engine), Direct3D 9.
- Steam: no Steam API provider runs on the console yet. Play loads a game's original `steam_api64.dll` unmodified
  and its `SteamAPI_Init` fails naturally; `steamclient64.dll` is always refused. Games that tolerate a missing client
  can proceed. Next: an emulated API (gbe_fork) for owned games with achievements and cloud saves synced to the real
  account, a [playability](docs/PLAYABLE.md) requirement.
- "Playable" is defined by evidence, not announcement: [docs/PLAYABLE.md](docs/PLAYABLE.md).

## Game milestones

**Confirmed working: 0.** Last evidence update: **2026-11-03**. Megabonk is the first target;
any independently verified playable game is a milestone. These are the requested test targets,
not a compatibility claim. A series is not marked working because one entry works.

| Game | Steam app ID | Status | Latest evidence / next gate |
|---|---:|---|---|
| Megabonk | 3405340 | **Blocked before game execution** | Launcher 0.5.169.0, source `95cb0c7`, attempt `6924:1`: terminal loader failure, `gameCodeExecuted=false`. Active load configuration unsupported. Donor-route acceptance fails because required game IAT routes were not reached; this is not yet a separate routing diagnosis. Evidence: [#25](https://github.com/jobucaldas/xboxnative/pull/25). |
| Danganronpa: Trigger Happy Havoc | 413410 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Danganronpa 2: Goodbye Despair | 413420 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Danganronpa Another Episode: Ultra Despair Girls | 555950 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Danganronpa V3: Killing Harmony | 567640 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Danganronpa V3: Killing Harmony Demo Ver. | 589120 | Queued — not tested (demo) | Demo evidence will not certify the full game. |
| Hyperdimension Neptunia Re;Birth1 | 282900 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Hyperdimension Neptunia Re;Birth2 Sisters Generation | 351710 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Hyperdimension Neptunia Re;Birth3 V Generation | 353270 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Hyperdimension Neptunia U: Action Unleashed | 387340 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Megadimension Neptunia VII | 460120 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Cyberdimension Neptunia: 4 Goddesses Online | 632350 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Megadimension Neptunia VIIR | 774511 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Neptunia Shooter | 1037590 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Haven | 983970 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Hades | 1145360 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Going Under | 1154810 | Queued — not tested | No launcher Play or gameplay evidence recorded. |

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
[research/X86-32BIT-PLAN.md](research/X86-32BIT-PLAN.md) (32-bit plan, held until Megabonk is playable).
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
