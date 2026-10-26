# X86-32BIT-PLAN: running 32-bit (PE32) Windows Steam games on the Xbox Series S stack

Author: Grok Bot executor (research only; no console use, nothing pushed or posted). Date: 2026-10-26 (America/Sao_Paulo).
Scope: BACKLOG item 49 (x86-engine, option b) and everything it drags in. Targets: Spacewar 480, LIMBO 48000, Undertale 391540.
Marking: **[V]** verified in a file or page this session (path/URL given). **[G]** guess/estimate, not measured. Effort = weeks for ONE coding agent (Codex or Claude) working offline plus console sessions.

## 0. TL;DR

1. Native 32-bit is dead (item 46): `ljmp 0x23` gives 0xc0000005 before any x86 instruction, WoW64 DLLs fail with error 126 [V: BACKLOG.md "Status 2026-10-26 10:05", runs/cs32/]. So 32-bit = **emulate an x86 CPU in-process and trap API calls to our 64-bit host**.
2. Best engine: **Boxedwine's "normal" interpreter core, extracted as a library** from /workspace/Boxedwine-xbox-64 (our fork; GPL-2-or-later; already unit-tested against real hardware; already the shipping no-JIT core for its WASM web build; has multi-threaded mode; has a built-in "callback" pseudo-opcode usable as the host trap). Not from scratch, it is an existing layer. A W^X JIT on top (Boxedwine's own JIT_X64 backend ported to VirtualAllocFromApp/VirtualProtectFromApp) is an optional later speed-up, not needed for hello world.
3. API boundary: **thunk at the Win32 DLL API level into our existing 64-bit mlp/uwph routes** (not by running Wine's i386 DLLs emulated), with thunk signatures **generated from Wine `.spec` files** and struct translators written by hand only for pointer/size-dependent structs. D3D9 is thunked at the COM boundary (~300 methods) into the 64-bit wined3d/GLon12 layer (items 48a/48b), never emulated.
4. Realistic cost: 32-bit console hello world **~2.5 weeks**; Spacewar CPU-side (no graphics) **~7 weeks** cumulative; Spacewar on screen **~11 weeks** (gated on 48b, 52, 42/43, 44). LIMBO and Undertale each +2 to 4 weeks after that. **All three games import d3d9.dll, so none can show a frame before the D3D9 layer (48b) works and is thunked from 32-bit.**
5. Recommendation: **do not put 32-bit in the Megabonk beta critical path.** Run a small side lane (M0 hello world) after item 52 (prun) lands, so the architecture is proven cheaply; do M1+ post-beta. Cut LIMBO (possible DRM wrapper) and Undertale (own x86 D3DX9, 467 imports) from any beta; Spacewar is the only sensible first 32-bit target.

## 1. What exists today (facts the plan rests on)

| Fact | Evidence |
|---|---|
| PE32 files are mapped (relocs applied, imports classified) but never executed | runs/pe32/play-480/launch_result.json: `"PE32 mapped, imports=122, gap=96; relocations=7887 ... logical-base=0x10000000; NO execution"`; play-48000: `imports=248, gap=232; relocations=0; logical-base=0x00400000` [V] |
| Even 64-bit games are only mapped, not run (`gameCodeExecuted=false`) | BACKLOG item 52 "play-runs-guest" TOP PRIORITY [V] |
| Native 32-bit transition impossible in UWP | BACKLOG status 10:05: "native-32 viable = NO ... 0xc0000005 before any x86 instruction; wow64*.dll LoadPackagedLibrary error 126" [V] |
| Item 49 already says: option (b) Boxedwine CPU core as a library behind thunks, no RWX | BACKLOG line 192 [V] |
| JIT permitted only via VirtualAllocFromApp/VirtualProtectFromApp | project rule from parent; WINE-BASE-REPORT.md [V] |
| Per-game imports (i386) | Spacewar: kernel32 75, steam_api 19, user32 18, sdkencryptedappticket 4, ole32 3, gdi32 1, d3dx9_42 1, d3d9 1 (122 total). LIMBO: kernel32 133, user32 46, gdi32 25, steam_api 13, dbghelp 10, shell32 6, d3dx9_43 4, ole32 3, xinput1_3 2, comdlg32 2, comctl32 2, dinput8 1, d3d9 1 (248 total). Undertale: kernel32 191, msvcrt 85, user32 69, ws2_32 30, gdi32 29, winmm 16, wininet 12, advapi32 6, d3dx9_43 6, shell32 4, ... (467 total) [V: runs/pe32/play-*/pe32-imports-*.csv, runs/wgap/undertale.csv] |
| wine_gap totals (all arches) | Spacewar 136 total/107 missing/18 win32u; LIMBO 256/237/66; Undertale 467/416/127 [V: runs/wgap/*-summary.txt]. Note: "missing" here is vs. current console implementations; most of it is the same user32/gdi32/kernel32 work item 53 (u32m) already does for Megabonk. |
| LIMBO/Undertale: ImageBase 0x400000, relocations stripped. Spacewar has .reloc | runs/wgap/pe/{limbo,undertale}/ section tables; launch_result above [V]. LIMBO has a `.bind` section, likely a SteamStub wrapper [G]. |
| Undertale ships its own **x86** D3DX9_43.dll (2.0 MB) and steam_api.dll | runs/wgap/pe/undertale/ [V] |
| steam-map is pinned to steamclient64 only | pe32 / Steam notes in LANE_NOTES.md [V: /workspace/wt/pe32/LANE_NOTES.md]; a 32-bit steam_api needs its own shim (see 3.6) |
| D3D9 verdict: no D3D9 in UWP; use Wine d3d9+wined3d on Mesa GLon12 (48a/48b); the D3D9 layer runs 64-bit and only the ~300-method COM boundary is thunked from 32-bit | research/D3D-LAYERS-REPORT.md [V] |
| i686 cross compiler is on the box: `i686-w64-mingw32-gcc` (needed for 32-bit test exes and any guest-side stubs) | `which` on box [V] |

## 2. Execution engine options, ranked

Constraint: no RWX/JIT other than VirtualAllocFromApp W^X. Host = Zen 2 x86-64, so ARM-targeted translators cannot be used as-is.

| Rank | Option | Fit | Effort to 32-bit hello world | Effort to first real game (CPU side) | Notes / evidence |
|---|---|---|---|---|---|
| **1** | **Boxedwine normal core (interpreter) as library** | Pure C++ decoder + op handlers; no host-ISA knowledge; x87/MMX/SSE/SSE2 present; hardware-compared unit tests; multi-thread mode; soft MMU; `Callback` pseudo-op = host trap | **2-3 wk** [G] | **+4-5 wk** (threads, SEH, callbacks, stability) [G] | docs/CPUemulation.md lines 5-26: "normal CPU emulator is slow but ... will just work on all architectures ... used for the WASM/Emscripten build"; supports single or multi-threaded mode [V]. Source ~273K lines total, emulation/ 107K lines, of which only cpu/common + cpu/normal + decoder + softmmu (~35-45K, my line-count estimate [G]) is needed. License GPL-2-or-later (license.txt [V]); we do not distribute the app, so no GPL obligation is triggered, but check before any public release (not legal advice). Our fork already builds for UWP and has "linear memory" (4 GiB guest-shaped alias via VirtualAlloc2/MapViewOfFile3, source/emulation/softmmu/soft_ram.h lines 45-65 [V]); whether those APIs are callable from UWP via the *FromApp variants is untested [G]. |
| 2 | Unicorn 2 built in **TCI (no-JIT) mode** | Existing library, clean API (map, regs, hooks, emu_start), x86 32-bit, QEMU 5.0.1 TCG core | **~2 wk** [G] | **+5-7 wk** [G] | Community fork github.com/1rhino2/unicorn-tci (unicorn 2.1.4 + QEMU 5.0.1; "maps the code buffer read+write only (no PROT_EXEC, no MAP_JIT)"; GPL-2.0; "TCI is far less exercised upstream than the JIT backends") [V: WebFetch search result, crates.io unicorn-engine-sys-tci]. Weak points [G]: one engine = one CPU, memory per engine (sharing needs `uc_mem_map_ptr` across engines, LOCK-prefix atomicity unclear), no AVX (QEMU 5.0), TCI speed unknown but TCI is generally slower than Boxedwine's decoded-op core, no precedent running Windows games. Good fallback if Boxedwine extraction proves too coupled. |
| 3 | Bochs (LGPL-2.1) or libx86emu-style interpreter | Pure interpreters | 3-4 wk [G] | 7+ wk [G] | Full-system oriented, slower, no Windows user-mode precedent here; not verified this session [G]. Dominated by option 1. |
| 4 | Boxedwine normal core + **W^X dynarec** (its JIT_X64 backend) | Same core, faster | n/a (follow-up) | M5: +3-4 wk [G] | JIT_X64 uses asmjit and writes via `Platform::alloc64kBlock(executable)`/`writeCodeToMemory`/`clearInstructionCache`; Windows x64 path assumes `PAGE_EXECUTE_READWRITE` (platform/windows/platform.cpp ~771) [V]. Needs a RW-then-VirtualProtectFromApp(RX) rewrite + per-block-flush; also relies on linear-memory aperture. Uses an existing dynarec, not from scratch, but not for the first milestone. |
| 5 | Box64/Box86 WOW64 (`wowbox64.dll`) | Real production i386 engine with Wine-WoW64 syscall interface | blocked | blocked | Upstream docs: WOW64 build option, ARM64 PE DLL for Wine WoW64 on ARM64 (github.com/ptitSeb/box64 docs/WINE.md; Hangover README) [V]. Its dynarecs target ARM64/RISC-V/LoongArch; it expects Wine's own wow64.dll/ntdll to drive it; interpreter-only build on an x86 host and use inside our non-Wine ntdll is not a supported configuration [G]. Could be mined for ideas (thunk tables) but not a drop-in. |
| 6 | FEX-Emu `libwow64fex` | Best-quality engine | blocked | blocked | ARM64 host only; JIT [V: Hangover README]. No x86-host path [G]. |
| 7 | Wine wow64cpu / Hangover (HODLL=wow64cpu.dll) | "native" i386 on x86_64 | **dead** | **dead** | needs far jump to cs32; item 46 proved UWP faults before the first instruction [V]. |
| 8 | QEMU system TCI / qemu-user | | n/a | n/a | qemu-user is Linux-syscall oriented (wrong ABI) [G]; Unicorn (rank 2) is the QEMU-derived option that fits. |
| 9 | Own recompiler from scratch | | rejected by João | | |

Decision: **option 1 now, option 2 as plan B, option 4 as speed phase.**

### 2.1 Why Boxedwine's core fits (and the real costs)

- Coupling: the core's `CPU` holds `KThread*`/`KMemory*` and ~56 `thread->` references in cpu/normal+common [V: grep, previous session]. But the repo's own unit tests build a bare `KProcess::create()` + `process->createThread()` + `KThread::setCurrentThread()` and run guest bytes with no Linux userland (source/test/cpu/testCPU.cpp lines 138-192, 5548-5626) [V]. So the minimal adapter is: keep KProcess/KThread/KMemory as the "machine", drop the Linux syscall layer (kernel/syscalls, fs, x11, opengl, sdl...). Cut list ~ everything under source/{fs,devs,x11,opengl,sdl,process} [G].
- Host trap: the decoder already turns a reserved encoding (`FE /7 imm8`, "Callback" op, source/emulation/cpu/decoder.cpp ~4343-4357) into a call to a host function pointer; Boxedwine uses it for `int99Callback[]` OpenGL/X11 [V: source/opengl/es/esopengl.c:1221 pattern]. We register our own index set; this is the thunk entry (no #UD, no signal, no exception path).
- Memory model: soft MMU with 4 KB RamPages (`source/emulation/softmmu/soft_ram.h`). Consequence: a guest pointer is NOT a host pointer. Thunks must copy in/out via the MMU helpers (readd/readb/memcopyToNative) or use the linear alias if it works in UWP. Design the thunk generator around helper calls `G2H(ptr, len)` that either returns the linear-alias pointer or a bounce buffer, so the policy can change without touching thunks.
- Guest address space is separate from the host's, so the "needs exactly 0x400000" problem of LIMBO/Undertale vanishes: the emulator's guest 4 GB is ours to lay out; the existing pe32 mapper (which maps at a logical base into *host* memory, no execution) needs a second backend that writes sections into guest pages at the true ImageBase, page protections included [G: pe32 lane code under wxl, not re-read this session].
- Performance premise: Boxedwine's own JIT'd 32-bit guest runs Quake II 800x600 software at 57.3 fps on an i7-14700 (docs/Performance.md) [V]. Series S figure (20-40 fps with a JIT) and interpreter slowdown (10-100x vs JIT depending on instruction mix) are **[G]**; I did not find a published normal-core number.

## 3. The 32↔64 boundary design

### 3.1 Model (Hangover-style, minus Wine's ntdll)
Hangover/Wine WoW64 run emulated code only for the app; every Win32/NT call leaves emulation at the syscall (github.com/AndreRH/hangover README "we break out of emulation at the win32 syscall or wine unix call level") [V]. Wine's version thunks ~758 NT/win32u syscalls (wow64: 258 `wow64_Nt*`, wow64win: 503 [V from WINE-BASE-REPORT.md; wow64 sources 16.8K lines total in dlls/wow64+wow64win, `wc -l` this session]). It does that because the i386 ntdll/kernel32/user32 are Wine's **i386 PE builds executing inside the emulator**.

We have two possible cut points:

| Cut | Thunk count | Emulated code volume | Reuses our 64-bit work | Verdict |
|---|---|---|---|---|
| A. **Win32 API level** (kernel32/user32/gdi32/advapi32/... exports) | per game: Spacewar 122, LIMBO 248, Undertale 467 [V] (union ≈ 650 w/ overlap) | only the game | directly (mlp routes, items 41-44, 53) | **Chosen.** Smallest emulated code, best speed. |
| B. NT syscall level (Wine wow64 table) | ~760 | whole i386 Wine DLL stack (ntdll, kernelbase, kernel32, user32, gdi32, win32u) interpreted | needs only Nt*/NtUser* (already in uwph partly) | Rejected for speed (user32/gdi32 interpreted) and because it needs an i386 Wine PE build + i386 ntdll loader. Keep as fallback for functions where A-level semantics are hairy (e.g. `RtlUnwind`, loader). |

Hybrid rule: the **CRT and pure-library DLLs run emulated** (msvcrt.dll for Undertale = 85 imports, varargs printf-family, FILE* struct; d3dx9_43.dll x86; steam_api.dll's pure-C parts); thunks cover everything that needs the OS. Build msvcrt/ucrt as i386 PE with `i686-w64-mingw32-gcc` from Wine source /workspace/research/wine (mingw i686 exists on box [V]); correctness of Wine's msvcrt under our loader is [G].

### 3.2 Thunk generation from Wine .spec files
- Input: `dlls/{kernel32,kernelbase,ntdll,user32,gdi32,advapi32,shell32,...}/*.spec` in /workspace/research/wine. Counts of stdcall exports: ntdll 1197, kernel32 1330, user32 778, gdi32 461 [V: grep -c this session]. Spec lines give calling convention and **arg types** (`long`, `ptr`, `str`, `wstr`, `int64`, `float`, `double`, `varargs`), which is enough to emit for each import: (1) a guest stub, (2) a host marshaller. The `wine_gap.py` import CSVs already enumerate exactly which symbols each game needs (route, class) so generation is demand-driven: **~650 thunks for all three games, generated, not hand-written** [V counts; "generated" is design].
- Guest stub (placed in a read-execute "thunk page", e.g. at 0x7FFE0000 range, chosen outside every PE's ImageBase; stubs 16 bytes each): `mov eax, <thunkId>` ; `FE 38 <cbIndex>` (Callback op) ; `ret <argbytes>` for stdcall (`ret` for cdecl). IAT entries are filled with the stub addresses by the loader. The host handler reads args from `[esp+4...]`, calls the existing 64-bit function through the mlp router, writes EAX (and EDX for 64-bit returns), adjusts nothing else (stub's `ret n` pops args). `varargs` (wsprintf, printf family) are handled by a per-function hand-written marshaller [G].
- Host-side pointer rule: guest ptr is a 32-bit guest address; `G2H` per 2 above. Handles (HANDLE/HWND/HDC/HMODULE) must stay < 2^31 and be passed as sign-correct 32-bit values; the host handle allocator is opaque-monotonic so this works if IDs fit in 32 bits [G: allocator not re-read].
- Struct translation: only structs containing pointers, `LONG_PTR/SIZE_T/ULONG_PTR`, or handles-in-struct-with-padding differ. Wine already has the 32-bit layouts in `dlls/wow64/struct32.h` (66 typedefs) and `include/winternl.h` (`TEB32`, `PEB32`, `NT_TIB32`, `RTL_USER_PROCESS_PARAMETERS32` at lines ~656, 788, 963, 1198) and `EXCEPTION_RECORD32` in `include/winnt.h:2414` [V]. At API level the hand-written set is small: WNDCLASS(EX)A/W, MSG, OVERLAPPED, SECURITY_ATTRIBUTES, STARTUPINFO, WIN32_FIND_DATA, OSVERSIONINFO, SYSTEM_INFO, PAINTSTRUCT, LOGFONT, DEVMODE, CONTEXT (see SEH), a few more [G: ~25-40 structs].
- Reference implementations to crib: dlls/wow64/{syscall,process,virtual,file,sync}.c and dlls/wow64win/{user,gdi}.c [V list], especially `Wow64SystemServiceEx` (syscall.c:1034), `wow64_NtContinue[Ex]` (501-533), `Wow64ApcRoutine` (1136), `Wow64KiUserCallbackDispatcher` (1222) [V].

### 3.3 Callbacks (host → guest)
Needed for: WndProc/DlgProc, timers, `EnumWindows`-style enums, TLS callbacks, DllMain, thread start routines, Steam `CCallbackBase::Run`, COM sink objects, `qsort` (if CRT thunked).
Design: `emu_call(thread, eip, args[], n, convention)` pushes args and a **return sentinel** (guest address of a stub `FE 38 <cbReturn>`), runs the interpreter until the sentinel traps, returns EAX. Nested calls form a stack per guest thread (host-to-guest-to-host reentrancy; the Wine analogue is `Wow64KiUserCallbackDispatcher`). Window messages: the message pump runs on the guest thread's own host thread; `DispatchMessage` thunk → host user32 (items 42/43) → registered guest WndProc → `emu_call`. Cross-thread `SendMessage` must queue to the target thread and run when it next pumps/waits [G: risk].

### 3.4 TEB32 / PEB32 / FS segment
- Allocate TEB32 + PEB32 + `RTL_USER_PROCESS_PARAMETERS32` + LDR lists in **guest memory** with Wine's 32-bit layouts (winternl.h as above; TlsSlots[64] at TEB32+0xE10, WOW32Reserved 0xC0 [V]). Required fields for CRTs: NT_TIB (ExceptionList 0, StackBase 4, StackLimit 8, Self 0x18), ClientId, ThreadLocalStoragePointer, PEB pointer 0x30, LastErrorValue, TlsSlots; PEB: ImageBaseAddress, Ldr, ProcessParameters, ProcessHeap, OS version fields, TlsBitmap [layout facts from winternl.h; exact needed set is [G] until a game faults].
- FS: Boxedwine's CPU keeps per-segment base (`seg[FS].address`); set FS base = TEB32 address on thread creation; `mov eax, fs:[0x18]` etc. then just work. This is exactly what native mode could not give us.
- TLS: implicit TLS (`.tls` directory) is allocated by our loader in guest memory and pointed to by `TEB32->ThreadLocalStoragePointer`; TLS callbacks run via `emu_call` (also required by item 52 for 64-bit).

### 3.5 32-bit SEH
- x86 SEH is frame-based (`fs:[0]` chain, `__except_handler3/4` in the CRT, i.e. guest code). We need only the **dispatcher**: on a CPU fault (access violation, divide, int3, illegal) the emulator builds EXCEPTION_RECORD32 + CONTEXT32 on the guest stack and resumes at a guest-side `KiUserExceptionDispatcher` that walks the `fs:[0]` chain calling handlers (`RtlDispatchException`), plus `RtlUnwind` and `RaiseException`/`RtlRaiseException` thunks that re-enter the same dispatcher. Write it in C, compile with `i686-w64-mingw32-gcc`, link into a tiny guest-resident "mini-ntdll" image (~300-600 lines [G]); model on Wine `dlls/ntdll/signal_i386.c`/`exception.c` [path from Wine layout; not opened this session -> [G]] and `dlls/wow64/syscall.c` `wow64_NtContinue`.
- C++ exceptions (0xE06D7363) go through the same path (`_CxxThrowException` → RaiseException). Games that never throw are unaffected; LIMBO/Undertale probably throw rarely [G].
- `NtContinue`/SetThreadContext need CONTEXT32 ↔ emulator-register mapping (also x87/SSE state via FXSAVE layout).

### 3.6 Steam API in 32-bit
All three import a 32-bit `steam_api.dll` (19/13/? symbols) [V]. The existing map targets steamclient64 only. Plan: provide a **replacement 32-bit `steam_api.dll`** (guest-side shim exporting the flat API, `SteamAPI_Init/Shutdown/RunCallbacks/RegisterCallback/UnregisterCallback/RegisterCallResult`, `SteamAPI_GetHSteamUser`, interface accessors) whose bodies thunk to the 64-bit steam bridge. Callbacks register guest `CCallbackBase*` objects, so the host calls guest vtables via `emu_call`. Payload struct packing differs between 32- and 64-bit Steam builds on some platforms [G: not verified for Windows]; check against the Steamworks SDK headers (`VALVE_CALLBACK_PACK_*`) during M2. No DRM bypass: the shim only forwards to the legitimate client path. LIMBO's `.bind` section [V present, SteamStub identity is G]: if it is Steam's wrapper it needs the real client to unwrap, i.e. same legitimate path; do not attempt anything else.

### 3.7 Threads
Each guest thread = one host thread running its own CPU loop over the shared KMemory (Boxedwine multi-threaded mode, docs/CPUemulation.md line 26 [V]); `CreateThread` thunk creates TEB32 + stack + CPU, starts `emu_call(startRoutine)`. Blocking APIs (WaitForSingleObject, Sleep, GetMessage) run on the host thread normally while the guest CPU is parked inside the thunk, so no guest scheduler is needed. LOCK-prefixed ops and `cmpxchg` in multi-threaded mode live in cpu/common/common_lock.cpp [path from earlier grep, treat as [G]]; verify with a threaded stress test (M1). Single-threaded Boxedwine scheduler is "not that great ... stuttering" so MT mode is required [V line 26].

## 4. Graphics: what could run without the D3D9 layer?

| Game (32-bit) | API | Without D3D9 layer? |
|---|---|---|
| Spacewar 480 | d3d9.dll (`Direct3DCreate9`) + `D3DXCreateFontA` from d3dx9_42 | **No** [V: pe32-imports-480.csv] |
| LIMBO 48000 | d3d9.dll + d3dx9_43 (4 funcs), xinput1_3, dinput8 | **No** [V] |
| Undertale 391540 | d3d9 via GameMaker runner + shipped x86 D3DX9_43.dll (6 imports) + winmm, ws2_32, wininet | **No** [V: pe dirs; d3d9 import from csv] |

None of the three is D3D11, software-only or OpenGL, so **no 32-bit Steam game currently identified can run visually before 48b**. What 32-bit work *can* be proven earlier with zero graphics: (a) console hello world, (b) Spacewar CPU/loader/Steam/Win32 init up to `Direct3DCreate9` (a `Direct3DCreate9` stub returning NULL gives a deterministic stop line). The D3D11 badge (item 51) will correctly show these three as "D3D9/Legacy".
If João wants a *visible* 32-bit result earlier, a 32-bit D3D11 or GDI-only game would be needed: any game from the later list that imports d3d11.dll as PE32 (unknown; need a PE32 scan of installed games [G]). Not found among the three targets.

D3D9 boundary (when 48b exists): guest `Direct3DCreate9` returns a guest-memory COM proxy whose vtable slots are thunk stubs; each stub marshals to the 64-bit `IDirect3D9`/`IDirect3DDevice9`/texture/surface/VB/IB/shader/query objects (~300 methods incl. ~119 on IDirect3DDevice9 [G count]); locked-buffer memory (`Lock`) returns guest-visible memory that is copied back on `Unlock` (bounce buffers; vertex-heavy games pay a copy per frame); D3DX9: Spacewar needs only `ID3DXFont` (~20 methods) via Wine d3dx9 on the 64-bit side; LIMBO 4 funcs; Undertale ships its own x86 D3DX9_43 (emulate it, but it will in turn call d3d9 COM → fine, same boundary).

## 5. Milestones

Dependencies: **52** play-runs-guest (loader, PEB/TEB, TLS, DllMain, entry on a guest thread: the 64-bit equivalent must exist first, M0 reuses its structure); **47** pe32 mapper (done, mapping only); **41/42/43/44/53** kernel32/user32/gdi32 behaviour; **48a/48b** D3D9; **45** donor Wine PE (version, hid...).

Acceptance lines are **proposed** exact strings; the lane that implements them should add them to `play-test.sh` output (existing format: the `launch: RESULT ok=1 ...` line seen in runs/pe32/play-480/boot.log:62 [V]). `<tid>` = a test appid for sideloaded test exes, like the gltri/d3d9tri appids planned in 48a/48b.

| # | Milestone | Smallest deliverable | Acceptance (play-test.sh output) | Effort | Depends on |
|---|---|---|---|---|---|
| **M0** | **32-bit console hello world** | `hello32.exe` built with `i686-w64-mingw32-gcc` (static CRT, msvcrt), calls GetStdHandle/WriteFile/ExitProcess. Boxedwine core extracted into `x32cpu` library (UWP build, no Linux layers), PE32 loaded into guest memory at its ImageBase, TEB32/PEB32 minimal, 20-40 generated kernel32 thunks, `emu_call`, one thread, **no RWX** | `play-test.sh <tid>`: `x32: engine=boxedwine-normal rwx=0` / `x32: entry 0x00401000 reached` / `x32: api kernel32!WriteFile ok` / `x32: hello from 32-bit` / `launch: RESULT ok=1 ... exit=0 gameCodeExecuted=true arch=i386` | **2-3 wk** [G] | 52 (shares loader/guest-thread plumbing; M0 may start in parallel offline: library extraction + offline x86 test harness need no console), item 46 (done) |
| **M1** | Real 32-bit runtime: threads, TLS, SEH, callbacks | test exe with `CreateThread`, `__declspec(thread)`, `__try/__except` (div-by-zero, AV), C++ throw/catch, a WndProc callback, `qsort` callback; mini-ntdll guest image | `x32: threads=4 ok` / `x32: tls ok` / `x32: seh AV caught` / `x32: cxx throw caught` / `x32: callback WndProc msg=WM_CREATE` / `launch: RESULT ok=1 ... arch=i386 tests=6/6` | **2 wk** [G] | M0, 42/43 for the WndProc piece |
| **M2** | **Spacewar to `Direct3DCreate9`**: 32-bit steam_api shim, all 122 imports resolved, loader reaches WinMain | `play-test.sh 480`: unresolved import count = 0 except d3d9/d3dx9; steam init OK from the 32-bit path | `x32: imports resolved=122 missing=2 (d3d9,d3dx9_42)` / `steam: SteamAPI_Init ok (32-bit shim)` / `x32: Direct3DCreate9 called` / `launch: RESULT ok=1 ... stage=d3d9-wait arch=i386` | **+2 wk** (cumulative ~7 wk with M0, M1, plus integration slip) [G] | M1, 41/42/43/53 (kernel32/user32 pieces for Spacewar), steam bridge |
| **M3** | **Spacewar on screen**: 32-bit D3D9 COM boundary thunk + ID3DXFont | `play-test.sh 480` shows ship/starfield, 300 presents | `d3d9: Direct3DCreate9 -> CreateDevice hr=0 (32-bit proxy)` / `d3d9: Present x300 hr=0` / `launch: RESULT ok=1 ... frames=300 arch=i386` + screenshot | **+4 wk** (cumulative ~11 wk) [G] | M2, **48b** (64-bit D3D9 triangle first), 44 (input) |
| **M4** | LIMBO 48000 | Loader ok at 0x400000 relocs-stripped (guest space makes it trivial), dbghelp/dinput8/xinput stubs, Steam wrapper handled (see 3.6), D3D9 shaders via 48b | `launch: RESULT ok=1 ... stage=title-screen arch=i386`; 30 s run, no crash | **+3-4 wk** [G] | M3, 48b SM3 coverage |
| **M5** | Undertale 391540 | msvcrt i386 PE + x86 D3DX9_43 emulated, ws2_32/wininet/winmm thunks, GameMaker runner (bytecode VM, CPU heavier) | `launch: RESULT ok=1 ... stage=title-screen arch=i386`, input works | **+3-4 wk** [G] | M4, 45 |
| **M6** (optional) | W^X JIT speed phase | Boxedwine JIT_X64 behind VirtualAllocFromApp/VirtualProtectFromApp; flag `x32jit=on` | `x32: jit rwx=0 blocks=N` and ≥2x frame rate vs M3/M4 baseline recorded | **+3-4 wk** [G] | M3, W^X pattern proven on console |

Offline-first note (SCHEDULING RULE, BACKLOG line ~222): M0's engine extraction, thunk generator, mini-ntdll, and x86 test exes (`i686-w64-mingw32-gcc`) are all console-free, so the lane never idles on CONSOLE.lock.

First deliverables, concretely: (1) `x32cpu` builds as a static library for the UWP target and passes a trimmed subset of source/test/cpu/testCPU.cpp on the box and on the console; (2) `hello32.exe` result lines above.

## 6. Risks

1. **Interpreter speed on Zen 2 [G].** Series S CPU is 8x Zen 2 ~3.4 GHz; the number of cores/CPU time available to a dev-mode UWP app is unmeasured here [G]. Normal core: decoded ops, indirect call per op, avg group ~5 instructions (docs/CPUemulation.md line 20-24 [V]); my planning number is ~10-30x slower than native, i.e. ~100-300 M guest instr/s per thread [G]. Spacewar (light) is fine; Undertale/LIMBO likely playable at reduced frame rates; anything with a heavy CPU-side renderer is not. Mitigation: M6 JIT, and measure early: add a fixed-workload benchmark exe in M0 and record guest MIPS on the console. **If M0 measures < ~50 MIPS, push the whole 32-bit effort behind M6.**
2. **GPL-2-or-later** code in the app (Boxedwine). Fine for personal sideload; revisit before any distribution [not legal advice].
3. **D3D9 dependency** dominates the critical path (3 of 3 games). 48b slips => everything visual slips.
4. **Soft MMU copy cost** for large buffers (textures/vertex locks); linear alias might not be allowed in UWP [G] (test early: VirtualAlloc2FromApp / MapViewOfFile3FromApp availability on the console).
5. **Cross-thread SendMessage/COM apartments** between guest threads and host user32 [G].
6. **x87/SSE correctness edge cases** (Boxedwine tests exist, but old D3D9-era games hit weird FPU control-word use) [G].
7. **LIMBO `.bind` wrapper** may need the real Steam client unwrapping path; if that cannot be done legitimately in our stack, LIMBO is out. No DRM bypass.
8. Hidden dependency: item 52 is not done even for 64-bit; 32-bit cannot be debugged faster than the 64-bit loader. Parallel M0 work should not touch the loader files that lane 52 owns.
9. i386 Wine PE builds (msvcrt etc.) under our loader are untested [G].

## 7. Recommendation, scope, and beta cuts

- **Verdict:** feasible without writing a CPU from scratch; moderate (not huge) engineering, but Spacewar is ~11 weeks of one-agent work end-to-end and it is blocked on the D3D9 layer regardless of the CPU engine.
- **Order:** finish Megabonk (52 → rendering → beta). While Codex lanes hold the console, run **M0 as a Claude/Grok offline side lane** (engine extraction + hello32). That buys the answer to the only unknowns that matter (does the extracted core build and run in UWP with no RWX; guest MIPS on the console) for ~2-3 weeks of one agent.
- **Then** M1-M3 only if João still wants 32-bit after the beta; M4/M5 are separate decisions per game.
- **Cut for beta:** all 32-bit execution except (optionally) the M0 hello-world proof; LIMBO; Undertale; the JIT (M6); 32-bit D3DX9 beyond ID3DXFont; dinput8/xinput1_3 32-bit shims; dbghelp.
- **What 32-bit must NOT block:** Megabonk (64-bit), d3d11, sqlite registry (item 50).

## Sources / paths consulted
- /workspace/x64-run/BACKLOG.md (items 46-53, status lines, scheduling rule)
- /workspace/x64-run/research/{WINE-BASE-REPORT.md,D3D-LAYERS-REPORT.md}
- /workspace/x64-run/runs/pe32/play-{480,48000}/{launch_result.json,pe32-imports-*.csv,boot.log}; runs/wgap/*; runs/wgap/pe/*; runs/cs32/
- /workspace/wt/pe32/LANE_NOTES.md (cs32 section ~line 2146; the /workspace/wt/cs32 worktree no longer exists)
- /workspace/Boxedwine-xbox-64: docs/CPUemulation.md, docs/Performance.md, license.txt, include/{boxedwine.h,platformBoxedwine.h,kmemory.h}, platform/windows/platform.cpp, source/emulation/{cpu,softmmu}/*, source/test/cpu/testCPU.cpp, source/opengl/es/esopengl.c
- /workspace/Boxedwine-uwp-upstream/README.md (core matrix; normal core used for WASM)
- /workspace/research/wine: dlls/wow64/{syscall.c,struct32.h,wow64.spec}, dlls/wow64win/*, include/winternl.h, include/winnt.h, dlls/*/*.spec
- Web (read 2026-10-26): github.com/AndreRH/hangover (README, docs/COMPILE.md, releases 11.0-11.16); github.com/ptitSeb/box64 docs/WINE.md; github.com/1rhino2/unicorn-tci, crates.io unicorn-engine-sys-tci, unicorn-engine/unicorn PR #2050 (TCI support)
- NOT verified this session: Bochs/libx86emu details, Box64 behaviour on x86_64 hosts, Steam callback struct packing, UWP availability of VirtualAlloc2/MapViewOfFile3 FromApp variants, Wine ntdll signal_i386.c content.

## Decision João needs to make
- **Scope/timing:** do you want 32-bit at all before the Megabonk beta? Recommended: no, except a ~2-3 week offline side lane for M0 (hello world + guest-MIPS measurement), then decide on Spacewar (~11 weeks total) with real numbers.
- **First 32-bit target:** Spacewar only (recommended), or also LIMBO/Undertale (+3-4 weeks each, LIMBO has a possible DRM wrapper)? All three wait on the D3D9 layer (48b).
- **Engine/licence:** accept GPL-2-or-later Boxedwine core (personal sideload only) as the engine, with Unicorn-TCI as plan B?

## x86e M0 source corrections (2026-10-26)

At fork 75b719bc, FE /7 imm8 is not a callback registry: DecodeGrp4_8 accepts
only index 1, hardwired to onExitSignal. KMemoryData::addCallback also rejects
all other functions. A standalone trap adapter must explicitly provide its own
handling. The CPU unit harness retains host KProcess/KThread/KMemory dependencies;
absence of Linux guest userland is not proof those dependencies can be dropped
without adapters. Wine .spec signatures supply stack argument classes, but not
return types, pointer lengths/direction/ownership or handle policy, so automatic
arbitrary Win32-to-x64 routing is not established. M0 can generate stack metadata
for a short explicit API allowlist and use hand-written checked marshallers.
Use an own freestanding i686 C hello fixture to bound startup requirements;
default static MinGW CRT startup needs substantially more than three APIs.
Any Play-480 hello fixture proof must say which PE ran: it is not proof that
Spacewar's original entry executes. All M0 execution/performance remains pending.
