# LANES.md — path from "test frame on console" to "Megabonk's first frame" on the Series S

Written by lane `plan` (worktree /workspace/wt/plan, branch lane/plan), 2026-10-25 ~22:xx BRT.
Supersedes the preliminary version written by the executor at 20:40 BRT (that version's table is
folded in below with real status; its "João idea" addendum is folded into §4, marked DONE).
Runner: `x64-run/lane.sh NAME WORKTREE PROMPTFILE [codex|claude]`, prompts in `x64-run/lanes/*.prompt`.
Rule for every lane below: local worktree + `lane/<id>` branch, NEVER push, commit after each milestone,
run `x64-run/backup.sh` after milestones, wrap console-touching commands in
`flock /workspace/x64-run/CONSOLE.lock <cmd>` (the `xbox-test/dp/*.sh` scripts already take the lock
themselves — don't double-wrap those).

## 0. Current status (ground truth, not aspirational — read before planning more work)

| Piece | State | Evidence |
|---|---|---|
| M1 minimal UWP test app | **DONE, verified on console** | `xbox-test/app/{main.cpp,AppxManifest.xml,build.sh}` (commit `2ab0b569`). Boots as `Grok.XboxTest_...!App`, ignores `-ServerName:`, MTA, AppContainer, static CRT. `runs/m1-boot2.log`: `D3D11CreateDevice hr=0 fl=0xb000 (11_0)`, `CreateSwapChainForCoreWindow` 1920x1080, ~57 fps steady. `runs/m1-shot1.png` confirms the frame on screen. Gamepad: `Windows.Gaming.Input` wired but reported 0 pads in that run (not yet confirmed with a controller present). |
| Device Portal scripts | **DONE, verified on console** | `xbox-test/dp/*.sh` (commit `faf3f247`, hardened `2e8fc9ba`): install/uninstall/launch/kill/ps/get-file/put-file/ls-dir/screenshot/wait-log/user-signin/pull-megabonk. CSRF-token handshake solved (403 fix). |
| Launcher UI + Steam QR login | **DONE for login; downloads are a stub** | `xbox-test/app/main.cpp` + `qrcodegen.cpp` (commits `438d0810`,`8ba34b94`,`59b7f2dc`,`56acf688`), branch history now on `lane/dl` (built from `g/launcher`). QR login works; sort/paging/covers work; refresh-token persistence works (access-token HTTP renewal is refused by Steam for `SteamClient` tokens — worked around by keeping both tokens and falling back to QR on expiry). **The A-button download path is still a stub.** |
| Megabonk game files on console | **Partial** | `/workspace/megabonk-from-console`: 24 of 69 files (248 MB of ~523 MB) pulled from the OLD package's `LocalState\megabonk`. Missing `steam_api64.dll`, `Megabonk_Data/il2cpp_data/Metadata/global-metadata.dat`, `StreamingAssets`, `MonoBleedingEdge`, etc. **Cannot boot as-is.** A real download (lane `dl`) is required before any "first frame" attempt. |
| Steam CM / depot downloader | **In progress (lane `dl`)** | Reference implementation already works OFF-console: `/workspace/steam-login/dl/dl.js` (Node, `steam-user` over the CM websocket) downloaded Megabonk appid `3405340` depot `3405342`, 69 files, 499 MB, all SHA1-verified. Lane `dl` is porting the same CM-websocket protocol to C++/WinRT so the **console itself** does the download through the launcher's A button. Not done yet. |
| Native x64 PE execution inside the UWP AppContainer (Wine/uwph path) | **Not started — this is what §1-§4 below plan** | No code exists yet under any `xbox-test/uwph` or similar path. |

Implication for sequencing: lanes `dl` (downloads) and the uwph/Wine path (this document, §3-§4) are
**independent and can run in parallel** — the uwph work does not need a finished downloader, it needs
*a* native x64 Windows PE to test against, and the already-pulled partial Megabonk files are enough to
get `Megabonk.exe`'s headers and `UnityPlayer.dll`/`GameAssembly.dll` for import analysis (done, §1) and
even for early loader smoke tests (a PE that fails deep in missing data files is still useful to prove
the *loader and DLL layer* work, before data completeness matters). Full Megabonk boot (lane
`megabonk-boot`) needs both tracks finished.

## 1. Import-gap table: what Megabonk's own binaries need vs. what AppContainer allows

Extracted with `llvm-readobj --coff-imports` against the real files in `/workspace/megabonk-from-console`
(raw dump saved at `docs/xbox/imports_raw.txt` in this worktree). Only Megabonk's **own** binaries matter
here (`Megabonk.exe`, `UnityPlayer.dll`, `GameAssembly.dll`, `baselib.dll`, `UnityCrashHandler64.exe`) —
`D3D12Core.dll`'s imports (mostly `api-ms-win-core-*` apiset forwarders) are resolved by the real OS
loading its *own* system component and are not our problem.

**Critical fact that shapes the whole architecture**: the Windows PE loader resolves *every* imported
DLL and *every* imported ordinal/name in a module's import table **at load time**, whether or not the
game ever calls that function at runtime. `-force-d3d11` stops Unity from *calling* OpenGL/D3D12 code
paths, but `UnityPlayer.dll` still **must successfully load** `OPENGL32.dll`, `d3d12.dll`, `dwmapi.dll`,
`SETUPAPI.dll`, `HID.DLL`, `WINHTTP.dll`, etc. at startup or the whole module fails to load. This is why
"just strip the D3D12/GL paths" is not an option — every imported module needs *something* that resolves
its exports, even if most of it is dead code once `-force-d3d11` runs.

| DLL (imported by) | AppContainer verdict | Plan |
|---|---|---|
| `KERNEL32.dll` (all 5 binaries, 256 unique symbols incl. `VirtualAlloc/Protect`, `CreateThread`, `CreateFileW`, **`CreateProcessW`**) | Mostly fine; `VirtualAlloc`/`VirtualProtect` with `PAGE_EXECUTE_READWRITE` are the one hard blocker (AppContainer requires `VirtualAllocFromApp`/`VirtualProtectFromApp`); `CreateProcessW` is flat-out blocked in AppContainer | **Wine builtin, patched**: keep Wine's real `kernel32.dll` PE (already x86_64, already on this box at `/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/kernel32.dll`) but give it a new unixlib backend (`uwph`) whose `VirtualAlloc`/`VirtualProtect` call the `...FromApp` variants, and whose `CreateProcessW` returns a clean failure instead of crashing the caller |
| `ntdll.dll` (used directly only by `D3D12Core.dll`, but Wine's kernel32 calls down into Wine's ntdll internally) | Same W^X constraint at the `Nt*VirtualMemory` level; most of the rest (handles, exceptions, TLS) is plain NT and fine | Wine builtin + `uwph` backend (same component as kernel32 work, §4 lane `uwph-kernel32-ntdll`) |
| `USER32.dll` / implicitly `win32u.dll` (122 symbols: `CreateWindowExW`, `RegisterClassExW`, `DefWindowProcW`, `GetMessageA`/`DispatchMessageA`-style loop, clipboard, `GetDC`/`ReleaseDC`) | **Hard blocker.** True UWP/CoreApplication AppContainer apps get exactly one OS-owned `CoreWindow`, no window station, no classic `HWND` creation, no clipboard without the WinRT `Clipboard` API, no `GetDC` (no HDC without a window station) | Wine builtin `user32.dll`/`win32u.dll` + a **new Wine display driver** that maps every Wine "HWND" onto the single real `CoreWindow` the app already creates (reuse the exact CoreWindow+swapchain code verified working in `xbox-test/app/main.cpp`). Clipboard/`GetDC`/OpenGL-context calls become no-ops/stubs — Megabonk doesn't need them with `-force-d3d11` |
| `d3d11.dll` / `dxgi.dll` / `d3d12.dll` | **Already verified working natively from AppContainer** (REBUILD.md: "D3D11 feature level 11_0 OK; D3D12 OK", and M1 app's own `CreateSwapChainForCoreWindow` success) | **Do NOT route through Wine's wined3d.** Resolve these imports straight to Xbox's real system `d3d11.dll`/`dxgi.dll`/`d3d12.dll`. This is the single biggest scope-cut vs. the original REBUILD.md sketch — no wined3d GL/Vulkan→D3D11 translation backend needs to be written at all |
| `bcrypt.dll`, `CRYPT32.dll`, `ole32.dll`, `OLEAUT32.dll`, `VERSION.dll` | Fully supported UWP-side APIs | Same treatment as D3D: resolve to the **real system DLLs**, no Wine builtin needed |
| `WS2_32.dll` (18 symbols: `WSASocketW`, `getaddrinfo`, `WSAIoctl`, ...) | TCP (incl. loopback) verified OK on console; only `AF_UNIX` is refused, and nothing here asks for `AF_UNIX` — that's a *Wine-internal* wineserver IPC detail (§ below), not a Megabonk/Steamworks networking need | Resolve to the **real system `ws2_32.dll`** directly. Zero new code for this DLL |
| `ADVAPI32.dll` (33 symbols: `Reg*`, `Crypt*` legacy, `Event*` ETW) | Registry is virtualized per-package in AppContainer (works, redirected, not blocked); ETW/legacy crypto mostly fine | Real system DLL is probably fine; low risk either way — **not on the critical path**, verify opportunistically |
| `GDI32.dll` (5 symbols: `ChoosePixelFormat`, `SetPixelFormat`, `SwapBuffers`, `GetDeviceCaps`, `GetObjectA`) + `OPENGL32.dll` (6 symbols, all `wgl*`) | No classic HDC exists without a window station → these would fail if actually called | Only need to **load successfully**, not work: Wine ships real builtin `opengl32.dll`/`gdi32.dll` PE+stub pairs already (`ls /usr/lib/x86_64-linux-gnu/wine/x86_64-windows/` confirms both present) — load those, don't worry about making them functional, since `-force-d3d11` means Unity's own code never actually calls through them (verify in spike #1, §2) |
| `HID.DLL` (14 symbols), `SETUPAPI.dll` (5 symbols) | Broad raw device enumeration (`SetupDiGetClassDevsA` with no scoping) is not available without a declared device-access capability | Wine builtin DLLs, backed by a `uwph` stub that enumerates **zero** devices. Megabonk's gamepad support goes through Unity's `XInput`/new-input-system path anyway (confirmed separately working via `Windows.Gaming.Input` in the M1 app), so an empty legacy-HID enumeration should just make Unity ignore that path, not crash it — **flagged as spike #5**, not yet proven |
| `IMM32.dll` (8), `WINMM.dll` (23, incl. `waveOut*`/`waveIn*`), `SHELL32.dll`/`SHLWAPI.dll` (IME, legacy wave audio, shell folder/exec calls) | IME and legacy `winmm` wave audio are not meaningful on a gamepad-only console (XAudio2 is the real audio path, separately broken per REBUILD.md — pre-existing bug, out of scope here); `ShellExecuteW`/`SHFileOperationW` are blocked | Wine builtin DLLs with stub/no-op backends — these exist only so the module loads; none are expected to be called in practice |
| `WINHTTP.dll` (4), `dwmapi.dll` (2) | `WinHttpOpen` etc. actually work fine in UWP (WinHTTP, unlike WinINet, is explicitly UWP-supported); `dwmapi` queries are harmless | Real system DLLs should work directly; low risk |
| `WININET.dll`, `PSAPI.DLL`, `dbghelp.dll` — **only `UnityCrashHandler64.exe` imports these**, not the game itself | WinINet is explicitly unsupported in UWP; this binary is Unity's separate out-of-process crash reporter | **Just don't launch `UnityCrashHandler64.exe`.** It's Unity's own crash-upload helper, invoked by `CreateProcessW` from `UnityPlayer.dll` — which is already stubbed to fail cleanly above, so this binary is simply never spawned. Zero work needed on this exe |
| `baselib.dll`'s own 12 exports (`Baselib_DynamicLibrary_*`, `Baselib_SystemFutex_*`, `Baselib_Timer_*`) | This is **Unity's own code**, not a system DLL — it implements IL2CPP's low-level platform abstraction on top of plain `KERNEL32`/`WS2_32`/`api-ms-win-core-synch-l1-2-0.dll` | No porting needed — it's a normal x64 PE that will load like any other DLL once kernel32/ntdll underneath it are sound |

## 2. Architecture recommendation

**Recommended: Wine's existing x86_64 PE loader + builtin DLL set, running inside the UWP AppContainer
process, with a brand-new `uwph` unixlib backend written against AppContainer-legal Windows/WinRT APIs
— and *not* a custom from-scratch PE loader, and *not* a new wined3d D3D11 backend.**

Why Wine's loader/builtins over a from-scratch loader (alternative "a" from the original brief):
- Debian's installed `wine64` package already ships **real, already-built x86_64 PE binaries** for
  every DLL Megabonk's own binaries import directly (`ntdll.dll`, `kernel32.dll`, `user32.dll`,
  `win32u.dll`, `advapi32.dll`, `gdi32.dll`, `opengl32.dll`, `setupapi.dll`, `hid.dll`, `winmm.dll`,
  `imm32.dll`, `shell32.dll`, `shlwapi.dll`, `winhttp.dll`, `wininet.dll` — confirmed present at
  `/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/`). Writing a from-scratch loader means re-deriving
  Wine's already-solved handling of **API Sets** (`UnityPlayer.dll` directly imports
  `api-ms-win-core-synch-l1-2-0.dll` and two `api-ms-win-core-winrt-*.dll` forwarders — Wine already
  resolves these correctly), SEH/x64 unwind tables, TLS callbacks, and the full export surface of ~15
  peripheral DLLs. That's strictly more work than adapting Wine's one substitution point.
- Wine's own PE↔unix split is **not ELF-bound at the protocol level**: each builtin PE module resolves
  an exported `__wine_unixlib_handle` and calls through `__wine_unix_call_dispatcher(handle, code, args)`
  — a plain function-pointer/opcode protocol. The *current* `.so` files on this box are ELF (can't load
  inside AppContainer, confirmed: AF_UNIX and anything implying a POSIX subsystem is off the table), but
  the dispatch protocol itself has no ELF dependency — a pure-PE "uwph" implementation can sit behind the
  same handle, as long as it exports the same opcode table each builtin module expects. **This is the
  core unverified assumption of the whole plan — see spike #2, §3.**

Why real system DLLs for D3D11/D3D12/DXGI/WS2_32/crypto instead of Wine's reimplementations:
- The entire reason Wine normally ships `wined3d` (translating D3D calls to GL/Vulkan) is that Linux has
  no real `d3d11.dll`. **Xbox is a real Windows NT kernel** — the real `d3d11.dll`/`dxgi.dll`/`d3d12.dll`
  are present and already verified working directly from an AppContainer process (REBUILD.md, and the
  M1 app's own swap chain). Translating through wined3d would be strictly worse (slower, lossy, and
  unnecessary engineering) when the real thing already works. Same logic for `ws2_32.dll` (loopback TCP
  already verified) and the crypto/COM DLLs (UWP-supported natively). This cuts the single largest
  originally-feared work item (a new wined3d backend, estimated 2000-5000 LOC by a from-scratch
  translation approach) down to a loader-configuration decision: **which import table entries point at
  Wine builtins vs. the real system DLLs** (lane `uwph-dll-passthrough-map`, §4).

What's actually new/hard, after the above cuts: only four things—
1. A `uwph` unixlib backend for `ntdll`/`kernel32` covering memory (W^X via `...FromApp`), threads,
   file I/O, and sync objects, using only AppContainer-legal calls.
2. An in-process replacement for wineserver's AF_UNIX-based IPC (handles/sync objects/process-thread
   bookkeeping), since AppContainer refuses `AF_UNIX` outright and UWP forbids a second OS process.
3. A new Wine display driver mapping every Wine "window" operation onto the single real `CoreWindow`
   the app already owns (reusing the verified `xbox-test/app` CoreWindow+D3D11 code).
4. Stubs (not full implementations) for the dead-at-runtime peripheral DLLs (`opengl32`, `setupapi`,
   `hid`, `winhttp`'s unused parts, IME) so they load without crashing, per the import-gap table in §1.

Risk comparison:

| Option | Est. new code | Biggest risk | Verdict |
|---|---|---|---|
| **A. Custom PE loader + from-scratch shim (no Wine)** | Loader (~2-4k LOC for a correct x64 PE/apiset/SEH loader) + ~15 peripheral DLL stub surfaces | Reinventing solved problems (apiset, unwind, TLS) with no reference to check against | Fallback only, if spike #2 shows Wine's unixlib substitution point is unworkable |
| **B. Wine PE loader/builtins + new `uwph` unixlib + real-DLL passthrough for D3D/WS2_32/crypto (recommended)** | `uwph` backend (~1-2k LOC estimate, mostly memory/thread/handle shims) + 1 display driver (~500-1500 LOC, scoped to a single CoreWindow, no multi-window support needed) + in-process wineserver bridge (~500-1000 LOC) | Spike #2 (unixlib substitution) and spike #3 (single-CoreWindow driver tolerance) both unproven | **Recommended** — reuses the most Wine code for the least new code, and the two unproven assumptions are cheap to spike before committing |
| **C. Full wined3d GL/Vulkan→D3D11 translation (original REBUILD.md sketch)** | +2000-5000 LOC on top of B | Pure waste — real D3D11/12 already verified working | Rejected |

## 3. Unknowns to spike first (ranked by how much they invalidate the plan if wrong)

1. **IL2CPP/Burst/Mono JIT memory calls.** Does `GameAssembly.dll`'s embedded runtime call
   `VirtualAlloc`/`VirtualProtect` with `PAGE_EXECUTE_READWRITE` directly, and does AppContainer's *real*
   `kernel32.dll` (if we ever fall back to it for anything) hard-fail or silently coerce that call? This
   determines whether the `uwph` kernel32/ntdll shim (item 1, §2) is mandatory before any Unity code runs
   at all, or something that can be added reactively after a crash. **Spike before lane
   `uwph-kernel32-ntdll` starts**, by building a tiny mingw/clang-cl test PE that calls `VirtualAlloc`
   then writes+executes a `ret` instruction, running it under the real system kernel32 inside a minimal
   signed UWP test app (can literally extend the existing M1 app with one test button).
2. **Is Wine's unixlib substitution point really a clean function-pointer boundary with no ELF
   dependency, in practice, not just in protocol description?** This is the load-bearing assumption of
   the entire recommended architecture (§2). Spike by writing the smallest possible `uwph` stub that
   answers 2-3 `ntdll` unix-call opcodes (e.g. whatever backs `NtClose`/`NtQuerySystemTime`) and getting
   Wine's real `ntdll.dll` to load and call into it standalone on Linux first (fast iteration, no
   console needed), before attempting it inside AppContainer.
3. **Does a single real `CoreWindow` tolerate Wine's user32 message-queue semantics** well enough for a
   trivial `CreateWindowExW` + `GetMessage`/`DispatchMessage` Win32 app (e.g. a 10-line test exe, not
   Megabonk yet) to survive, given Unity polls window size/focus every frame? Spike inside lane
   `uwph-win32u-corewindow-driver`.
4. **Are the Debian `wine64` x86_64 PE builtins standalone-usable**, or do they bake in assumptions about
   `/usr/lib/wine/...` paths, the Wine registry hive, or a POSIX environment on first run that would need
   patching even before `uwph` substitution? Spike by copying just the PE files to an isolated directory
   and trying to load one (e.g. `ntdll.dll`) with a minimal custom loader stub off-console, checking what
   it reaches for at `DllMain`.
5. **Does Unity tolerate an empty legacy-HID device enumeration** (`SetupDiGetClassDevsA` returning zero
   devices) without falling back away from its working `Windows.Gaming.Input`/XInput gamepad path? Low
   priority — spike only once `uwph-kernel32-ntdll` + the display driver already work, as part of
   `megabonk-boot`.

## 4. Concrete lanes

Format: **ID | objective | files/dirs | build+test commands | acceptance | console? | login? | size | deps**

### Done (kept for the record; do not re-run)

- **app (M1)** — minimal signed UWP test app. Files: `xbox-test/app/{main.cpp,AppxManifest.xml,build.sh,qrcodegen.{cpp,hpp},Assets/}`. **DONE**, see §0.
- **deploy** — Device Portal scripts. Files: `xbox-test/dp/*.sh`. **DONE**, see §0.
- **plan** — this document. **DONE** (this commit).
- **launcher-login** (folds in the "João idea": on-console Steam QR login) — QR rendered in the
  CoreWindow via `BeginAuthSessionViaQR`-equivalent HTTPS calls + `qrcodegen.cpp`, gamepad-selectable
  Refresh button, 5-minute auto-refresh, token persisted to `LocalState\steam_session.bin`. **DONE**,
  commits `438d0810`..`56acf688` on `lane/dl` (built from `g/launcher`).

### In progress

- **dl** | finish the on-console Steam CM/depot downloader so the launcher's A button fills in real
  Megabonk files | `xbox-test/app/main.cpp` (CM client, protobuf, AES/LZMA chunk decode), evidence in
  `x64-run/runs/dl/` | build: `xbox-test/app/build.sh`; test: install via `xbox-test/dp/install.sh`,
  trigger via `LocalState\autotest.txt` hook, verify via `xbox-test/dp/ls-dir.sh` + SHA1 against the
  manifest | acceptance: all 69 files present under `LocalState\megabonk`, sizes+SHA1 match
  `/workspace/megabonk-from-console/MANIFEST.sha256`'s source-of-truth manifest | console: yes | login:
  yes (João's QR, already scanned once) | size: large (most of a C++ Steam CM client) | deps: none
  (independent of the uwph track below)

### New: the uwph / native-execution track (this plan's main contribution)

1. **uwph-spike-dispatch** | prove Wine's `__wine_unix_call_dispatcher` substitution point works with a
   pure-PE replacement, off-console first (spike #2) | new dir `xbox-test/uwph/spike-dispatch/` |
   build: a small C++ shim exporting the same opcode handler signature `ntdll.so`'s real unixlib exports,
   linked so a copy of `/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/ntdll.dll` loads against it instead
   of `ntdll.so`, tested first under plain Wine on this Linux box (`WINEDLLOVERRIDES`, `WINEDLLPATH`
   tricks) before any UWP packaging | acceptance: a trivial test program calling 2-3 `ntdll` entry points
   (e.g. `NtQuerySystemTime`) gets correct answers from the new shim instead of the real `ntdll.so` |
   console: no | login: no | size: medium (research spike) | deps: none — **start first**
2. **uwph-vrx-spike** | spike #1 — does AppContainer's real `kernel32.dll` hard-fail or coerce
   `VirtualAlloc(..., PAGE_EXECUTE_READWRITE)` | extend `xbox-test/app/main.cpp` with one test path: call
   `VirtualAlloc` with RWX, write a `ret`, call through a function pointer, log the result | build/test:
   `xbox-test/app/build.sh` + `xbox-test/dp/install.sh` + `xbox-test/dp/launch.sh` +
   `xbox-test/dp/wait-log.sh` | acceptance: `boot.log` on LocalState shows whether RWX alloc+exec
   succeeded, failed cleanly, or crashed | console: yes | login: no | size: small | deps: none — can run
   in parallel with #1
3. **uwph-kernel32-ntdll** | build the real `uwph` unixlib backend: memory (`VirtualAllocFromApp`/
   `VirtualProtectFromApp` under the hood), threads (map to real `CreateThread`, which works per
   REBUILD.md), file I/O (map to real `CreateFileW` et al., which work), sync primitives, and a
   `CreateProcessW` stub that fails cleanly | new dir `xbox-test/uwph/host/` | build: extend
   `xbox-test/app/build.sh`'s link step to pull in Wine's real `ntdll.dll`/`kernel32.dll` PE files from
   `/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/` plus this new backend; test: load a trivial test PE
   (not Megabonk yet) that calls a few dozen representative kernel32 functions and logs results | 
   acceptance: the test PE's log shows correct results for file I/O, threading, and RWX memory handling
   (via `VirtualAllocFromApp`) | console: yes | login: no | size: large | deps: #1, #2
4. **uwph-wineserver-inprocess** | replace wineserver's AF_UNIX request/response IPC with in-process
   calls/shared state (same address space) for handle duplication, sync objects, process/thread
   bookkeeping | new dir `xbox-test/uwph/server/` | build/test: same test-PE harness as #3, extended to
   exercise `CreateEventW`+`WaitForSingleObject`+`DuplicateHandle` across two threads | acceptance: the
   test PE's two threads correctly synchronize via the in-process server, logged | console: yes | login:
   no | size: medium-large | deps: #3
5. **uwph-win32u-corewindow-driver** | new Wine display driver mapping every Wine window operation onto
   the single real `CoreWindow` already created by `xbox-test/app/main.cpp` (spike #3 folds in here) |
   new dir `xbox-test/uwph/display/` | build/test: a trivial Win32 test PE doing
   `RegisterClassExW`+`CreateWindowExW`+a `GetMessage`/`DispatchMessage` loop, logging `WM_SIZE`/
   `WM_ACTIVATE`-equivalent delivery | acceptance: the test PE's window survives for N frames without
   Wine's internal window state diverging from the real CoreWindow's actual size/focus, logged to
   `boot.log` | console: yes | login: no | size: large | deps: #3, #4
6. **uwph-dll-passthrough-map** | configure the loader so `d3d11.dll`/`dxgi.dll`/`d3d12.dll`/
   `ws2_32.dll`/`crypt32.dll`/`bcrypt.dll`/`ole32.dll`/`oleaut32.dll`/`version.dll`/`advapi32.dll`
   resolve to Xbox's **real system DLLs**, while `user32.dll`/`win32u.dll`/`kernel32.dll`/`ntdll.dll`
   resolve to the `uwph`-backed Wine builtins, and `opengl32`/`setupapi`/`hid`/`winmm`/`imm32`/
   `shell32`/`shlwapi`/`winhttp` resolve to Wine's real builtin PE files with stub-only backends (per
   §1's table) | new file `xbox-test/uwph/host/dll_map.cpp` (or a loader config table) | build/test:
   link the test PE from #5 plus a D3D11 `CreateSwapChainForCoreWindow` call directly against the real
   system `d3d11.dll` side-by-side with Wine-builtin `user32.dll` | acceptance: one process successfully
   uses both the real `d3d11.dll` (rendering a frame) and the `uwph`-backed `user32.dll` (owning the
   window) at once | console: yes | login: no | size: small-medium | deps: #5
7. **uwph-loader-smoke-test** | first end-to-end test of a real, unmodified, non-trivial Win32 PE (not
   written by us) running under this whole stack, before risking Megabonk | pick something small and
   already available, e.g. a tiny public-domain Win32 game or `UnityCrashHandler64.exe` itself (already
   on disk, not critical if it fails) | build/test: load it through the `uwph` stack, log every failure
   point | acceptance: the chosen test binary gets further than "fails to load" — ideally produces a
   visible frame or a clean exit, logged | console: yes | login: no | size: medium | deps: #6
8. **megabonk-boot** | boot `Megabonk.exe` (`-force-d3d11`) through the full `uwph` stack to the Unity
   splash screen | depends on lane `dl` having placed all 69 files, and on this track's #7 passing | 
   build/test: `xbox-test/dp/install.sh` the combined app+uwph package pointed at
   `LocalState\megabonk\Megabonk.exe`; `xbox-test/dp/screenshot.sh` | acceptance: DP screenshot shows the
   Unity splash (or further) | console: yes | login: needs `dl` lane's download to have completed | size:
   medium (integration, assuming #1-#7 succeeded) | deps: `dl`, #7

### Kept from the original backlog, unchanged in scope (not this lane's job to detail further)

- **steam-map** — static interface map of pinned `steamclient64` build `1788652215` (read-only analysis,
  João pre-approved). No console. Independent of the uwph track.
- **steamhost** — headless `steamhost64.exe` loading the real unmodified Valve client for DRM only, after
  `steam-map`. Needs the uwph track far enough along to run an unmodified Win32 exe (overlaps with
  `uwph-loader-smoke-test`, #7 above).

## Backups

Unchanged from REBUILD.md: `x64-run/backup.sh` → git bundles + diffs into the persistent agent store,
`x64-run/backup-loop.sh` every 6h, local branch `x64-spike` holds all local work, **never push**.
