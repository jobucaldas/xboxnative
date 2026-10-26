# lane `plan` — notes

## What was done (2026-10-25, this session)

Objective (from `x64-run/lanes/plan.prompt`): research + write the concrete plan from "test frame on
console" to "Megabonk's first frame", with an import-gap table and an architecture recommendation.

1. Extracted full import tables from the real Megabonk binaries in `/workspace/megabonk-from-console`
   (`llvm-readobj --coff-imports` on `Megabonk.exe`, `UnityPlayer.dll`, `GameAssembly.dll`, `baselib.dll`,
   `UnityCrashHandler64.exe`, `D3D12Core.dll`). Raw dump: `docs/xbox/imports_raw.txt`.
2. Dispatched 3 parallel Explore agents to study: (a) `/workspace/Boxedwine-xbox-64`'s existing UWP
   integration (SDL2 WinRT layer, CoreWindow+D3D11 swapchain code, file system abstraction — all 32-bit
   CPU-emulation specific, not directly reusable for native 64-bit, but confirms the CoreWindow pattern);
   (b) Wine 10's unixlib/`__wine_unix_call_dispatcher` architecture using the Debian `wine64` package
   actually installed on this box (confirmed real x86_64 PE builtins exist at
   `/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/`, paired with ELF `.so` unixlibs at
   `x86_64-unix/` — the dispatch protocol is a function-pointer/opcode table, not ELF-bound); (c) the
   UWP reference ports (`uwp-crossbuild`'s exact build/pack/sign pipeline, `xenia-uwp`'s
   `VirtualAllocFromApp`/`VirtualProtectFromApp` W^X pattern, manifest capability patterns from
   `dolphin-uwp`/`UnleashedRecomp-uwp`).
3. Read `runs/g-status.md` to ground the plan in REAL current lane status (M1 app + deploy scripts
   verified on console; launcher+QR login done; downloads still a stub; Megabonk only 24/69 files
   present) rather than the stale "queued" status in the old `x64-run/LANES.md`.
4. Found prior art already in the repo I almost missed: `docs/daytona-wine11-gdi.md` and
   `tools/buildWine/`/`tools/d3dToWebGL` show this fork already has a mature pipeline for building Wine
   from source with custom patches and even compiling a standalone PE `wined3d.dll` targeting a
   non-standard backend (WebGL) — direct precedent for "PE DLL targeting a different backend", though
   that pipeline targets 32-bit (`-m32`) for Boxedwine's CPU-emulated guest, not the 64-bit native target
   needed here.
5. Key insight that reshaped the plan: **don't reimplement wined3d at all.** Xbox is a real Windows NT
   kernel — `d3d11.dll`/`dxgi.dll`/`d3d12.dll`/`ws2_32.dll`/crypto DLLs already work natively from
   AppContainer (REBUILD.md confirms this). Only `kernel32`/`ntdll` (W^X + CreateProcess block) and
   `user32`/`win32u` (no classic HWND, single CoreWindow only) actually need new Wine-builtin-plus-custom-
   unixlib work. This cuts the previously-feared "rewrite wined3d" (2000-5000 LOC) out of the critical
   path entirely.
6. Wrote `docs/xbox/LANES.md` (also copied to `/workspace/x64-run/LANES.md`, the shared file other lanes
   read): current-status table, the full import-gap table with per-DLL AppContainer verdicts, the
   architecture recommendation with a 3-way risk comparison (custom loader vs. recommended Wine-builtins
   vs. full wined3d translation), 5 ranked unknowns to spike first, and 8 new concrete lanes for the
   uwph/native-execution track (ID, files, exact build/test commands, acceptance, console/login needs,
   size, deps) plus cross-references to the `dl` lane and the kept `steam-map`/`steamhost` backlog items.
7. Committed `125a81bb` on `lane/plan`.

## Next steps (not done by this lane — handed off)

- The highest-leverage next move is **spike #2** in `docs/xbox/LANES.md` §3 (prove Wine's
  `__wine_unix_call_dispatcher` substitution works with a pure-PE replacement, off-console, before
  committing to the whole `uwph` track) and **spike #1** (does AppContainer's real kernel32 hard-fail
  on `VirtualAlloc(PAGE_EXECUTE_READWRITE)` — can be tested directly on console by extending the
  existing M1 app, cheaply). Both are called out as "start first" / can run in parallel with lane `dl`.
- Lane `dl` (Steam CM/depot downloader, C++/WinRT) is the other independent prerequisite before
  `megabonk-boot` can be attempted — not this lane's job, already in progress per `g-status.md` §4.
- No LANE_DONE marker was requested by the prompt text itself for this lane in the same explicit way as
  other lanes (the prompt's objective was specifically "research + write LANES.md", which is complete);
  marking done below per the project's general convention ("when the lane objective is fully done and
  verified, create LANE_DONE").
