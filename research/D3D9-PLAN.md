# Direct3D 9 for Play on the console: decision record

Status: decision plus milestone 1 (a D3D9 shim that loads in Play, creates a device through the CoreWindow
swap-chain adapter, clears and presents). Nothing here claims a game renders on the console. Evidence labels:
**[measured]** built or run on this machine, with the command or log line named, **[read]** read in the pinned
source, **[unverified]** needs the console.

## Problem

The console ships `d3d11.dll` and `dxgi.dll` (both `d3d11on12`) and no `d3d9.dll` (research/D3D-LAYERS-REPORT.md
§1; the launcher's own `import_map.json` records the same). A guest that imports `d3d9.dll` — statically or
through `LoadLibrary` — therefore fails to load or to get a device, before it draws a frame. D3D9 is the API of
the older half of the catalogue (Spacewar-class samples, GameMaker and older Unity titles, many 2D games).

### Which owned games actually need it

Static imports of every EXE and DLL under `<local steam-owned games directory>` (39 files, `llvm-readobj --coff-imports`):

| Game | Renderer imports | D3D9? |
|---|---|---|
| Megabonk | `UnityPlayer.dll`: d3d11, d3d12, dxgi, opengl32 | no static import; `d3d9.dll` appears only as a string |
| Fields of Mistria | `SDL3.dll`: no graphics import; strings `D3D9.DLL`, `SDL.renderer.d3d9.device`, `SDL_WINDOWS_USE_D3D9EX` | no static import (SDL's optional renderer) |
| Monster Hunter Wilds (hwde) | `xgameFinal.exe`: d3d11, dxgi | no (`D3DCompiler_46.dll` names d3d10 in strings only) |
| Mega Man Legacy Collection (mmlc) | `Proteus.exe`: d3d11 | no |
| Puyo Puyo Tetris 2 | `PuyoPuyoTetris2.exe`: d3d11, dxgi | no |
| Slime Rancher | `UnityPlayer.dll`: opengl32 | no |
| Say No! More (snm) | `UnityPlayer.dll`: opengl32 | no |

So **no owned game requires D3D9 today**, and none is lost without it. The work is for the catalogue rather than
for a title in hand, and it is worth exactly as much as the compatibility it buys. That shapes the decision below:
the cheap, broad option wins over the one that would be best in isolation.

## Measured evidence

### Wine's d3d9 + wined3d build as PE DLLs from the pinned source

Built from the same pinned `wine-src` tarball the donors use (`deps.pin`, SHA-256 verified), with the donor tree's
already-configured mingw build:

```
make -j8 dlls/d3d9/all dlls/wined3d/all        # exit 0
```

`llvm-readobj --coff-imports` on the results:

| Image | Size | Imports |
|---|---|---|
| `d3d9.dll` | 1.9 MB | `wined3d.dll` (175 symbols), `ucrtbase.dll` (19), `kernel32.dll` (11), `ntdll.dll` (1) |
| `wined3d.dll` | 25.8 MB | `ucrtbase.dll` (68), `user32.dll` (54), `kernel32.dll` (54), `gdi32.dll` (25), `opengl32.dll` (9), `ntdll.dll` (4), `advapi32.dll` (4) |

Both have no writable-executable section. **[measured]**

The `wined3d.dll` import list is the whole problem in one table. It needs, from Play:

* **`opengl32.dll`** — `wglCreateContext`, `wglMakeCurrent`, `wglGetProcAddress`, `glEnable`/`glDisable`. Play's
  built-in `opengl32` module answers state queries and fails context creation; Mesa's GLon12 `opengl32.dll`
  (research/OPENGL-PLAN.md, merged) serves them. **[read]** — wined3d loads GL through `GetModuleHandleA("opengl32.dll")`
  and `GetProcAddress` (`dlls/wined3d/adapter_gl.c:5006-5013`), so it finds whichever module Play mapped under that
  name, which is exactly the Mesa one.
* **`gdi32.dll`** — 25 symbols, of which **9 are `D3DKMT*`**: `D3DKMTOpenAdapterFromLuid`,
  `D3DKMTCreateDevice`, `D3DKMTSetVidPnSourceOwner`, `D3DKMTQueryVideoMemoryInfo`, and friends. Play's gdi32
  route implements the pixel-format and DC surface and **none** of the D3DKMT family; today they resolve to
  failing stubs (`MLP STUB gdi32.dll!D3DKMTEnumAdapters2 route=stub execution=reject` in the Vulkan fixture log).
  wined3d calls them on the adapter-creation path (`dlls/wined3d/directx.c:3438`, `wined3d_adapter_init`), and
  returns failure if `D3DKMTOpenAdapterFromLuid` fails, so **the adapter never comes up without a D3DKMT shim**.
* **`user32.dll`** — 54 symbols, mostly display enumeration and window management (`EnumDisplayDevicesW`,
  `EnumDisplaySettingsExW`, `ChangeDisplaySettingsExW`, `MonitorFromWindow`, `GetDC`). Play's `u32c` record core
  serves the window half; the display-enumeration half is not implemented (the console has one virtual display).
* **`ucrtbase.dll`** — the console has it (import_map class `system`), so this is free.

### The GL path's shader translation

wined3d compiles D3D9 shaders to GLSL through vkd3d-shader: `dlls/wined3d/glsl_shader.c:781` sets
`info.target_type = VKD3D_SHADER_TARGET_GLSL`, and `vsir_program_compile` handles `VKD3D_SHADER_TARGET_GLSL`
(`libs/vkd3d/libs/vkd3d-shader/vkd3d_shader_main.c:1659-1667`), so the pinned vkd3d does support the target
wined3d needs. The path is D3D9 bytecode → vkd3d IR → GLSL → Mesa's NIR → DXIL → D3D12. **[read]**

Fixed-function D3D9 (the common case for the target games) goes through `dlls/wined3d/ffp_hlsl.c`, which builds
an HLSL source and compiles it with `vkd3d_shader_compile` (`ffp_hlsl.c:865`). **[read]**

### The present path

`swapchain_gl_present` (`dlls/wined3d/swapchain.c:583`) calls `gl_info->gl_ops.wgl.p_wglSwapBuffers(context_gl->dc)`
at line 627 — a `wglSwapBuffers` on the device's DC. Mesa's GLon12 `opengl32.dll` has no `wglSwapBuffers`; the
console's present is the CoreWindow swap chain. Play's gdi32 `SwapBuffers` already forwards to the mapped
`opengl32.dll`'s `wgl*` when one is mapped (research/OPENGL-PLAN.md, milestone 1), so a `wglSwapBuffers` entry
point on the Mesa side (or a gdi32 route that presents the CoreWindow chain) is the missing link. **[read]**

## Options

| | A: Wine d3d9 + wined3d over Mesa GLon12 | B: own D3D9→D3D11 shim from MIT donors | C: wined3d + new D3D11 backend |
|---|---|---|---|
| Code we write | D3DKMT family (~9 calls), display enumeration (~10), `wglSwapBuffers` presentation | ~108 `IDirect3DDevice9` methods, fixed-function state → D3D11 pipeline, SM1-3 → DXBC | a wined3d backend adapter (adapter_d3d11.c) plus D3D9→DXBC translation |
| Reuse | Wine's complete D3D9 (SM1-3, FFP, D3D9Ex, D3D8/DDraw later) | DXUP front-end (zlib, archived 2019), D3D9On12 ShaderConverter (MIT), dx9asm | wined3d's command stream and state tracking |
| Translation chain | D3D9 → GLSL → NIR → DXIL → D3D12 | D3D9 → DXBC → D3D11 → D3D12 | D3D9 → DXBC → D3D11 |
| Risk | D3DKMT semantics, GL-on-D3D12 shader stutter, Mesa WGL present | fixed-function is the hard part (DXUP has none: `SetTransform` is a log stub) | largest new code, no precedent |
| Effort to a D3D9 triangle | **2-4 weeks** | 6-10 weeks | 2-3 months |
| Compatibility ceiling | highest (Wine's D3D9 is the reference open implementation) | medium (FFP has to be rebuilt) | high |

DXVK-on-dzn and dgVoodoo2 stay ruled out for the reasons in research/D3D-LAYERS-REPORT.md §2 (dzn lacks the
Vulkan features DXVK needs; dgVoodoo2 is closed and self-unpacks into RWX).

## Decision

**Option A, Wine's d3d9 + wined3d over Mesa GLon12**, built from the pinned Wine source like the other donors.

Reasons, in order:

1. It is the only option whose missing pieces are *small and enumerable*: 9 D3DKMT calls, a display-enumeration
   table, and a present hook. Option B's missing piece is a fixed-function pipeline, which is not enumerable and
   is where DXUP stopped.
2. It reuses Wine's D3D9, which is the most compatible open implementation; the target games are exactly the ones
   whose quirks Wine has spent fifteen years absorbing.
3. The GL half is already merged on main (Mesa GLon12, PR #94) and is the same stack that runs shipped homebrew
   on retail Xbox in Dev Mode.
4. No owned game needs it yet, so the cheap broad option is the right bet; if the GL path's frame times on the
   console turn out to be unacceptable, option C is the fallback and reuses the same d3d9 front-end.

**Rejected:** option B as the first path (fixed-function cost, archived donors), option C as the first path (no
precedent, largest new code). Both remain open as follow-ups; the decision gate is a frame-time measurement on the
console with a many-draw D3D9 scene.

## Milestone 1 (this branch)

The brief's milestone 1 is "d3d9.dll loads in Play, `Direct3DCreate9`/`Direct3DCreate9Ex` succeed, a minimal device
(clear + present) works through the CoreWindow swap-chain adapter". Option A cannot reach that milestone inside a
branch: `Direct3DCreate9` in Wine's d3d9 walks into `wined3d_init`, which needs the D3DKMT adapter and a GL
context, and neither exists off-device in Play yet. So milestone 1 is built as **Play's own D3D9-on-D3D11 shim**
(`src/xbox-test/uwph/d3d9`), the front-end half of option B, which is also the shape option A's d3d9.dll would
present to a guest.

What milestone 1 delivers:

* `d3d9.dll` maps in Play from the launcher's runtime directory (class `uwph`), like the D3D10 shim.
* `Direct3DCreate9` and `Direct3DCreate9Ex` return a real `IDirect3D9`/`IDirect3D9Ex`.
* `IDirect3D9::CreateDevice` creates a D3D11 device through Play's route and a swap chain through the validated
  CoreWindow adapter; `Clear`, `BeginScene`/`EndScene`, `Present` and `Reset` work.
* The rest of `IDirect3DDevice9` answers honestly: what is implemented works, what is not returns the D3D9 failure
  code the API defines (`D3DERR_INVALIDCALL`/`E_NOTIMPL`) rather than pretending.

What it does **not** deliver: shaders, fixed-function transforms, drawing, textures. Those are option B's 6-10
weeks and are not started. Nothing claims a game runs.

## Milestone 2 and after (option A)

1. **D3DKMT shim** in `uwph/gdi32`: `D3DKMTOpenAdapterFromLuid` (return a synthetic adapter for the console's
   LUID), `D3DKMTCreateDevice`/`DestroyDevice`, `D3DKMTSetVidPnSourceOwner` (report success: the CoreWindow owns
   the output), `D3DKMTQueryVideoMemoryInfo` (report the D3D11 adapter's budget). Gate: Wine's `wined3d_init`
   reaches `wined3d_adapter_gl_init` instead of failing at the adapter.
2. **Display enumeration** in `uwph/user32`: `EnumDisplayDevicesW`/`EnumDisplaySettingsExW` describing the
   CoreWindow's single output, so wined3d finds one attached output.
3. **Present**: give the Mesa `opengl32.dll` a `wglSwapBuffers` that presents the CoreWindow chain, or route
   gdi32's `SwapBuffers` for wined3d's DC. Gate: `swapchain_gl_present` returns and a frame reaches the screen.
4. **Build recipe**: `src/tools/build-wine-d3d9.sh` (deps.pin already carries `wine-src`), producing `d3d9.dll`,
   `wined3d.dll` and the vkd3d PE library, with the same import gate the D3D10 shim build has.
5. **Acceptance**: a D3D9 triangle fixture (FFP + one SM3 shader) through `play-test.sh`, then a real D3D9 title.

## Honest uncertainty

* `wined3d.dll` was built and its imports were read, but it has never been loaded by Play. The D3DKMT and display
  enumeration gaps are from the import list and the call sites, not from a run. **[unverified]**
* Mesa GLon12's frame times on Series S for the wined3d workload (many small draws, shader compile at first use)
  are unmeasured. This is the decision gate.
* `wglSwapBuffers` is not among the 9 `opengl32.dll` symbols wined3d imports statically; it resolves it with
  `GetProcAddress` (`adapter_gl.c:5013`), so a missing entry point fails at present time rather than load time.
  **[read]**
* The D3D9 shim's fixed-function surface is stubbed, so a game that uses it will draw nothing. That is milestone
  1's stated limit, not a claim.
* 32-bit D3D9 (Melty Blood AACC) is a separate effort (research/X86-32BIT-PLAN.md); nothing here changes it.
