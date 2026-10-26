# Wine as a base for "any Windows game boots" on Xbox (uwph) — research report

Date: 2026-10-26 (America/Sao_Paulo). Author: Grok executor (research only; no product code changed, no console used).
Requested by João in pt132u: "32 bit should work too though / … can't we use [wine] as a base? Or at least to know what might be missing because any windows game should boot".

All coverage numbers come from `research/wine-coverage/analyze.py` + `dll-table.py` run on **Wine 11.0 source** (`/workspace/research/wine`) and the Debian **Wine 10** PE builtins on the box. They are produced by a regex/call-graph walk of the C sources, **not a compiler**: treat percentages as ±5 points and the win32u syscall count as an upper bound. Everything marked (UNVERIFIED) is reasoning, not measurement.

## 0. TL;DR

1. **Do not run Wine's user32/win32u/ntdll as the runtime.** Their real work sits in Unix-side code (win32u.so ≈96K lines + freetype/fontconfig, ntdll.so ≈55K lines, wineserver ≈50K lines / 306 requests) that assumes POSIX, signals, fds, AF_UNIX. uwph has none of that. Porting it = rewriting it.
2. **Do use Wine as (a) the authoritative checklist of what each Win32 symbol needs underneath (`*.spec` + call graph), (b) a donor of *PE-only* DLL code, (c) the model for 32-bit (new WoW64 splits "thunk at the syscall boundary", which is a ~760-function surface, instead of thunking the whole Win32 API).**
3. Recommendation = **hybrid (c)**: keep the current stack (real system DLLs + our wxl mapper + mlp router + uwph), fill the gap with a thin CoreWindow-backed user32/gdi32 layer written to Wine's semantics, borrow Wine PE code where it only needs Nt*.
4. Megabonk's 391 distinct imports: all 391 exist in Wine; 35% are pure PE code, 38% need only ntdll Nt* syscalls, **27% need win32u (NtUser/NtGdi)**. The 188 symbols the console does not already satisfy are 56% win32u-bound, so **user32/win32u semantics are the keystone**, which matches the repo's PLAN A/B.
5. **32-bit**: the console CPU is x86-64, so a 32-bit PE can in principle run natively through `ljmp 0x23` (compat mode) with *no emulator*, but whether Xbox's kernel allows it, and how to provide FS/TEB32, is unknown: a 1-lane console spike answers it. Fallback: in-process x86 interpreter/JIT (Boxedwine core, GPL-2) with API-boundary break-out à la Hangover. Boxedwine is already known to run on Xbox Dev Mode (32/16-bit, "lag/stutter"), so 32-bit *is possible* on this hardware. **Bigger blocker than the CPU: UWP has no D3D9/OpenGL**, and Spacewar/LIMBO/Undertale-class 32-bit games are D3D9/GL.

---

## A. Wine as a base

### A1. What of Wine runs as-is on uwph (PE-only) vs needs the Unix side

**wgap source-audit correction:** The table does not prove that any listed Wine
DLL runs unchanged on uwph today. Its closure excludes delay imports; setupapi's
Makefile.in explicitly delay-imports user32, comdlg32, shell32 and ole32. Even a
PE-only HID implementation needs a real device I/O backend. Treat donor readiness
as unverified until exercised through a real loader and the console.

Wine's split (verified in source): every builtin DLL is a PE; a few have a `.so` "unixlib" reached through `__wine_unix_call_dispatcher(handle, code, args)` (`WINE_UNIX_CALL` sites). Everything else only needs ntdll syscalls (`Nt*`, ~200 distinct used by Megabonk's closure, see `required-syscalls.csv`) and, for window/GDI, win32u syscalls (`NtUser*`/`NtGdi*`). Under uwph, `Nt*` can be served by our host; *the unixlib `.so` functions cannot be reused* (ELF/POSIX) and must be re-implemented behind the same opcode tables.

Full table: `research/wine-coverage/wine-dll-table.csv` (62 DLLs; columns: verdict, has_unixlib, WINE_UNIX_CALL sites, direct Nt/win32u imports, static import closure). Reading guide: `PE_ONLY` = no unixlib, no direct syscall import in the PE; "closure" = transitive static imports that are unix-side or window-system DLLs.

| Class | DLLs (Wine 10 PE builtins on the box / Wine 11 source) | Meaning for uwph |
|---|---|---|
| **UNIX_SIDE_REQUIRED** | ntdll (8 unix-call sites + ntdll.so), win32u (syscall-stub PE; real code in win32u.so), opengl32, dwrite (freetype), ws2_32 (sockets), bcrypt, crypt32, secur32 | Need a uwph/system replacement. We already route ws2_32/bcrypt to the **real system DLLs**, so these cost nothing. win32u/opengl32/dwrite are the real work |
| **PE_ONLY candidates (incomplete static closure)** | kernel32/kernelbase (need 28/131 Nt* imports only), advapi32, sechost, rpcrt4, msvcrt, ucrtbase, msvcp140, vcruntime140, hid, setupapi (3 Nt), version, shcore, avrt, psapi, dbghelp, x3daudio1_7, iphlpapi (+ws2_32) | **Unverified donor candidates:** Nt* export coverage alone does not establish initialization, delay/runtime dependencies, or device support. These are the cheap donors (hid, setupapi, version, shcore, avrt, x3daudio, CRTs). On console most of them are already real system DLLs, so Wine's copy is only a fallback |
| **PE_ONLY but pulls user32/win32u in closure** | winmm, dsound, dinput/dinput8, xinput1_3/1_4, xaudio2_7/2_9 (FAudio), xactengine, mmdevapi, shell32, shlwapi, comctl32, comdlg32, oleaut32, ole32, combase, gdiplus, windowscodecs, uxtheme, dwmapi, winhttp, wininet, mfplat, imm32 | Code is portable but **loads Wine's user32/gdi32 (hence win32u) as imports**. They only become donors once a user32/gdi32 exists (ours must export the same symbols Wine's PEs import) |
| **PE_ONLY, graphics** | d3d9, d3d11, dxgi, d3d12, wined3d, d3dcompiler_47 | Pull opengl32 → unix GL. Pointless on Xbox for d3d11/dxgi/d3d12 (real system DLLs are better, per PLAN). **wined3d is the only Wine path for D3D9 (needs OpenGL, which UWP lacks)** |

Key finding: **almost everything above "kernel32/ntdll level" has win32u in its static closure** (ole32, winmm, dinput, xaudio2, shell32 …). That is why win32u is the single keystone; if we provide a compatible user32+gdi32 export surface, most of Wine's PE DLLs become usable.

### A2. Megabonk imports, classified (per import)

**u32c source-audit correction (2026-10-26):** Independent parsing of all five
original top-level PEs confirms 628 direct+delay DLL/symbol pairs and **121 user32
pairs**, ten more than the historical CSV's 111. The historical list omits
AdjustWindowRect, CreateIconFromResourceEx, DialogBoxParamA, GetDlgItem, GetIconInfo,
InflateRect, LoadImageA, LookupIconIdFromDirectoryEx, SendDlgItemMessageA and UnionRect.
The CSV's “20 PE_PURE user32” is not a verified portable subset: Wine win.c's
GetWindowLongPtrW explicitly calls NtUserGetWindowLongPtrW; ClientToScreen and
ScreenToClient also call NtUser. sysparams.c forwards GetSystemMetrics and monitor
queries to NtUser. DefWindowProcW uses NtUserMessageCall. Device notifications in
input.c call I_ScRegisterDeviceNotification/I_ScUnregisterDeviceNotification with
callbacks and require a backend. Rectangle routines in uitools.c are pure geometry.
Acceptance must count actual implemented routes against the complete 121-symbol
user32 set, and must not count notification placeholders or export presence as
working notification delivery. Independently derived list: u32c/megabonk-real-imports.csv.

**2026-10-26 lane/wgap correction (source and PE audit):** The 391-row CSV is a
subset extracted from a log, not the complete unique import set. Direct imports of
`baselib.dll`, `UnityPlayer.dll`, and `Megabonk.exe` contain 512 unique DLL/symbol
pairs; the archived CSV omits 121 (including system-routed symbols). Scanning all
top-level EXE/DLLs also includes GameAssembly and UnityCrashHandler. Therefore the
391 total and percentages below describe only the historical subset, not the game.
Do not use that subset as an exact full-game acceptance oracle.
The complete top-level direct+delay set is 628 pairs, independently confirmed by
llvm-readobj-19 as well as pefile. Spacewar/LIMBO/Undertale sets contain 136/256/467
pairs, also independently cross-checked. wgap launcher 0.5.1.1 on the real Xbox
prints GAP 628/458/120 for Megabonk and 136/107/18 for Spacewar, both with zero
unknown catalog entries. Here missing counts non-system **policy** routes except
supplied game modules; it is not observed GetProcAddress failure. These are
non-executing import diagnostics through the real Play path, not game execution.
The original 391-row CSV is retained; exact equality acceptance remains failing.

`opengl32/wgl.c` calls `UNIX_CALL`, defined in `opengl32/unixlib.h` as
`WINE_UNIX_CALL`; analyze.py detects neither that wrapper nor generated GL dispatch.
For example wglGetProcAddress is falsely PE_PURE in the CSV. The product tool
recognizes this wrapper and reports PE_NEEDS_UNIXLIB separately. A missing graph
body is GRAPH_UNRESOLVED, never proof of PE purity. The graph still cannot prove
indirect/runtime dependency closure. No quantitative ±5-point error bound has been
measured; the earlier bound is unsupported.

A1's “can run today unchanged” statement is conditional speculation, not a tested
portability result. `setupapi/Makefile.in` has DELAYIMPORTS including user32,
comdlg32, shell32 and ole32, so its “ntdll only” closure claim excludes delay loads.
`hid/hidd.c` uses DeviceIoControl through sync_ioctl, requiring a working HID device
backend beyond exporting Nt functions. No Xbox experiment was used for this audit.

Historical subset findings (retained below for provenance):

Input: the 391 unique (dll, symbol) imports of `Megabonk.exe`, `UnityPlayer.dll`, `baselib.dll` etc. (`runs/gdet/play-3405340/launch_probe.log`) plus console routing evidence (`runs/stubs/native-discovery-console.log`: 368 ROUTE, 121 MISSING, 67 STUB; **the old probe's "486 MISSING" is an artifact** — real kernel32 imports resolve to kernelbase/api-ms-win-core-rtlsupport on the console).
Classification per import: resolved Wine's `*.spec` (incl. forwarders) → function → call graph over Wine's C sources → set of `Nt*` / `NtUser*` / `NtGdi*` / `__wine_unix_call` reached.

* CSV: `research/wine-coverage/megabonk-imports-vs-wine.csv` (391 rows; columns `dll,symbol,console_route,wine_class,wine_impl_dll,wine_c_name,verdict,n_nt,n_win32u,unixlib_call,nt_syscalls,win32u_syscalls`)
* `summary-by-dll.csv`, `required-syscalls.csv` (252 rows; top: NtWaitForSingleObject, NtAlertThreadByThreadId, NtReleaseSemaphore, NtAllocateVirtualMemory…)

| verdict | all 391 imports | the 188 the console does NOT already satisfy (121 MISSING + 67 STUB) |
|---|---|---|
| PE_PURE (Wine implements it with no syscall) | 137 (35%) | 59 (31%) |
| PE_NEEDS_NT (Wine PE code + ntdll Nt*) | 148 (38%) | 23 (12%) |
| PE_NEEDS_WIN32U (NtUser/NtGdi) | 106 (27%) | **106 (56%)** |
| not in Wine / stub-only / unresolved | 0 | 0 |

**PE-only coverage (no win32u, no unixlib) = 73% overall, 44% of the actual gap.** The remaining gap is by DLL: user32 111 (20 pure, 91 need win32u), kernel32 10 (Toolhelp32 ×3, CreateWaitableTimerA, GetComputerNameW, GetStartupInfoA, GetSystemPowerStatus, SetDllDirectoryW, SetThreadAffinityMask, VerifyVersionInfoW — all need only Nt*), plus winmm (23), hid (14), imm32 (8), opengl32 (6), setupapi (5), shell32 (5), gdi32 (4), shlwapi (2) that are currently STUB.
The gap's syscall surface: 56 distinct `Nt*` and ≤172 `NtUser*/NtGdi*` (upper bound, function-pointer heuristic over-approximates).

What this means: ~⅔ of the symbols that matter are *user32 window/message/input semantics*. Wine tells us **exactly what each does underneath** (e.g. which `NtUser*` call, which wineserver request); it cannot give us the implementation, because the implementation is `win32u.so`+wineserver.

Caveats: (1) static analysis ignores runtime-only paths (COM, delay-loads, `GetProcAddress` at runtime; Unity does call LoadLibrary/GetProcAddress for some); (2) Wine 11 source vs Wine 10 binaries; (3) the call graph is regex-based, hence the ± note above.

### A3. Why not run Wine's real stack on uwph (option b), concretely
* win32u.so: window management, message queues, GDI, input, clipboard, fonts, ~96K lines, all wineserver clients (`wine_server_call` ×306 request types). wineserver itself is 50K lines of POSIX C (sockets, fds, poll, signals).
* ntdll.so: signal-based SEH, `mmap`-style VM (we have W^X `…FromApp`), thread creation via pthread, `fork`-less but fd-heavy file I/O.
* Earlier lane research (`x64-run/LANES.md` §2) *did* recommend "Wine's loader + builtin PE set + a new uwph unixlib backend"; its own §3 flagged that as "the core unverified assumption". PLAN-0600 later moved to our mapper+router instead and delivered more: real-DLL routing, W^X mapper, TLS, HID stub, triangle on console (smoke 8/8 PASS at 06:30). That decision holds up under this analysis.

---

## B. Strategy

| Option | What | Effort (UNVERIFIED estimates) | Risk | Verdict |
|---|---|---|---|---|
| (a) own mapper + stubs only (today's path) | Keep wxl+mlp; implement user32 & friends by hand | per-API cost; user32 111 imports + long tail per game | Whack-a-mole per game; no definition of "done" | Necessary core but not sufficient alone |
| (b) real Wine PE DLLs, uwph as the "Unix side" | Load Wine ntdll/kernel32/user32/win32u PEs; implement `__wine_unix_call` + wineserver in-process | Very large: win32u unix side (~96K) + wineserver (~50K) + ntdll unix (~55K) re-hosted on WinRT | Highest; every Wine upgrade is a merge; loader semantics already fixed by Wine but replaced by the mapper work already done | **No** |
| (c) hybrid | Real system DLLs where Xbox has them (kernelbase, d3d11/12, dxgi, ws2, bcrypt, ole32…). Our CoreWindow-backed user32/gdi32/winmm/imm32/shell32/hid/setupapi written **to Wine's observable semantics** using Wine's specs as checklist. Pull Wine PE-only DLLs (hid, setupapi, version, shcore, FAudio-based xaudio2, CRTs, d3dcompiler/vkd3d-shader if needed) as donors | Medium; incremental; each step is testable with a game | Medium; main unknowns = window-system semantics under one CoreWindow, D3D9/GL absence | **Recommended** |

### Recommended order
1. Make the gap measurable for **every** game, not only Megabonk (item 40 below).
2. Close the cheap tail: PE_PURE + Nt-only symbols (59+23 of the 188).
3. user32 window/message/input core on CoreWindow (the 106). This is what items 37/28 and "Unity first Win32/D3D11 calls" already need.
4. Donor DLLs once user32/gdi32 export surface exists (winmm, imm32, shell32, xaudio2 via FAudio, dinput).
5. 32-bit track in parallel (feasibility spike first, section C). 
6. D3D9→D3D11 decision (gates most older 32-bit games).

### What already exists in the repo / earlier lanes proved
* wxl mapper (W^X via `VirtualAllocFromApp`/`VirtualProtectFromApp`, zero RWX), mlp import router (system/uwph/stub per `tools/xbox/import_map.json`; `mlp_routes.h` generated), uwph host private ABI v1, TLS lane (tls/tlm), HID stub 20/23, native D3D11 triangle (d3tri) 300 presents hr=0 on the Series S, smoke-all 8 PASS (BACKLOG watcher 06:30).
* smap lane: pinned steamclient map (340 entries validated, fail-closed validator); **Megabonk reached its main menu under Wine on the box** (BACKLOG item 5/16 DONE). Per the delegated brief: wine64 loader runs a 64-bit PE and Valheim server booted under wine64 — I found no artefact for the Valheim claim in /workspace notes, so I cite it as reported, not re-verified.
* Both prove the *game* side is fine under a full Wine; the open problem is only our substitute for Wine's Unix half.
* Prior art in /workspace/ref: dolphin-uwp, RetroArch, XBSX2, DuckStation-uwp, UnleashedRecomp-uwp (JIT/W^X on Xbox UWP is established practice), Boxedwine-uwp-upstream and Boxedwine-xbox-64.

### Graphics/API gap (cross-cutting, not Wine-specific)
Xbox UWP offers **D3D11/D3D12 only** (Microsoft Learn "Map DirectX 9 features to DirectX 11 APIs — UWP applications": UWP apps cannot use d3d9; https://learn.microsoft.com/en-us/windows/uwp/gaming/feature-mapping). D3D9On12 is a desktop component (https://github.com/microsoft/D3D9On12), DxWrapper only redirects desktop d3d9 (https://github.com/elishacloud/dxwrapper). Wine's d3d9 → wined3d needs OpenGL/Vulkan; no GL/Vulkan in UWP. So **D3D9 and OpenGL games need a translation layer we own or port** (candidates: write a small D3D9→D3D11 shim for the fixed-function/SM3 subset; DXVK's D3D9 front-end is Vulkan-bound; ANGLE gives GLES-over-D3D11 but wined3d wants desktop GL — all UNVERIFIED, needs its own spike, item 48). D3D11/D3D12 games (Megabonk/Unity, most 64-bit titles) are unaffected.

---

## C. 32-bit

### C1. Does UWP/Xbox block native 32-bit?
* Microsoft: Xbox no longer supports x86 app *development or submission* (https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/frequently-asked-questions, "what's new" page); UWP-on-Xbox builds target x64. That is a **policy/SDK statement**, not a documented hardware ban. The CPU is x86-64 so it executes x86 instructions.
* No source found showing a 32-bit *process* running on Xbox UWP. Whether the Xbox OS even has `wow64.dll`/`SysWOW64` for UWP is **unknown** (UNVERIFIED; a packaged-load failure establishes only that the loader could not resolve that DLL for this package; it does not establish OS-wide absence).
* Proof that 32-bit Windows code *can* run in an Xbox UWP: **Boxedwine on Xbox Dev Mode** ("BoxedWine is Now on Xbox Dev Mode", https://xbdev.store/news/9/, 2024-11-06; runs 32/16-bit .exe/.bat/.msi, "might see lag or stuttering", tuning frame skip, GDI or OpenGL renderer). Repo: https://github.com/worleydl/Boxedwine-uwp (UNSTABLE EXPERIMENTAL), upstream issue https://github.com/danoon2/Boxedwine/issues/5 ("port to UWP … broken OpenGL/window handling"). It is an *emulated* x86, not native.

### C2. Options
| Option | How | Pros | Cons / unknowns |
|---|---|---|---|
| **1. Native far-jump (Heaven's Gate)** | In our x64 process: map a PE32 with our mapper, allocate it below 4 GB, `ljmp 0x23:eip`; 32-bit stubs jump back `ljmp 0x33:` into thunk code that calls our 64-bit router | **Native speed, no emulator, smallest code**. Desktop observations do not establish selector availability or compatibility transitions in Xbox AppContainer; Wine obtains selectors from actual thread context (ReWolf, https://github.com/rwfpl/rewolf-wow64ext) | (i) Does Xbox's NT kernel let a non-WoW64 AppContainer thread run compat mode? (ii) **FS/TEB32**: 32-bit CRT/SEH read `fs:[0]/[0x18]/[0x30]`; for non-WoW64 threads the kernel does not maintain a TEB32 (UNVERIFIED) → need binary-patching or a different FS strategy. (iii) Exceptions/APC/callbacks across modes are fragile, CFG bitmaps on Win10 (Ionescu, SentinelOne write-ups). (iv) Allocation <4 GB via `VirtualAllocFromApp` (probably fine, UNVERIFIED). (v) W^X for 32-bit code identical to now. My guess: ≤30% chance it works end-to-end unmodified |
| **2. Wine new WoW64 (Wine 10/11)** | 32-bit PE DLLs + `wow64.dll`/`wow64win.dll`/`wow64cpu.dll`: the thunk surface is the **syscall surface** (258 wow64_ NT thunks in `wow64/*.c`, ~503 in `wow64win/*.c`, 8.3K lines in wow64win) not the whole Win32 API. Wine 11 implements this model; loader naming must be checked in loader/Makefile.in and configure.ac, not inferred from dlls/wow64*. This does not imply UWP compatibility | Real, maintained mapping of 32-bit syscalls to 64-bit; i386 PE DLLs are ordinary PEs; reusable for any CPU backend | `wow64cpu.dll` uses far jumps through `cs32_sel`; on Linux Wine gets cs=0x23 and **an FS selector from `set_thread_area`/LDT** (`ntdll/unix/signal_x86_64.c`: `cs32_sel = 0x23; fs32_sel = alloc_fs_sel(...)`) — exactly the piece Windows user mode can't do; on Windows the kernel provides it only to real WoW64 threads. So Wine WoW64 needs the *same unknowns* as option 1 for native x86, **or** a replacement `wow64cpu` backed by an emulator |
| **3. Emulated x86 CPU, break out at the API boundary** | Replace `wow64cpu.dll` (or our own thunk layer) with an x86 interpreter/JIT; 32-bit app code is emulated, **every API/syscall runs natively on the 64-bit side**. Precedent: Hangover (https://github.com/AndreRH/hangover) plugs `wowbox64.dll` (Box64), `libwow64fex.dll` (FEX), or `wow64cpu.dll` into Wine WoW64 via `HODLL`, "everything Unix related is never emulated"; FEX docs: Windows FEX modules "purely handle CPU emulation" (https://wiki.fex-emu.com/index.php/Development:ARM64EC) | Works regardless of what Xbox's kernel allows; our JIT-capable UWP can host a JIT (codeGeneration capability + `VirtualProtectFromApp`; Dolphin/RetroArch Xbox prove it: https://github.com/dolphin-emu/dolphin/pull/11339, https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations) | **Box64/FEX dynarecs target ARM64/RISC-V/LoongArch (Hangover COMPILE: `-DARM_DYNAREC=ON`), not an x86 host** (UNVERIFIED for very recent versions): the Xbox host is x86-64 so we'd need an x86→x86 engine: Boxedwine's CPU core (interpreter "normal", plus JIT for x86/x64/ARMv8 hosts; https://github.com/danoon2/Boxedwine/blob/master/docs/CPUemulation.md; **GPL-2**, licensing implication for us) or QEMU TCG or a purpose-built simple interpreter |
| **4. Full Boxedwine as a fallback** | Boxedwine fork already in /workspace/Boxedwine-xbox-64 / Boxedwine-uwp-upstream: emulated Linux + 32-bit Wine + wined3d(OpenGL) | Exists, runs on Xbox | Heaviest, emulates the whole OS, GL-only graphics, speed "lag/stutter"; not our architecture, but it's the **known-works** baseline for "does a 32-bit game boot at all" |

### C3. Performance expectations (estimates, not measurements)
* Boxedwine's own benchmark (`docs/Performance.md`, Wine 10/11 era): Quake II 800×600 *software renderer* timedemo on an i7-14700: 57 fps (68.7 with Wine 11) for the 32-bit guest build vs 73–99 fps for 64-bit on the same host; ARMv8 hosts 57–84 fps. That's the **JIT** class: guest 32-bit CPU-bound code costs roughly 1.3–2× native on a fast desktop CPU. Series S cores are slower (≈3.4 GHz Zen 2 vs i7-14700), so expect Quake-II-class software rendering to land in the 20–40 fps range **if a JIT is used (UNVERIFIED extrapolation)**.
* **Interpreter-only** is the slow path (one to two orders of magnitude slower than native; UNVERIFIED but consistent with Boxedwine's own "normal is slow" framing). Verdict by game class:
  * Spacewar (Steamworks SDK sample, D3D9, trivial CPU): fine on either; blocked by D3D9.
  * LIMBO (2010, D3D9, SM3, modest CPU, Steam page: Windows w/ DirectX 9.0c): **JIT needed**; interpreter risky; blocked by D3D9.
  * Undertale (GameMaker, 2D, low CPU): fine with JIT; probably OK on interpreter at native 30 fps timing but unproven; GameMaker's renderer is D3D9 for the Windows runner (UNVERIFIED) — blocked by D3D9.
* **Native far-jump**, if it works, would be native speed for all of the above.

### C4. Recommended 32-bit plan
1. 1-day **console spike** (item 46): in a throwaway UWP test identity, allocate low memory, copy a 20-byte x86 routine, `ljmp 0x23`, return with `ljmp 0x33`; also probe `GetSystemWow64DirectoryW`, `IsWow64Process`, `NtCurrentTeb()->WowTebOffset`, read segment selectors (CS/DS/FS/GS values). Record the exact exception or success.
2. If it works: PE32 mapper mode + a 32→64 API thunk layer *generated from Wine's `wow64`/`wow64win` structures* (and kernel32/user32 semantics shared with the 64-bit path via mlp).
3. If it fails: embed an x86 engine behind the same thunk layer (Hangover model). Start with Boxedwine's CPU core as a library (interpreter first, JIT with the W^X rules from the repo's mapper second).
4. In both cases the 32-bit *API* side reuses the 64-bit uwph/mlp work, so **the 64-bit user32/gdi32 gap (section A) is also the 32-bit gap**: no duplicate effort.

---

## D. Next 10 backlog items (ranked; each fits one Codex/Grok lane)

Conventions: appids Megabonk 3405340, Spacewar 480 (free, any account), LIMBO 48000, Undertale 391540 (the last two only if João owns/can install them). `play-test.sh` = `xbox-test/tools/play-test.sh <appid>` (lives in wt/gdet at the moment). Numbers continue after BACKLOG item 39.

| # | Item | Work | Acceptance (all must hold) |
|---|---|---|---|
| 40 | **wine-gap-tool** | Productise `research/wine-coverage/analyze.py` as `tools/xbox/wine_gap.py`: input = any game dir (PE list) + `import_map.json` → CSV (route, wine class, needed Nt*/NtUser*) + one summary line; run on Megabonk, Spacewar, LIMBO, Undertale; wire the summary into `launch_probe` output | Offline: `wine_gap.py` exit 0 and CSV for 3405340 equals `megabonk-imports-vs-wine.csv` ±0 rows; then `play-test.sh 3405340` prints a `GAP: total=… missing=… win32u=…` line matching the CSV; the same for `play-test.sh 480` |
| 41 | **kernel32-nt-tail** | Implement the 10 kernel32 MISSING imports (Toolhelp32, CreateWaitableTimerA, GetComputerNameW, GetStartupInfoA, GetSystemPowerStatus, SetDllDirectoryW, SetThreadAffinityMask, VerifyVersionInfoW) and the other PE_PURE/NT-only symbols (≈82) using uwph/system calls, following Wine's behaviour | `play-test.sh 3405340` launch log shows `kernel32 missing=0`; smoke-all.sh exit 0 |
| 42 | **user32-pure-and-class-core** | user32 PE_PURE 20 symbols + class/atom/window-property table in the host (no CoreWindow yet; windows are records) | `play-test.sh 3405340`: `user32 missing` drops from 111 to ≤ 91; a test PE doing RegisterClassExW/CreateWindowExW/GetWindowLongPtr round-trips (log lines) |
| 43 | **user32-message-pump-corewindow** (builds on swc item 37) | Message queue (GetMessage/PeekMessage/PostMessage/SendMessage/Translate/Dispatch, timers, WM_PAINT/SIZE/ACTIVATE/CLOSE) driven by CoreWindow dispatcher events | `play-test.sh 3405340`: UnityPlayer reaches its first `GetMessage/PeekMessage` and log shows ≥300 pump iterations without the app being killed; screenshot (dp) shows non-black swap chain |
| 44 | **user32-input-cursor-focus** | GetAsyncKeyState/GetKeyState, cursor clip/capture/visibility, focus/foreground, monitor/DisplayConfig metrics, raw input registration → CoreWindow keyboard/pointer/gamepad | `play-test.sh 3405340` log: Unity input init completes, controller button press (one-shot `cmd.txt`) arrives as WM_/XInput event; no synthetic input left running |
| 45 | **donor-wine-pe** | Build/vendor Wine PE-only DLLs that need only Nt*: hid, setupapi, version, shcore, avrt; route in import_map as `wine-pe`; prove the loader runs them with the mapper | smoke-all.sh exit 0; `play-test.sh 3405340`: `hid/setupapi/winmm STUB` count from 67 → ≤ 40 |
| 46 | **x86-compat-mode-spike** (32-bit, console) | Throwaway identity `Joao.XboxSteam.Cs32`: low alloc via `VirtualAllocFromApp`, `ljmp 0x23` round-trip, WoW64 probes, selector dump, FS strategy test | One console log with `CS32-OK` or the exact exception code/selector values; written conclusion in LANE_NOTES: native-32 viable yes/no. Does not need play-test (no game) — gate = `smoke-all.sh` still exit 0 |
| 47 | **pe32-mapper-and-gap** | wxl maps a PE32 (relocs, imports classified by wine_gap, no execution); i386 import list per game | Offline test maps Spacewar and LIMBO PE32 with 0 reloc errors; `play-test.sh 480` and `play-test.sh 48000` print `PE32 mapped, imports=N, gap=M` and the `launch: RESULT` line |
| 48 | **d3d9-layer-decision** | 1-week-sized design+spike: a minimal D3D9→D3D11 path for SM3/fixed-function (own shim vs ports), plus a D3D9 triangle test exe through it | Test exe draws a D3D9 triangle on the console (300 presents, hr=0); `play-test.sh 480` log reaches `Direct3DCreate9` → `CreateDevice` OK |
| 49 | **x86-engine-or-native-32** (after 46) | Either (a) native 32→64 thunk layer from Wine's wow64 tables or (b) Boxedwine CPU core as library behind the thunks; run a hello 32-bit console exe | `play-test.sh 480` shows the 32-bit entry point reached and returns from `main`/`WinMain` with API calls logged; no RWX |

Not in the ten (next wave): xaudio2 via FAudio, winmm/imm32/shell32 donors after user32, DirectInput, Wine-style registry shim (item 39 exists).

### Universal-boot test ladder (cheap → hard; each rung = `play-test.sh <appid>` + a one-line pass condition)
| Rung | Game | Arch / API | Gate (pass condition) | Needs |
|---|---|---|---|---|
| 0 | tiny own PEs (hello, window, d3d11 triangle) | 64-bit, D3D11 | smoke-all.sh | exists (d3tri/swc) |
| 1 | **Megabonk (3405340)** | 64-bit Unity, D3D11 | `play-test.sh 3405340` reaches main menu frame on console (screenshot) | items 41–45 |
| 2 | a small 64-bit D3D11 game João owns (suggest one with Steam API but no anti-cheat; pick from `steam-owned`) | 64-bit D3D11 | same as rung 1 | rung 1 |
| 3 | **Spacewar (480)** | 32-bit, D3D9 | reaches its window and first frame | 46–49 |
| 4 | **LIMBO (48000)** | 32-bit, D3D9 SM3 | main menu | rung 3 + perf (JIT) |
| 5 | **Undertale (391540)** | 32-bit GameMaker (D3D9 per UNVERIFIED) | title screen at ≥ 30 fps | rung 4 |

Keep the order **1 → 2 before 3**: the 64-bit path has no CPU or D3D9 unknowns and exercises the user32 work that the 32-bit path inherits.

---

## Sources
* This repo: `x64-run/PLAN-0600.md`, `REBUILD.md`, `BACKLOG.md`, `LANES.md`; `wt/gdet/LANE_NOTES.md`, `wt/stubs/LANE_NOTES.md`; `runs/stubs/native-discovery-console.log`; `runs/gdet/play-3405340/launch_probe.log`; `tools/xbox/import_map.json`.
* Wine 11.0 source (shallow clone `/workspace/research/wine`; `dlls/*/*.spec`, `dlls/wow64cpu/cpu.c`, `dlls/ntdll/unix/signal_x86_64.c`, `dlls/wow64win/*.c`); Debian Wine 10 PE builtins `/usr/lib/x86_64-linux-gnu/wine/x86_64-windows` (note: `i386-windows` on the box has only zlib1.dll, so no 32-bit builtins are installed).
* Hangover https://github.com/AndreRH/hangover (README, COMPILE.md); FEX https://wiki.fex-emu.com/index.php/Development:ARM64EC.
* Boxedwine https://github.com/danoon2/Boxedwine (docs/CPUemulation.md, docs/Performance.md; GPL-2), https://github.com/worleydl/Boxedwine-uwp, https://xbdev.store/news/9/, issue #5.
* Microsoft Learn: UWP D3D9→D3D11 mapping https://learn.microsoft.com/en-us/windows/uwp/gaming/feature-mapping; Xbox FAQ https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/frequently-asked-questions; capabilities https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations; VirtualProtectFromApp https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotectfromapp.
* Dolphin Xbox UWP JIT: https://github.com/dolphin-emu/dolphin/pull/11339; ReWolf wow64ext: https://github.com/rwfpl/rewolf-wow64ext.
* LIMBO store page https://store.steampowered.com/app/48000/LIMBO/; Spacewar sample https://partner.steamgames.com/doc/sdk/api/example.

## Known limits of this report
Call-graph numbers are approximate (regex, not a compiler). The original research pass ran no console experiments. Item 46 now has actual console evidence below; broader PE32 runtime claims remain unverified. The claim that Wine 11 removed the wine64 loader comes from the earlier research pass, not re-checked in this one. GPL-2 of Boxedwine matters if its core is linked into a shipped binary (João to decide; this is only a research note).


### C5. cs32 source audit (2026-10-26)

Actual source is Wine 11.0 (`VERSION`). `wow64cpu/cpu.c:BTCpuProcessInit`
captures CS64/DS64/FS through RtlCaptureContext and obtains CS32/SS32 through
RtlWow64GetThreadContext. It rejects a syscall transition function above 4 GiB.
It does not establish fixed selectors for arbitrary Windows AppContainer threads.
`ntdll/unix/signal_x86_64.c` sets CS32=0x23 only in the Linux wow_teb path,
then calls alloc_fs_sel; the Apple path allocates a selector differently.
This Unix setup is not a documented UWP FS provisioning strategy.

The actual *.spec interfaces are also broader than a syscall-count shortcut:
`wow64.spec` includes exception preparation, APC delivery, initialization and
system-service dispatch, with numerous explicit stubs. `wow64win.spec` exposes
`sdwhwin32` data. `wow64cpu.spec` includes context get/set/reset and simulation.
Export existence and approximate C-function counts do not prove those lifecycle
and callback requirements are portable or implemented by our host.

CSV caution: wine-coverage combines Wine 10 PE imports with Wine 11 source and
regex call graphs. required-syscalls.csv lists NtWow64GetNativeSystemInformation
and NtWow64IsProcessorFeaturePresent, and the per-import CSV attributes them to
GetNativeSystemInfo/IsProcessorFeaturePresent. ntdll.spec marks BOTH -arch=win32;
they are not requirements for the x64 version of those imports. Kernelbase
memory.c uses RtlGetNativeSystemInformation; process.c uses
RtlIsProcessorFeaturePresent. Branch/architecture-insensitive reachability
should remain a research overestimate, not a generated x64 implementation gate.
No CSV result establishes PE32 runtime, native FS/TEB32, or Xbox compatibility.
The external original CSVs are retained unchanged as research inputs.

Console results and exact evidence for item 46 are recorded in LANE_NOTES.md.

Actual Xbox Series S result (Cs32 0.1.1.0, PID 5616): VirtualAllocFromApp
at 0x10000000 and RW-to-RX protection succeed; an x64 control routine in the
same allocation returns 42. The indirect far jump to selector 0x23 raises
0xc0000005 at RIP=0x10000000, still CS=0033. Exception access=0,
target=0xffffffffffffffff. Selectors: SS/DS/ES/GS=002b, FS=0053.
All three packaged WoW64 DLL probes return error 126, which establishes only
package-loader resolution failure. FS:[18] test is not reached. No supported
FS/TEB32 strategy was established. Native-32 viable: NO for this tested direct
selector-transition approach under documented UWP constraints. This does not
prove absence of x86 hardware capability or every possible OS mechanism.
Exact committed evidence: xbox-test/uwph/host/cs32-console-evidence.log.

### A1 dwpe source audit (2026-10-26)

The item 45 premise "hid, setupapi, version, shcore, avrt need only Nt*" is
incorrect as a complete dependency claim. setupapi/Makefile.in delay-imports
cabinet, shell32, wintrust, ole32, comdlg32 and user32; shcore delay-imports user32
and ole32; avrt imports user32 directly. avrt/main.c contains FIXME stubs returning
fabricated handles (0x12345678) and unconditional success: those are not a
multimedia scheduling implementation. version.spec imports most query functions
from kernelbase and forwards VerLanguageNameA/W to kernel32, requiring real
forwarder resolution. HID hidd.c uses DeviceIoControl and still needs a device
backend. The source *.spec and C bodies, not a PE_ONLY label, determine behavior.

The historical summary-by-dll.csv has 14 HID + 5 setupapi + 23 winmm pairs = 42.
The historical 67 STUB pairs cover additional DLLs. Thus "hid/setupapi/winmm STUB
67" combines two different metrics. The corrected 628-pair import oracle and a
fresh console resolver log must be counted separately. These source corrections
are off-device; they do not claim donor code ran on Xbox.


### A2/C5. pe32 lane re-audit (2026-10-26)

The A2 text saying exact equality remains failing is stale: wgap's authorised
628-pair oracle correction passed equality and console acceptance (see committed
LANE_NOTES). External `summary-by-dll.csv` still totals 391, including 111 user32
pairs, whereas the corrected per-import CSV has 628 pairs and 121 user32 pairs.
Do not combine those summaries as if regenerated from the current oracle.

Source rechecks confirm `opengl32/unixlib.h` wraps WINE_UNIX_CALL as UNIX_CALL;
`setupapi/Makefile.in` delay-imports cabinet/shell32/wintrust/ole32/comdlg32/user32;
`hid/hidd.c` needs DeviceIoControl; `ntdll.spec` restricts
NtWow64GetNativeSystemInformation to win32. Thus CSV classification is a source
heuristic, not an architecture-specific runtime closure or console export test.

Wine `ntdll/loader.c:LdrProcessRelocationBlock` supports ABSOLUTE, HIGH, LOW and
HIGHLOW on i386. The inspection mapper implements these with bounds checks and
rejects other types. Spacewar has HIGHLOW relocations; the installed LIMBO PE32
has no relocation directory and must retain its preferred logical image base.
A data-only inspection image can retain that logical address without reserving
that native address. Neither a successful inspection mapping nor an import list
establishes native i386 execution, FS/TEB32 support or executable API thunks.
Item 46's recorded native transition failure remains applicable. No emulation
or selector transition is added by item 47.

CSV row-count correction: external `required-syscalls.csv` has 251 data rows,
not 252, and `wine-dll-table.csv` has 61 DLL data rows, not 62. The report's
larger counts included the header. Their static classifications remain research
inputs; this correction does not regenerate those historical tables.

### A1 dwpe actual-console donor result (2026-10-26)

Debian Wine 10.0~repack-6 hid/setupapi/version/shcore/avrt PEs all pass strict
import preflight on Xbox Series S: no unresolved direct imports and no failure
placeholders. These binaries have no TLS/load-config/delay-import directories;
the Wine 11 source Makefiles are not a literal inventory of the Wine 10 images.
The launcher runtime mapper initializes each donor (DllMain, registered unwind
records, W^X) and executes focused behavior checks: HID GUID, setupapi set
creation/destruction, version resource read/query and native export forwarder,
shcore command-line parsing, avrt invalid-task error. This establishes actual
donor execution for those checks, not all exports, multimedia scheduling,
device enumeration delivery, or a running game.

Real Play on launcher 0.5.47.2 maps Megabonk's three target images and binds 22
imports into donor exports (HID 14, setupapi 5, version 3). Baseline launcher
0.5.42.1 had 67 STUB lines overall, 42 in hid/setupapi/winmm. The current inherited
native tail already reduces those to 26/7 before donor execution; donor work
must not claim that entire reduction as its own. shcore/avrt have no direct
imports in the corrected 628-pair game oracle, but both execute focused tests.
Wine graph labels such as setupapi PE_NEEDS_UNIXLIB describe reachability through
Wine's own dependencies; native dependency routing can avoid that Unix closure.
They do not forbid every export from executing, nor certify arbitrary exports.
The original CSVs remain unchanged. All eight classification tests pass with
exact-byte historical-oracle proof and a separate current-policy routing check.
