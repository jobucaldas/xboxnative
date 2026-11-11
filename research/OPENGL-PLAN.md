# OpenGL for Play on the console: plan

Status: plan plus milestone 1 (loader route and recipe) and milestone 2 (the d3d12 driver builds, is packaged, is
mapped, and creates a GL context and presents frames off-device through both the d3d12 driver and the software
rasterizer). Nothing here claims a game renders on the console. Evidence labels:
**[verified]** read in the pinned source or run off-device, **[unverified]** needs the console.

## Problem

Several owned games render with desktop OpenGL (Fields of Mistria: Rust `glow` over SDL3's WGL path; GameMaker and
many SDL titles can too). The console has no `opengl32.dll`, so `LoadLibrary("OPENGL32.DLL")` fails there
(`error=126`, launcher 0.5.230.0 log), while Play's resolver only answers the `wgl*` state queries and fails every
pixel format and context call (`uwph/opengl32`, `uwph/gdi32/pixel_format.h`).

## Findings from this investigation

1. **The load is not an optional probe for Fields of Mistria.** The README milestone row records that the game's
   renderer is OpenGL (SDL3 WGL, `No matching GL pixel format available` off-device under Wine). The installed game
   was not available on the machine that wrote this plan (the Steam directory held only `mods/`), so this rests on
   that recorded run and was not re-derived from the game's imports. **[unverified here]**
2. **What failed on the console.** SDL's `SDL_GL_LoadLibrary` returns an error when the load fails; a Rust game that
   treats that as fatal aborts on the thread running its renderer (a thread Play did not create). The exact abort
   path was not observed. Launcher 0.5.231.0+ contains and logs such crashes. **[unverified]**
3. **Fix landed (milestone 1).** `opengl32.dll` is now a Play-owned built-in module (`uwph/opengl32/modules.h`), the
   same mechanism as the built-in XInput modules. `LoadLibrary`, `GetModuleHandle(Ex)`, `GetProcAddress`,
   `FreeLibrary` and `GetModuleFileName` behave identically on every host (no host `opengl32.dll`, so Wine and the
   console agree). A guest- or runtime-shipped `opengl32.dll` still takes precedence, which is how Mesa replaces the
   stubs below. Off-device an SDL3 program now loads the library and stops at SDL's clean
   `No matching GL pixel format available`, the same state the game reached under Wine
   (`app/tests/play-sdl3-gl.sh`). **[verified off-device]**
   Because the game's renderer is GL-only, this does not make it run on D3D11; there is no D3D11 fallback to take.

## Options

| | Mesa Gallium **d3d12** (GLOn12) | ANGLE | zink (GL on Vulkan) |
|---|---|---|---|
| API exposed | desktop GL up to 4.x core and compatibility, WGL | GLES / EGL (desktop GL only partially, via a D3D11 backend) | desktop GL |
| Backend | D3D12, which the console has | D3D11 (has) or D3D12 | Vulkan, which the console lacks |
| WGL for SDL/glow | yes (`libgl-gdi` builds `opengl32.dll`, `targets/wgl` the driver) | no WGL; games need a desktop-GL context | n/a |
| Licence | MIT | BSD-3 | MIT |

**Choice: Mesa d3d12.** Games that call `wglCreateContext` need desktop GL and a WGL surface; ANGLE does not give
that (it would only serve GLES games), and the console has no Vulkan for zink. Mesa builds exactly the two pieces
needed and already carries a build mode for Xbox (`_GAMING_XBOX`, `d3d12_wgl_framebuffer_xbox.cpp`, a GDI shim
header), which shows the WGL frontend can run without a real GDI. **[verified in the 25.3.6 source]**

## Architecture

```
game ──> opengl32.dll (Mesa libgl-gdi: gl*/wgl*) ──> libgallium_wgl.dll (stw frontend + d3d12 driver)
                                                         │  D3D12CreateDevice / DXGI swapchain
Play: maps both from the runtime layout like Wine's msvcp140 ──> console d3d12.dll / dxgi.dll
      gdi32 forwarders (ChoosePixelFormat, SetPixelFormat, GetPixelFormat, DescribePixelFormat, SwapBuffers)
      ──> opengl32.dll's wgl* when it is the mapped module, else the current failing stubs
```

* The two Mesa DLLs are built from the pinned source (`src/tools/build-mesa-d3d12.sh`), packaged beside the donor
  DLLs and mapped by Play as the "runtime" modules are. Game-shipped GL DLLs keep precedence. **[verified off-device]**
  The build is UCRT-flavoured mingw-w64: the console has no `msvcrt.dll`, and the script now refuses a compiler that
  links it (it probes a trivial program's imports before configuring) and carries the compiler path in its cache key,
  so a cached msvcrt-configured tree can no longer be reused by a UCRT compiler.
* Presentation: Mesa's `d3d12_wgl_framebuffer.cpp` creates its swapchain with `CreateSwapChainForHwnd` (or
  composition) on a DXGI factory. Play already hands guests a factory that maps a guest HWND to the app's CoreWindow
  (`guest DXGI factory uses validated CoreWindow adapter` in the launch log). The patch makes the framebuffer use
  three buffers, no tearing/waitable flags, `AlphaMode = IGNORE` (so it takes the HWND path, not DirectComposition)
  and a Present that never waits on an absent waitable object. **[verified in source; not run on the console]**
* Window integration: `stw_framebuffer.c` subclasses the window procedure and queries the client rect; the user32
  record core ("u32c") supports `SetWindowLongPtr(GWLP_WNDPROC)`/`CallWindowProc`, `GetClientRect` and
  `GetWindowDC` (added for Mesa's presenter import). **[verified off-device]**
* GDI use in the stw frontend (`GetDC`, `ReleaseDC`, `WindowFromDC`, DIB blits for the software path) is served by
  u32c/gdi32 records; `StretchDIBits` answers `ERROR_NOT_SUPPORTED` (the softpipe winsys links it, the d3d12 path
  never calls it). **[verified off-device]**

## Required pieces, in order

1. **Done: built-in `opengl32.dll` module** (milestone 1). Loading no longer fails; WGL stays failing cleanly.
2. **Done: recipe and build** (`src/tools/build-mesa-d3d12.sh`): pinned `mesa-src` (SHA-256), `meson setup` for a
   UCRT mingw cross build with only the d3d12 driver and WGL targets (no LLVM, Vulkan, GLES/EGL/GLX, video).
   `--dry-run` runs in CI; `--build` compiles and gates the result. **[verified: the DLLs build]**
   * `opengl32.dll` (456 KB) exports 20 `wgl*` and 336 `gl*` entry points; `libgallium_wgl.dll` (30 MB) carries the
     stw frontend and both drivers. Both import only OS DLLs (`kernel32`, `ntdll`, `user32`, `gdi32`, `shell32`,
     `version`, `api-ms-win-crt-*`) and have no writable+executable section. **[verified off-device]**
   * The toolchain must be UCRT-flavoured: env.sh's mingw links `msvcrt.dll`, which the console lacks, and the gate
     caught exactly that. NixOS: `pkgsCross.ucrt64.stdenv.cc`; its libstdc++ is mcf-threaded, so the build needs
     `MCFGTHREAD_LIBDIR` and `MCFGTHREAD_INCDIR` (headers for `<mcfgthread/gthr.h>`, which `<thread>` includes).
   * GNU ld emits zero-length x64 unwind entries that Play's loader refuses; `src/tools/pe-drop-empty-pdata.py`
     drops exactly those from our own output (`opengl32.dll`: 0, `libgallium_wgl.dll`: 23 in this build).
3. **Done: DirectX-Headers pin.** `deps.pin` records the 40-hex commit `64c650b0…` (v1.618.1); the script checks the
   tag in Mesa's wrap, clones that tag and verifies the commit. **[verified]**
3b. **Done: command-queue fallback** (`patches/mesa-src/0002-play-d3d12-command-queue-fallback.patch`). This was the
   blocker between "device created" and "zero pixel formats": `d3d12_init_screen_command_queue` calls
   `ID3D12Device9::CreateCommandQueue1(OpenGLOn12CreatorID)` whenever the device exposes that interface and treats
   any failure as fatal, with no fallback. Wine's d3d12 exposes `ID3D12Device9` and answers `E_NOTIMPL`, so the screen
   never initialised, `stw` had no adapter formats, and the guest saw `No matching GL pixel format available`. The
   patch falls back to `ID3D12Device::CreateCommandQueue`; a creator ID only tags the queue for tooling. On a D3D12
   implementation that honours creator IDs nothing changes. **[verified off-device: Wine 11]**
   The diagnostic path, if this ever regresses: `MESA_DEBUG`/`debug_printf` is compiled out of release builds, so the
   trace comes from `OutputDebugStringA` (Play logs it) — the screen init chain is `d3d12_wgl_create_screen` →
   `d3d12_create_dxgi_screen` → `d3d12_init_screen`, and a NULL there leaves `stw` with zero formats.
3c. **Done: truthful WGL errors** (`patches/mesa-src/0010-play-wgl-setlasterror.patch`). With the pixel format fixed,
   the next reported stop was still `No matching GL pixel format available`, but the string is SDL3's stale fallback:
   the real failure was `wglCreateContextAttribsARB` returning NULL with `GetLastError() == 0`. Mesa's WGL frontend
   lost the state tracker's reason twice — `stw_create_context_attribs()` never mapped `enum st_context_error` to
   `SetLastError()`, and the failure path's `pfnwglDeleteContext()` clobbered what it did set. The patch maps
   `NO_MEMORY -> ERROR_OUTOFMEMORY` and `BAD_VERSION -> ERROR_INVALID_VERSION_ARB` (0x2095) at the funnel both entry
   points share and preserves the error across the cleanup call. Why the request is refused at all: the d3d12 driver
   needs `PIPE_FORMAT_R9G9B9E5_FLOAT` for `EXT_texture_shared_exponent`, and a D3D12 that answers
   `CheckFeatureSupport(FORMAT_SUPPORT)` with E_FAIL for `DXGI_FORMAT_R9G9B9E5_SHAREDEXP` (Wine 11's vkd3d and, per
   the format-support probing, the console's) drops the extension, which caps `compute_version` at GL 2.1 — so every
   >= 3.0 request takes the `BAD_VERSION` path. Off-device the refusal now reports `err=8341`
   (`ERROR_INVALID_VERSION_ARB`) and the SDL3 fixture still presents its frame; the opengl fixture gained a
   version-refusal stage and the off-device gate greps for it, so an old Mesa build without the patch fails the
   gate. **[verified off-device: Wine 11]**
4. **Done: package + map.** `app/build.sh` copies both DLLs, the licences, the patches and `SOURCE.txt` into
   `layout/runtime/`; `loadExW` finds them there (after any copy the game ships) through `redistributable()`.
   Mesa's imports resolve through the existing routes; `d3d12.dll!D3D12CreateDevice` is routed with the real
   adapter (as `D3D11CreateDevice` is) and logged as `guest D3D12CreateDevice request adapter=… minimum=… iid=…`
   then `… returned hr=… device=…`. **[verified off-device]**
5. **Done: gdi32 forwarders.** `ChoosePixelFormat`, `SetPixelFormat`, `GetPixelFormat`, `DescribePixelFormat` and
   `SwapBuffers` forward to the mapped `opengl32.dll`'s `wgl*` when it is mapped, and fail cleanly otherwise
   (`GetPixelFormat` sets `ERROR_INVALID_PIXEL_FORMAT`). **[verified off-device]**
6. **DXGI factory level 4.** Mesa asks `CreateDXGIFactory2` for `IDXGIFactory4` and calls `EnumAdapterByLuid`;
   Play's wrapper answers `IDXGIFactory3`..`IDXGIFactory7` when the real factory has the level (the D3D12 routes),
   so the CoreWindow adapter reaches Mesa. **[verified off-device]**
7. **D3D12 on the console from Play**: Mesa calls `D3D12CreateDevice` itself; Play routes it with the real adapter.
   Whether the console's D3D12 accepts Mesa's device creation has to be tested on the device. **[unverified]**
   What the driver needs from the backend at runtime, read in the pinned source and confirmed off-device: a D3D12
   device at feature level ≥ 11.0 (else the queue is a compute queue), `CheckFeatureSupport` for options/architecture/
   feature levels/shader model and per-format support, a command queue, a shared fence, and DXGI adapters by LUID.
   `CreateCommandQueue1` is optional (see 3b). DXIL validation is optional (see 8).
8. **DXIL validation.** Read in the pinned source: `d3d12_context_common.cpp` only creates the validator when
   `D3D12_DEBUG` does *not* include `experimental`; without `dxil.dll` on the search path `dxil_create_validator`
   fails and Mesa proceeds with `NO_DXIL_VALIDATION`. With `D3D12_DEBUG=experimental` Mesa instead asks the device
   factory to `EnableExperimentalFeatures(D3D12ExperimentalShaderModels)`, and on the GDK X path it refuses
   experimental models outright. `dxil.dll` is now mappable from `runtime/` (optional signer, game copies first).
   Which of these the console accepts is **[unverified]**.
9. **Acceptance**: `app/tests/play-sdl3-gl.sh` runs the SDL3 GL path twice off-device: the default (d3d12) pass and a
   `GALLIUM_DRIVER=softpipe` pass that must present a frame. Both passes present a frame here (Wine 11.0 with its
   built-in vkd3d, Intel HD 4000 through vkd3d): d3d12 reports `renderer="D3D12 (Intel(R) HD Graphics 4000)"`,
   `version="2.1 Mesa 25.3.6"`; softpipe reports `renderer="softpipe"`, `version="3.3 (Compatibility Profile)"`.
   The d3d12 swapchain is created through Play's CoreWindow adapter (`guest DXGI CoreWindow swapchain returned
   hr=0x00000000`). On the console, via the launcher with Fields of Mistria (`play-test.sh 2142790`), with evidence
   in the PR. **[verified off-device; console unverified]**

## Licensing and policy

Mesa and DirectX-Headers are MIT; listed in `NOTICE`; built from pinned sources, outputs git-ignored, never
committed (AGENTS.md rules 5 and 9). No game or Valve files are touched; the GL stack is substituted in memory like
the other Play modules. Nothing here weakens a gate: the SDL fixture still prints `BLOCKED` (never PASS) for a stage
the host cannot run rather than counting it as a pass.

## Not covered

D3D9/D3D10 games, GLES-only games (ANGLE could serve them later and is orthogonal), 32-bit OpenGL (the x86 plan in
`X86-32BIT-PLAN.md` would need a 32-bit Mesa build).
