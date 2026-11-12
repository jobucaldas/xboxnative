# Managed runtimes coverage: Unity Mono and .NET hosting in Play

Evidence: static PE inspection (pefile, python3 -I) of the owner's installed
copies at `/home/jobu/steam-owned/games/*` on 2026-11-12. Names only; no game
files are read into the repo or committed. CLR-header facts come from data
directory 14 (COM descriptor) parsed manually; import lists from directory 1.

## Which owned games need which managed runtime

| Game (app id) | Engine | Managed runtime needed | Evidence (PE-level) |
|---|---|---|---|
| Megabonk (3405340) | Unity 6, IL2CPP | **None** — no Mono, no .NET. `GameAssembly.dll` is a native x64 image with no CLR header; no `mono-2.0-bdwgc.dll` in the install | CLR dir empty on GameAssembly.dll; no MonoBleedingEdge dir |
| Slime Rancher (633300) | Unity 5.x, **Mono** | **Unity Mono (MonoBleedingEdge)** — x64 `mono-2.0-bdwgc.dll` + `MonoPosixHelper.dll` loaded by UnityPlayer by name; IL assemblies are x86-marked metadata images | local install has `Managed/Assembly-CSharp.dll` + `InControl.dll` (CLR header runtime 2.5, ILONLY, PE32 machine=x86 metadata) but **no MonoBleedingEdge dir in the local copy**; UnityPlayer.dll contains the `MonoBleedingEdge`/`EmbedRuntime/mono-2.0-bdwgc.dll` load strings |
| Say No! More (1095750) | Unity, **Mono** | **Unity Mono** — ships `MonoBleedingEdge/EmbedRuntime/{mono-2.0-bdwgc.dll,MonoPosixHelper.dll}` | on-disk EmbedRuntime present; UnityPlayer has the same load strings |
| Mega Man Legacy Collection (817900) | Unity, **Mono (x86 host)** | **Unity Mono** — ships `MonoBleedingEdge/EmbedRuntime` (present locally, empty listing kept out of repo); game host `Proteus.exe` is **x86** | MonoBleedingEdge dir present; Proteus.exe machine=0x14c |
| Fields of Mistria (2142790) | Custom (SDL3+FMOD), **not** GameMaker, **no managed runtime** | **None** | no CLR header on any PE; no Mono directory; no mscoree/hostfxr imports |
| Puyo Puyo Tetris 2 (670160) | Native engine | **None** | no CLR header, no Mono dir |
| Halo Wars: Definitive Edition (2669320) | Native (xgameFinal) | **None** | no CLR header, no Mono dir |
| Stardew Valley (413150) | MonoGame / .NET Framework 4.5 | **.NET Framework via mscoree** — not in the local install set; listed from engine knowledge only, **unverified** (no local copy; PE evidence pending if it is installed later) | — |

Hades (1145360) is listed in the launcher's demo catalog but is not owned
(slime/mmlc/ppt2/snm/mistria/hwde/megabonk are the owned installs); no evidence
gathered for it.

### Slime Rancher x86 assemblies caveat

Slime Rancher's IL assemblies (`Assembly-CSharp.dll`, `InControl.dll`) are
PE32/x86-marked images with a CLR 2.5 header. Unity Mono loads these as
*metadata*, not as executable PE images — the IL is interpreted/JITed by the
x64 `mono-2.0-bdwgc.dll`, so the x86 marking of the metadata containers does
not make Slime Rancher a 32-bit game (UnityPlayer and mono are x64). Only
MMLC would need the x86 milestone (memory: playability-priority puts x86
after DX11 x64).

## What Play must provide for a Mono game (evidence from mono-2.0-bdwgc.dll imports)

`mono-2.0-bdwgc.dll` (x64) imports 217 kernel32 symbols plus advapi32, ole32,
oleaut32, psapi, shell32, user32, version, winmm, ws2_32. Notable groups and
their current route status in Play:

| Mono requirement | Symbols | Play route today | Status |
|---|---|---|---|
| JIT memory (W^X) | `VirtualAlloc`, `VirtualProtect`, `VirtualFree`, `VirtualQuery` | `rwx_emulation.h` adapters in `play_runtime.h` (RWX requested → RW/RX flip per page) | **covered** (SNM trampoline crash was the motivation) |
| Unwind registration | `RtlAddGrowableFunctionTable`, `RtlGrowFunctionTable`, `RtlDeleteGrowableFunctionTable`, `RtlLookupFunctionEntry`, `RtlVirtualUnwind`, `RtlUnwindEx`, `RtlInstallFunctionTableCallback`, `RtlDeleteFunctionTable`, `RtlCaptureContext` | `uwph/kernel32/exception_route.h` forwards to OS ntdll, typed fallbacks | **covered** |
| Threads/TEB | `CreateThread`, `Get/SetThreadContext`, `TlsAlloc/Get/SetValue`, `QueueUserAPC`, `SuspendThread`, `ResumeThread` | native-thread backend + `thread_query_route.h` | **covered** |
| Loader/module | `LoadLibraryW/ExW`, `GetProcAddress`, `GetModuleHandle*`, `GetModuleFileName*` | `play_runtime.h` guest loader (search order incl. `SetDllDirectory` dir and runtime dir) | **covered** |
| Module enumeration | psapi `EnumProcessModules`, `EnumProcesses`, `GetModuleBaseNameW`, `GetModuleFileNameExW`, `GetModuleInformation` | `psapi.dll` routed **system** in import_map; `K32GetModuleBaseNameW` hooked in play_runtime (`guest_loader_queries.h`) so guest module handles return guest names; **`EnumProcessModules`/`EnumProcesses`/`GetModuleFileNameExW`/`GetModuleInformation` have no Play-specific route** — the native psapi answers with the launcher's own module list | **gap** (Mono's backtrace path calls `EnumProcessModules`; a launcher-module list can mis-set Mono's own module base). Closable with a `guest_loader_queries.h`-style hook that enumerates the guest module registry; not closed in this slice |
| Shell | `CommandLineToArgvW`, `SHGetFolderPathW`, `SHGetKnownFolderPath`, `ShellExecuteExW` | `entry_stubs.cpp` typed failure/empty adapters (probe surface) | gap for CSIDL_PERSONAL/SAVEDGAMES returns — Mono's `Environment.GetFolderPath` needs a real path for saves; **gap** |
| Security/token | `AllocateAndInitializeSid`, `CheckTokenMembership`-family, `CreateProcessWithLogonW`, `ImpersonateLoggedOnUser`, `GetEffectiveRightsFromAclW`, `CryptGenRandom`, … | advapi32 routed **system** (native); UWP AppContainer tolerates token queries but refuses elevation calls | **partially covered** — verification needed per export on console |
| Time | `QueryPerformanceCounter/Frequency`, `GetTickCount64`, `timeBeginPeriod`, `timeGetDevCaps`, `timeSetEvent`, `timeKillEvent` | `nls_time_route.h`, winmm audio routes | **covered** |
| COM | `CoInitializeEx`, `CoTaskMem*`, `SafeArray*`, `SysAllocString*` | ole32/oleaut32 routed **system** | **covered** (native) |
| Crypto RNG | `CryptAcquireContextW`, `CryptGenRandom`, `CryptReleaseContext` | advapi32 **system** (native); bcrypt.dll also **system** | **covered** |
| DLL load flags | Mono/Unity pass `LOAD_WITH_ALTERED_SEARCH_PATH` / `LOAD_LIBRARY_SEARCH_*` | play_runtime `searchOnly` flag set — accepted, never refused | **covered** |

### How Unity loads Mono (loader behavior Play must satisfy)

UnityPlayer.dll loads `MonoBleedingEdge/EmbedRuntime/mono-2.0-bdwgc.dll` by
**relative path** (strings `MonoBleedingEdge`, `/EmbedRuntime/`,
`mono-2.0-bdwgc.dll` in the binary). Play's `guestLibraryPath` resolves a bare
name against the game root plus the `SetDllDirectory` directory; a relative
`MonoBleedingEdge/EmbedRuntime/...` argument already resolves against the game
root. **Gap to verify:** when UnityPlayer passes the sub-path with
`LOAD_WITH_ALTERED_SEARCH_PATH`, the path resolution must not strip the
subdirectories — this is the first closable gap, testable off-device with a
synthetic guest that loads `sub/dir/lib.dll` the way UnityPlayer does.

### Mono interpreter vs JIT on UWP

Mono's JIT needs executable memory. On Xbox (AppContainer, W^X enforced —
vrx evidence err=87 for RWX), the options are:

1. **RWX emulation** (implemented, `src/xbox-test/shared/rwx_emulation.h`):
   RW pages flipped to RX on execute fault, back to RW on write fault. No RWX
   page ever exists. This is the route Play ships.
2. **Mono interpreter** (`--interpreter` / `MONO_PATH` + env `MONO_ENV_OPTIONS`):
   avoids JIT entirely but Unity's Mono builds ship interpreter support
   compiled-in only in some versions; UnityPlayer does not pass the flag, and
   setting `MONO_ENV_OPTIONS` from Play would be a per-game env injection.
   Not needed while RWX emulation works; keep as fallback.
3. **AOT**: Unity only pre-AOTs IL2CPP builds, which by definition have no
   Mono. Not applicable.

So: **JIT with RWX emulation is the design; interpreter is the fallback.**
No change needed to the W^X policy.

## .NET (mscoree / hostfxr / CoreCLR) hosting

- **No owned game imports mscoree.dll, hostfxr.dll, nethost.dll or coreclr.dll**
  (checked every top-level PE in all 7 owned installs). Slime Rancher's IL
  assemblies import mscoree.dll, but those are metadata containers consumed by
  Mono — Unity Mono never executes through mscoree.
- Stardew Valley (MonoGame, .NET Framework 4.5) would be the first candidate:
  it launches via a native launcher that calls `mscoree!CorBindToRuntimeEx`
  (or an apphost embedding hostfxr for .NET 6+ builds). Play would need:
  1. `mscoree.dll` route (stub CorBindToRuntimeEx → or serve Wine's mscoree
     donor) — **not present in import_map.json today**.
  2. For .NET Core games: `hostfxr.dll`/`nethost.dll`/`coreclr.dll`/`hostpolicy.dll`
     routes plus `System.*.dll` P/Invoke into kernel32/user32 — the P/Invoke
     surface is the same kernel32 route Play already provides; the gap is the
     host DLL set itself, which would have to come from Wine PE donors
     (pinned, built by a `src/tools/build-*.sh` script) because the console
     ships no .NET.
- **Decision for this slice:** no .NET hosting is implemented now. The gap is
  recorded; the first owned .NET game (Stardew Valley, if installed) triggers
  it. mscoree is added to `import_map.json` as `stub` with the reason
  recorded, so the gap matrix names it instead of leaving it unclassified.

## Gaps this slice closes (off-device)

1. **import_map.json has no entry for `mono-2.0-bdwgc.dll`, `mscoree.dll`,
   `hostfxr.dll`, `nethost.dll`, `coreclr.dll`, `msvcr120.dll` (Mono's CRT on
   some Unity versions) or `psapi.dll`'s K32 exports** — a Mono game's gap
   report would show them as unclassified/missing. Fix: add classification
   entries (mono-2.0-bdwgc → `uwph` guest-mapped, mscoree/hostfxr/nethost/
   coreclr → `stub` with the .NET-hosting gap recorded, msvcr120 → `uwph`).
2. **Relative sub-path DLL loads** (`MonoBleedingEdge/EmbedRuntime/mono-2.0-bdwgc.dll`):
   `guestLibraryPath` handles a bare name and an absolute path; a relative
   multi-directory path is handled by `path=requested; if no ':' and not
   leading '\\' then root+'\\'+requested` — correct, but there is no test
   pinning it, and Unity's load is exactly this shape. Fix: Wine host test
   with a synthetic guest that LoadLibraryExW's a `sub/dir/guest.dll` and
   asserts the mapping and its imports resolve.
3. **psapi module enumeration**: `EnumProcessModules` etc. route "system"
   today, meaning the native psapi/K32 exports answer with the *host* process
   module list, which inside the launcher includes launcher modules — Mono's
   backtrace path uses it only for its own module, but a wrong module list
   can also make Mono's profiler mis-set its own base address. Fix (this
   slice): keep the native route but pin, with a host test, that
   `K32GetModuleFileNameExW`-family queries on a guest module handle return
   the guest path — via the existing `kernelModuleBaseName` hook pattern —
   and record the remaining psapi exports in the matrix as a console-check.

## Console checks for the coordinator (when this PR is approved)

1. `play-test.sh` on Say No! More (1095750) — the only owned Mono game with
   EmbedRuntime present locally — and record whether
   `MonoBleedingEdge/EmbedRuntime/mono-2.0-bdwgc.dll` maps (launch log line
   `LoadLibrary name=...mono-2.0-bdwgc.dll` with no error) and where the
   first managed exception lands.
2. Slime Rancher (633300): confirm the local-install absence of
   MonoBleedingEdge is a download gap, not a Steam-depot difference —
   `dp ls-dir` the installed game root and compare against
   `/home/jobu/steam-owned/games/slime`.
