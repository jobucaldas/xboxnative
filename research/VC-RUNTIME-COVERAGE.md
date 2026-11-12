# x64 Visual C++ runtime coverage matrix

Scope: which x64 Visual C++ / UCRT / support DLLs the owned Steam games
statically import, how Play routes each one today, and what this slice adds.
Import data: pefile scan (names only, no binaries committed) over
`/home/jobu/steam-owned/games` — 39 x64 binaries; scan artifacts kept local
(`/tmp/xbn-vcrt/imports-all.json`, `imports.json`). PE32 (0x14c) binaries
(Proteus.exe, steam_api.dll, InControl plugin, Assembly-CSharp) are out of
scope here — see `research/X86-32BIT-PLAN.md`.

## Matrix

| DLL | Imported by (owned, x64) | Route today | Decision | Test that proves it | Status |
|---|---|---|---|---|---|
| vcruntime140.dll | FieldsOfMistria.exe (8), xgameFinal.exe (23), Proteus.exe (14), AmqpMessenger (17), PlayerChatUI (18), commonEventLoggingLibrary (18), vccorlib140 (13), msvcp140 (18), concrt140 (12) — 36 distinct names | `src/xbox-test/uwph/vcruntime/route.h` (`vcruntime_core`): forwards Wine 10 spec names to console ucrtbase + owned helpers (`__vcrt_*`, `__report_gsfailure`, `_CxxThrowException`) | Console route (owned) | `play-runtime.sh` native-vcrt mode grep gates | covered |
| vcruntime140_1.dll | none statically (xgameFinal links it only via msvcp140's delay chain) | same `vcruntime_core` route covers the 140_1 spec names | Console route (owned) | same as above | covered |
| msvcp140.dll | xgameFinal.exe (133), commonEventLoggingLibrary (80), AmqpMessenger (37), Proteus.exe (17), PlayerChatUI (4), concrt140 (31), vccorlib140 (3) — 156 distinct names | Wine `runtime/msvcp140` donor build (`src/tools/build-wine-donors.sh` RUNTIME list) served via `guest_loader_search.h` `redistributable()` | Wine donor (pinned wine-10.0) | `play-runtime.sh` native-msvcp mode; `play-msvcp.cpp` /MD fixture | covered |
| msvcp140_1 / _2 / _atomic_wait / _codecvt_ids | none statically; loaded via msvcp140's import table at runtime | same RUNTIME donor list | Wine donor | same | covered |
| concrt140.dll | xgameFinal.exe (4), Proteus.exe (4), msvcp140 (12) — 20 distinct names (Concurrency::critical_section/_Condition_variable) | Wine `runtime/concrt140` donor build, same `redistributable()` path | Wine donor | same | covered |
| ucrtbase.dll | none statically (games import api-ms-win-crt-* only) | console system DLL | Console provides | `play-runtime.sh` api-set gates | covered |
| api-ms-win-crt-* (all 11 contract sets seen: console, convert, environment, filesystem, heap, locale, math, multibyte, runtime, stdio, string, time, utility) | every VC-linked binary above | `mlp_probe.h` maps api-ms-win-crt-* to kind="system" → console ucrtbase | Console provides | same | covered |
| vcomp140.dll | none | n/a | none needed; revisit if an owned game links OpenMP | — | no consumer |
| d3dcompiler_47.dll | xgameFinal.exe: `D3DCompile`, `D3DDisassemble`, `D3DGetBlobPart`, `D3DReflect` | **MISSING** — absent from `build-wine-donors.sh`, `deps.pin`, `import_map.json`, `redistributable()`, `wine_gap_routes.h`; `d3d10.cpp` `Compiler::get()` LoadLibrary fails on console | **This slice: Wine donor** (pinned wine-10.0, `dlls/d3dcompiler_47`, PARENTSRC d3dcompiler_43) | new play-runtime mode + /MD fixture calling D3DCompile | implemented in this PR |
| xinput1_3.dll | Proteus.exe, PuyoPuyoTetris2.exe (ordinals 2/3/4/5/7; ppt2 ordinal 5) | `src/xbox-test/uwph/xinput/route.h` (name + ordinal table) | Console route (owned) | xinput tests | covered |
| xinput1_4.dll | none statically | same route exports 1_4 | Console route | same | covered |
| xinput9_1_0.dll | xgameFinal.exe (2), PuyoPuyoTetris2.exe (2) | same route (name-only exports) | Console route | same | covered |
| xaudio2_8/9, dsound.dll | none statically | n/a | audio slice (`research/audio/`) owns dynamic loads | — | no consumer in this slice |

## d3dcompiler_47 donor details

- Pinned wine-10.0 source (existing `deps.pin` sha); builds via
  `src/tools/build-wine-donors.sh` with the same mingw-w64 recipe cache.
- Its import needs (verified by objdump of the built DLL,
  `/tmp/xbn-vcrt/d3dc-full.txt`): kernel32 (HeapAlloc/HeapReAlloc,
  GetCurrentDirectoryA, RaiseException, GetTickCount, CreateFileA/W,
  ReadFile/WriteFile/GetFileSize, CloseHandle, WideCharToMultiByte,
  GetModuleHandleW/GetProcAddress, DisableThreadLibraryCalls,
  Enter/LeaveCriticalSection, IsBadStringPtrA/W), ntdll `_vsnprintf`,
  ucrtbase CRT names — all routed except `IsBadStringPtrA/W` (0 refs in the
  tree before this PR; routed here) — plus 7 `vkd3d_shader_*` symbols from
  `wined3d.dll`, statically linked from Wine's bundled libvkd3d so the donor
  does not pull a wined3d.dll dependency onto the console.
- No binaries committed: the DLL is built at setup time from the pinned
  source; NOTICE gains the Wine/vkd3d LGPL-2.1-or-later entry.

## Honest gaps

- Console status for the new donor: **unverified** (off-device slice). The
  console check to run later is listed in the PR.
- vcomp140, xaudio2_*, dsound have no owned-game static importer; if a game
  dynamically loads them, that is a new finding, not covered here.
- x86 (PE32) binaries are a separate plan (`research/X86-32BIT-PLAN.md`).
