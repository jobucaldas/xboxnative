# OpenGL for Play on the console: plan

Status: plan plus milestone 1 (loader route and recipe). No OpenGL context can be created yet, and nothing here
claims a game renders. Evidence labels: **[verified]** read in the pinned source or run off-device, **[unverified]**
needs the console or a build this machine could not run.

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
  DLLs and mapped by Play as the "runtime" modules are. Game-shipped GL DLLs keep precedence.
* Presentation: Mesa's `d3d12_wgl_framebuffer.cpp` creates its swapchain with `CreateSwapChainForHwnd` (or
  composition) on a DXGI factory. Play already hands guests a factory that maps a guest HWND to the app's CoreWindow
  (`guest DXGI factory uses validated CoreWindow adapter` in the launch log). **[verified in source; not run]**
* Window integration: `stw_framebuffer.c` subclasses the window procedure and queries the client rect; the user32
  record core ("u32c") must support `SetWindowLongPtr(GWLP_WNDPROC)`/`CallWindowProc` and `GetClientRect` for
  Play's records. Review needed.
* GDI use in the stw frontend (`GetDC`, `ReleaseDC`, `WindowFromDC`, DIB blits for the software path) must be served
  by u32c/gdi32 records; the d3d12 path avoids `StretchDIBits`.

## Required pieces, in order

1. **Done: built-in `opengl32.dll` module** (this PR). Loading no longer fails; WGL stays failing cleanly.
2. **Recipe: `src/tools/build-mesa-d3d12.sh`** (this PR): pinned `mesa-src` (SHA-256), `meson setup` for a mingw
   cross build with only the d3d12 driver and WGL targets (no LLVM, Vulkan, GLES/EGL/GLX, video). `--dry-run` runs
   in CI. The default run configures and `--build` compiles. This machine could not run meson (no network, no
   meson in the store) so the configure itself is **[unverified]**; option names and types were checked statically
   against Mesa's `meson.options`.
3. **DirectX-Headers pin.** Mesa's only needed subproject is fetched by tag as Mesa's own wrap does. The 40-hex commit
   still has to be recorded in `deps.pin` (the script prints it and enforces it once present). Mesa 25.3.6 needs
   `>= 1.618.1`.
4. **Package + map**: copy the DLLs to the layout, let `loadExW` find them in the runtime directory, import routing
   (Mesa imports user32/gdi32/advapi32/ws2_32/ole32 names; the strict policy must classify each).
5. **gdi32 forwarders**: route the pixel-format and `SwapBuffers` entry points to the mapped GL module.
6. **D3D12 on the console from Play**: `apibadge` currently labels D3D12 unsupported for games. Mesa calls
   `D3D12CreateDevice` itself, so the console's D3D12 must be reachable and usable by a guest module. **[unverified]**
7. **DXIL validation (open risk).** Mesa's d3d12 driver emits DXIL; its `dxil_validator` signs through `dxil.dll` /
   `dxcompiler.dll` if present and otherwise relies on `D3D12EnableExperimentalFeatures(ExperimentalShaderModels)`
   (developer mode). Whether the console runtime accepts either has to be tested on the device; if not, a validator
   from a pinned source (DXC, Apache-2.0 with LLVM exceptions) would be a further `deps.pin` entry. **[unverified]**
8. **Acceptance**: `app/tests/play-sdl3-gl.sh` flips from "window/context BLOCKED" to PASS first off-device with a
   software ICD path where possible, then on the console via the launcher with Fields of Mistria
   (`play-test.sh 2142790`), with evidence in the PR.

## Licensing and policy

Mesa and DirectX-Headers are MIT; listed in `NOTICE`; built from pinned sources, outputs git-ignored, never
committed (AGENTS.md rules 5 and 9). No game or Valve files are touched; the GL stack is substituted in memory like
the other Play modules. Nothing here weakens a gate: the new SDL fixture prints `BLOCKED` for window/context creation
rather than counting it as a pass.

## Not covered

D3D9/D3D10 games, GLES-only games (ANGLE could serve them later and is orthogonal), 32-bit OpenGL (the x86 plan in
`X86-32BIT-PLAN.md` would need a 32-bit Mesa build).
