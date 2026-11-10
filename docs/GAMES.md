# Game status

Per-title evidence for the games this project targets. Updated from real launcher runs on the console; see [PLAYABLE](PLAYABLE.md) for what "working" requires. Links to other docs are relative to this folder.

## Milestones

**Confirmed working: 0.** Last evidence update: **2026-11-09**. Megabonk is the first target;
any independently verified playable game is a milestone. These are the requested test targets,
not a compatibility claim. A series is not marked working because one entry works.

| Game | Steam app ID | Status | Latest evidence / next gate |
|---|---:|---|---|
| Megabonk | 3405340 | **Launch progress** | Console, launcher 0.5.178.0 to 0.5.198.0: reaches the main menu on the Xbox Series S. The owner confirmed audio and controller. Steam goes through the staged gbe_fork provider: real cloud saves download, and achievements are prepared. Fixes along the way: the greyed-menu hang (frame-latency waitable, [#50](https://github.com/jobucaldas/xboxnative/pull/50)), log-driven stutter ([#48](https://github.com/jobucaldas/xboxnative/pull/48)), and Present stalls (3 buffers, measured `avg=16.7ms p99=17.2 max=17.4` over 600 frames, [#53](https://github.com/jobucaldas/xboxnative/pull/53), not yet merged). Next gate: a recorded run of at least five minutes from the menu into gameplay, with a real pad, per [PLAYABLE](PLAYABLE.md). Evidence: [#46](https://github.com/jobucaldas/xboxnative/pull/46), [#50](https://github.com/jobucaldas/xboxnative/pull/50), [#53](https://github.com/jobucaldas/xboxnative/pull/53). |
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
| Haven | 983970 | **Launch progress** | Console, launcher 0.5.242.0: renders its intro, but has no controller input. Its controller plugin `InControlNative.dll` failed to map on `shlwapi.dll!StrCmpW`. 0.5.243.0 with [#81](https://github.com/jobucaldas/xboxnative/pull/81): the plugin maps and starts, the swap chain resizes to 1024x768 ([#82](https://github.com/jobucaldas/xboxnative/pull/82)), and the game runs at 33 ms per frame. Next gate: the owner confirms pad input, then gameplay recorded per [PLAYABLE](PLAYABLE.md). |
| Hades | 1145360 | **Blocked: console storage** | Console, launcher 0.5.243.0, download dry run: depot 1145362 is not in this account's licenses and is now skipped ([#78](https://github.com/jobucaldas/xboxnative/pull/78)). The two licensed depots need 11.3 GB; the console had 5.9 GB free. Next gate: install with enough free space, then launch. |
| Going Under | 1154810 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Fields of Mistria | 2142790 | **Blocked after game execution (off-device)** | Wine, Play: maps with the vcruntime140 route ([#61](https://github.com/jobucaldas/xboxnative/pull/61)), runs its own code, Steam fails naturally, SDL3 creates its windows; stops at `No matching GL pixel format available`. The game renders with OpenGL (Rust `glow` via SDL3 WGL), so it needs a guest OpenGL driver. No console run of this build. Console, launcher 0.5.230.0: game code runs, then `LoadLibrary(OPENGL32.DLL)` fails (no OpenGL on the console) and the game crashed on a thread Play did not create, which took the launcher down. 0.5.231.0 contains such crashes (the launcher reports and restarts); OpenGL remains the blocker. `OPENGL32.DLL` is now a Play-owned module ([#75](https://github.com/jobucaldas/xboxnative/pull/75)). Console, launcher 0.5.242.0: the load succeeds, SDL stops at the pixel-format error, and the game's `exit(1)` is contained; the launcher stays up ([#76](https://github.com/jobucaldas/xboxnative/pull/76)). Plan: Mesa's d3d12 driver, [research/OPENGL-PLAN.md](../research/OPENGL-PLAN.md). |
| Puyo Puyo Tetris 2 | 1259790 | **Launch progress** | Console, launcher 0.5.230.0: installs through the launcher. The Steam DRM passes with Valve's signed client and the owner's ownership ticket. The game renders and reaches its animated main menu. On the way it needed: DirectInput from the pinned Wine source (static import and COM creation), Crashpad's named pipe in the AppContainer namespace, and gbe's `PlaySoundA`. In that run the game later exited cleanly (`ExitProcess 0`, cause not yet known), and the launcher returned to the game list. The owner played a story match on the console; the game then hung at "Saving replay... Please wait!". Write diagnostics are in [#73](https://github.com/jobucaldas/xboxnative/pull/73). Next gate: a reproduction with those diagnostics, then a recorded match per [PLAYABLE](PLAYABLE.md). |
| Mega Man Legacy Collection | 363440 | **32-bit execution merged, console-unverified** | Console, launcher 0.5.230.0: the PE32 image maps for inspection (173 imports, 0 relocation errors) and stopped before any code ran. 32-bit execution (a PE32 interpreter with a guest Win32/CRT surface, [#108](https://github.com/jobucaldas/xboxnative/pull/108), [#109](https://github.com/jobucaldas/xboxnative/pull/109), [#111](https://github.com/jobucaldas/xboxnative/pull/111), plus [#118](https://github.com/jobucaldas/xboxnative/pull/118) for case-insensitive module names, a game-sized instruction cap and the probe's executable directory) is now in main: the launcher starts PE32 images in the interpreter instead of refusing them. It has run an owned mingw-CRT program off-device to exit 42, with stdout and exit code identical under Wine; it has **not** run on the console and has **not** run this game. Steam, cloud saves and presence are not staged on the PE32 path yet. Next gate: `src/xbox-test/tools/play-test.sh 363440` and the first `PRUN PE32 fault kind=... eip=...` line, which names the next blocker. |
| Mega Man Legacy Collection 2 | 495050 | Queued — not tested | No launcher Play or gameplay evidence recorded. |
| Say No! More | 1191900 | **Launch progress** | Console, launcher 0.5.242.0: reaches its main menu. Mono's null write during initialization was its JIT writing to memory the console maps W^X; Play now emulates guest RWX ([#72](https://github.com/jobucaldas/xboxnative/pull/72)). Earlier fixes: `GetUserObjectInformationW`, `PathFileExistsW`, IMM calls, `SendMessageTimeoutW`, `WaitForInputIdle`, multimedia timers. Next gate: gameplay with a real pad, recorded per [PLAYABLE](PLAYABLE.md). |
| Halo Wars: Definitive Edition | 459220 | **Launch progress** | Console, launcher 0.5.242.0: reaches its title screen. Fixes on the way: CFG-built runtime DLLs map as under Wine ([#71](https://github.com/jobucaldas/xboxnative/pull/71)), TLS slot 0 for its MSVC /GA executable ([#77](https://github.com/jobucaldas/xboxnative/pull/77)), and the first `WM_SIZE` from `CreateWindowEx` (the game divided by its zero-sized window otherwise, [#79](https://github.com/jobucaldas/xboxnative/pull/79)). Next gate: menus and a match with a real pad. |
| Slime Rancher | 433340 | **Launch progress** | Console, launcher 0.5.243.0: Unity reaches the main menu, drawn whole. Before [#82](https://github.com/jobucaldas/xboxnative/pull/82), its resize to 1920x1080 failed and the menu showed zoomed in an 800x600 chain. Next gate: gameplay with a real pad, recorded per [PLAYABLE](PLAYABLE.md). |

### What counts

- **Queued / not tested:** no real launcher Play evidence. Ownership, downloading, store badges and
  an assumed engine or graphics API do not establish compatibility.
- **Blocked:** a real attempt reached a failing prerequisite; record the first observed blocker,
  exact launcher version/source and evidence PR. Do not infer later failures that execution never reached.
- **Launch progress:** game code, a window or a main menu has appeared, but the full playability
  criteria are not yet met. Record the partial milestone without calling the game working.
- **Confirmed working:** meet the existing [playability requirements](PLAYABLE.md), applied to
  the individual title: real launcher Play and correlated evidence; visible, stable menu for at least
  five minutes; controller navigation into a run; nonzero audio output; successful legitimate Steam
  integration; and at least five minutes of playable gameplay with measured frame rate.

For every promotion, record the exact build/source, date, input method (real pad or labelled synthetic),
observed duration, frame rate, audio/Steam checks, limitations and evidence PR. Raw account-bearing
captures and game files stay private. Host smoke, Wine fixtures and CI are separate regression gates,
not game milestones. Record failures honestly; never weaken a gate to increase the count.

## Graphics API survey

Static evidence only — imports (`llvm-readobj --coff-imports`) and strings in the installed files.
A survey result is not a compatibility claim and does not move a title's status above.

| Game | Vulkan badge | Evidence |
|---|---|---|
| Megabonk, Slime Rancher, Say No! More | `Vulkan-optional` | `UnityPlayer.dll` carries `vulkan-1.dll` and `force-vulkan`; no static import. Unity's Windows default is D3D11, which they use. |
| Fields of Mistria | `Vulkan-optional` | `SDL3.dll` carries `vulkan-1.dll` and the `VK_*` error names; the game renders through OpenGL (SDL3 WGL), no static import. |
| Halo Wars: Definitive Edition, Puyo Puyo Tetris 2, Mega Man Legacy Collection | `none` | No Vulkan strings and no import. |

No installed title statically imports `vulkan-1.dll`. Play ships a Vulkan loader that answers "no GPU" the
way Windows does, so a Vulkan-optional engine falls back instead of failing; a real Vulkan driver is not
needed by any title today. Details: [research/VULKAN-PLAN.md](../research/VULKAN-PLAN.md).
