# Win32 gaps

Every Win32 function the target games import that Play does not yet serve, ranked by
whether a real run reached it. Sources are the console launch logs (`MLP MISSING`,
`MLP STUB`, `unresolved static import`, `WXL FAIL`, `GAP: total=`) from
`/tmp/xbn-runs/*/launch_probe.log`, the static import tables of the owned binaries,
and `src/xbox-test/app/wine_gap_catalog.h`. Per-title status is in
[GAMES.md](GAMES.md); what "working" requires is in [PLAYABLE.md](PLAYABLE.md).

Two kinds of entry matter:

- **Reached** — the guest asked for the name while running. These break a title.
- **Imported** — the name is in an import table but no run has asked for it yet. These
  are latent: the first code path that touches them fails.

A third group is listed at the end because it looks like a gap in the logs and is not
one. Do not spend time on those.

The `MLP` probe resolves names without executing guest code, so a `MLP MISSING` line is
the probe's answer, not proof that the game called the function. "Reached" below means
the name was requested during a launch that got as far as mapping and running guest
code; the per-title evidence in [GAMES.md](GAMES.md) says how far each run got.

## Reached at runtime

Ranked by how much of a title's progress depends on it. "Status" names the commit or
PR that closes the entry.

| Rank | Area | DLL!function | Games | Intended behaviour | Status |
|---:|---|---|---|---|---|
| 1 | ntdll | `RtlAddGrowableFunctionTable`, `RtlGrowFunctionTable`, `RtlDeleteGrowableFunctionTable` | Haven 983970, Slime Rancher 433340, Say No! More 1191900, Going Under 1154810, Danganronpa V3 567640 | Forward to the OS `ntdll.dll` exports. The unwinder reads only its own ntdll's table list, so a private registry would accept every registration and never be consulted. Return `STATUS_NOT_SUPPORTED` when the OS has no export. | `5e0b118`, [#101](https://github.com/jobucaldas/xboxnative/pull/101) |
| 2 | user32 | `SetProcessDpiAwarenessContext`, `SetThreadDpiAwarenessContext`, `GetThreadDpiAwarenessContext`, `GetAwarenessFromDpiAwarenessContext`, `AreDpiAwarenessContextsEqual`, `IsValidDpiAwarenessContext` | Fields of Mistria 2142790 | Real implementation over the record backend's single 96-DPI context; no per-monitor DPI virtualization exists. | `b9845eb`, [#98](https://github.com/jobucaldas/xboxnative/pull/98) |
| 3 | user32 | `GetTouchInputInfo`, `CloseTouchInputHandle`, `GetPointerDeviceRects` | Fields of Mistria 2142790 | Documented refusal: the record backend has no touch/pointer source. | `b9845eb`, [#98](https://github.com/jobucaldas/xboxnative/pull/98) |
| 4 | imm32 | `ImmLockIMC`, `ImmUnlockIMC`, `ImmLockIMCC`, `ImmUnlockIMCC`, `ImmDisableTextFrameService` | Halo Wars DE 459220, Fields of Mistria 2142790 | Wine-parity no-context answers: no IME context exists, so locks return null and unlock/disable are no-ops. | `b48aedf`, [#99](https://github.com/jobucaldas/xboxnative/pull/99) |
| 5 | dwmapi | `DwmFlush` | Fields of Mistria 2142790 | Documented refusal (`E_NOTIMPL`): guest presents go to the CoreWindow swapchain, which paces itself; there is no DWM composition queue to flush. | `766c087`, [#102](https://github.com/jobucaldas/xboxnative/pull/102) |
| 6 | mss64 | 19 `AIL_*` sample functions (see below) | Halo Wars DE 459220 | Full Miles Sound System. Too large for one PR; see [Too big for one PR](#too-big-for-one-pr). | **Open** |
| 7 | user32 | `CharNextW`, `CloseWindow`, `DialogBoxIndirectParamA`, `EqualRect`, `GetTopWindow`, `GetWindow`, `MessageBeep`, `SetClassLongPtrA`, `wsprintfW`, `GetUserObjectInformationW`, `SendMessageTimeoutW`, `WaitForInputIdle` | Halo Wars DE 459220, Say No! More 1191900 | Record-backend implementations and Wine-parity refusals. | `b874ca8` |
| 8 | winmm | `PlaySoundA`, `timeSetEvent`, `timeKillEvent`, `waveOutSetVolume` | Puyo Puyo Tetris 2 1259790, Say No! More 1191900, Halo Wars DE 459220 | ANSI→Wide `PlaySoundA`, thread-pool multimedia timers, native `waveOutSetVolume`. | `b874ca8` |
| 9 | gdi32 | `GetTextExtentPoint32W` | Halo Wars DE 459220 | Real text metrics from the record backend's font. | `b874ca8` |
| 10 | shlwapi | `PathFileExistsW` | Say No! More 1191900 | Native path query (no window handles involved). | `b874ca8` |
| 11 | dinput8 | `DirectInput8Create` | Puyo Puyo Tetris 2 1259790 | Wine donor build: keyboard and mouse through the record core, pads through XInput. | `b874ca8` |
| 12 | imm32 | `ImmGetCompositionStringA`, `ImmGetDefaultIMEWnd`, `ImmGetIMEFileNameW`, `ImmGetOpenStatus`, `ImmGetVirtualKey`, `ImmIsIME`, `ImmSetConversionStatus`, `ImmSetOpenStatus`, `ImmSimulateHotKey` | Halo Wars DE 459220, Say No! More 1191900 | Wine-parity no-IME answers. | `b874ca8` |

The only entry still open is the Miles Sound System set in Halo Wars.

## Imported, not yet reached

These are in an owned binary's import table. No run has asked for them, so they are
latent rather than blocking. Each is listed with the route it would take, which is what
decides the work: a `system` route needs no code (the console's own DLL answers), a
donor route needs the donor to export it, and a `stub` route needs a real implementation
or a documented refusal.

| Area | DLL!function | Games | Route today | Note |
|---|---|---|---|---|
| shell32 | `SHCreateDirectoryExW` | Megabonk 3405340 | `stub` | Only `UnityCrashHandler64.exe` imports it, not the game. |
| psapi | `GetModuleFileNameExW`, `EnumProcessModules`, `GetModuleInformation` | Megabonk 3405340, Fields of Mistria 2142790 | `system` | Console export availability under AppContainer is unverified. |
| ntdll | `NtCreateNamedPipeFile`, `NtOpenFile`, `NtReadFile`, `NtWriteFile`, `RtlNtStatusToDosError` | Fields of Mistria 2142790 | `sysx` native | Already served by `sysx_native`; listed for completeness. |
| userenv | `GetUserProfileDirectoryW` | Fields of Mistria 2142790 | `system` | |
| iphlpapi | `GetAdaptersAddresses`, `NotifyAddrChange`, `CancelIPChangeNotify` | Halo Wars DE 459220 | `system` | |
| crypt32 | `CryptUnprotectMemory`, `CertFreeCertificateContext` | Halo Wars DE 459220, Puyo Puyo Tetris 2 1259790 | `system` | |
| oleaut32 | `SysAllocString`, `SysFreeString`, `VariantClear` | Puyo Puyo Tetris 2 1259790 | `system` | |
| ole32 | `CreateStreamOnHGlobal` | Puyo Puyo Tetris 2 1259790 | `system` | |
| dbghelp | `SymLoadModuleEx`, `SymRegisterFunctionEntryCallback64` | Megabonk 3405340 | `sysx` native | Already served; `SYSX NATIVE ... available=1` in the logs. |
| xinput9_1_0 | `XInputGetState`, `XInputSetState` | Megabonk 3405340, Puyo Puyo Tetris 2 1259790 | `xinput` core | Play's WGI emulation. |
| wininet | 8 `Internet*`/`Http*` calls | Megabonk 3405340, Puyo Puyo Tetris 2 1259790 | `system` | |
| normaliz, powrprof, wtsapi32 | `IdnToAscii`, `CallNtPowerInformation`, `WTSRegisterSessionNotification` | Megabonk 3405340, Halo Wars DE 459220 | `system` | |

`msvcp140.dll` and `concrt140.dll` (136 and 8 symbols) are imported by Halo Wars and
Mega Man LC but are served from Wine's C++ runtime donors in `app/out/layout/runtime/`,
so they are not gaps.

## Not gaps

The logs name these, but nothing is missing. Chasing them wastes time.

| What the log says | Why it is not a gap |
|---|---|
| `user32.dll!UnityPluginLoad`, `UnityRenderEvent`, `UnitySetGraphicsDevice`, `UnityRenderingExt*`, `UnitySetEventQueue`, `UnityShaderCompilerExtEvent`, `UnityGetAudioEffectDefinitions`, `UnityPluginUnload` under `user32`, `advapi32`, `kernel32`, `hid`, `setupapi` | Mono probes every loaded module for Unity's native-plugin entry points before falling back. They are not user32/advapi32 exports. |
| `user32.dll!_GetKeyboardLayoutName@12`, `_SetupDiGetClassDevsWW@100`, `RegOpenKeyExWW`, `GetWindowLongWW`, `SetWindowLongPtrWW`, `_SystemParametersInfo@32` | Mono's decorated-name probes. The undecorated `W`/`A` forms resolve. |
| `user32.dll!GetKeyboardLayoutName`, `user32.dll!SystemParametersInfo` (no suffix) | Windows and Wine export only the `A`/`W` forms (`user32.spec`); the bare names do not exist. |
| `ntdll.dll!GetConsoleCP`, `GetConsoleOutputCP`, `GetCurrentPackageId` | `kernel32`/`kernelbase` exports, not ntdll. Play's kernel32 route answers them (`K32T ROUTE ... found=1`). |
| `ntdll.dll!wine_get_version` | A Wine-only export; a correct miss on a real Windows kernel. |
| `setupapi.dll!SetupDiGetClassDevsWW`, `hid.dll!HidD_*`, `version.dll!GetFileVersionInfo*`, `avrt.dll!*`, `shcore.dll!*` | The donors are built and routed (`DWPE ROUTE setupapi.dll!...`); the remaining lines are decorated probes or come from the donor self-test fixture, not a game. |
| `xbtest.exe!*` | The probe app's own module key, not a guest import. |

## Too big for one PR

These are whole subsystems. Each needs an owner area rather than a function-by-function
patch.

| Subsystem | Games | What it needs | Proposed owner |
|---|---|---|---|
| Miles Sound System (`mss64.dll`, 19 `AIL_*` sample calls reached) | Halo Wars DE 459220 | A sample-based mixer: allocate/init sample handles, load buffers, set rate/volume/pan, EOB callbacks, digital latency. Halo Wars delay-imports it from `bink2w64.dll`, so the calls appear only once audio starts. | `audio` |
| OpenGL driver | Fields of Mistria 2142790 | A guest GL implementation over the console's D3D12. Plan already written. | `opengl` — see [research/OPENGL-PLAN.md](../research/OPENGL-PLAN.md), [#94](https://github.com/jobucaldas/xboxnative/pull/94) |
| 32-bit x86 execution | Mega Man Legacy Collection 363440 | PE32 loader plus an x86 execution core; the console cannot run 32-bit code. | `x86` — see [research/X86-32BIT-PLAN.md](../research/X86-32BIT-PLAN.md), [#80](https://github.com/jobucaldas/xboxnative/pull/80) |

## Refreshing this list

From a set of console launch logs:

```sh
# Reached names, minus the probe noise documented above.
grep -h '^MLP \(MISSING\|STUB\) ' /tmp/xbn-runs/*/launch_probe.log |
  sed 's/^MLP [A-Z]* //; s/ route=.*//' | sort -u
```

Re-run the static import pass with `src/tools/xbox/wine_gap.py` over an owned game
directory; it prints `GAP: total=.. missing=.. win32u=..` per title. The catalog it
consults is `src/xbox-test/app/wine_gap_catalog.h`, regenerated by
`src/tools/xbox/gen-gap-routes.py` from `src/tools/xbox/import_map.json`.
