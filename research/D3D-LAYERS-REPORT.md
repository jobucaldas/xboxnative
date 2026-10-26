# D3D layers for Xbox Steam (UWP, W^X only, no D3D9/GL/VK in OS)
Written 2026-10-26 (BRT). Research only, no product code touched. Refines BACKLOG item 48.
Question (pt136u): "can we dxvk to direct3d 11 or some similar project?"

## 0. TL;DR
- There is no "DXVK to D3D11". DXVK's D3D9 front-end is welded to its Vulkan backend. The only open D3D9->D3D11 projects (DXUP, d3d9-to-11) are archived and incomplete. (crosire "d3d9to11" does not exist; 404.)
- Best reuse path = **Wine `d3d9`+`wined3d` (GL backend) on Mesa GLon12 (OpenGL over D3D12), which ALREADY runs inside Xbox UWP** (aerisarn/mesa-uwp: Doom3/dhewm3, RetroArch, gzdoom, Citra, Supermodel on retail console in Dev Mode). Everything is source, LGPL/MIT, builds as W^X-clean PE DLLs. Covers D3D9 SM1-3, fixed function, D3D9Ex, plus D3D8/DDraw/D3D1-7 as extra Wine DLLs, and any GL game.
- Vulkan route (DXVK on Mesa dzn) is **blocked**: dzn lacks features DXVK needs and DXVK itself says dzn cannot run it.
- dgVoodoo2 is **out**: closed source, "no bundling" licence, and the DLLs are packed in one RWX section (entropy 0 + 7.92). Breaks W^X.
- D3D9On12 (MIT, 35k lines) is a DDI driver, not a runtime; needs the OS d3d9.dll we lack. But its ShaderConverter (15k lines, MIT, SM1-3 + fixed-function VS -> DXBC) is a great donor for the long-term own-shim option.
- Recommendation: Lane A = Mesa GLon12 UWP as a loadable DLL + GL triangle. Lane B = wined3d/d3d9 PE + our gdi32/opengl32 glue, D3D9 triangle, then Spacewar. Long-term option = a D3D11 backend for wined3d (reuse wined3d + donors) if GL-on-D3D12 perf/shader stutter is a problem.

## 1. Constraints that drive the ranking
- Our wxl loader maps PE DLLs itself and resolves imports via import_map. A vendored DLL is fine if its imports route to something we implement and it has no RWX sections / runtime codegen. W^X via VirtualAllocFromApp/VirtualProtectFromApp.
- Real d3d11/d3d12/dxgi exist. D3DCompile (d3dcompiler_47) is documented as available to UWP/Xbox (walbourn blog, devdocs.xbox.com), unverified on our device (our d3tri compiles offline on purpose).
- HWND swapchain: lane swc (item 37) already presents an HWND on the CoreWindow. HDC/GL-context creation on HWND still needs glue (see Lane B).
- 32-bit: Spacewar/LIMBO/Undertale are PE32. Any 32-bit D3D layer would call 64-bit d3d11 via ~hundreds of COM thunks. Cheaper: run the layer as 64-bit and thunk only the D3D9 boundary (IDirect3D9/Device9/Texture9/VB9/IB9/Surface9/Query9/Shader9/StateBlock ~300 methods, pointers < 4GB) from the 32-bit side. Applies to every candidate; do not compare candidates on 32-bit.
- Games believed to be D3D9: Spacewar 480 (SteamworksExample, d3d9.h + D3DX on Windows; ToGL is macOS only), LIMBO 48000 (SM3, believed), Undertale 391540 (GameMaker Studio 1.x runner, D3D9, believed). Verify with the import list (item 47/40 output) before betting.

## 2. Candidates
Legend effort = to first working game on console, rough.
| # | Candidate | Licence | What it is / coverage | W^X + UWP fit | Integration | Series S perf | Precedent | Effort-to-first-game |
|---|---|---|---|---|---|---|---|---|
| 1 | **Wine wined3d GL + Mesa GLon12 (mesa-uwp)** | LGPL-2.1 (wine) + MIT (mesa) | wined3d: most complete D3D9 (SM1-3 via vkd3d-shader d3dbc->GLSL, ffp_hlsl/ffp_gl fixed function, D3D9Ex via wine d3d9; D3D8 via wine d3d8; ddraw). Wine 10 tree here: wined3d 88k lines, d3d9 front ~10k. | Both are plain C/C++ PE. wined3d imports only opengl32,user32,gdi32,advapi32,dxguid,vkd3d (Makefile.in). Uses plain WGL (wglCreateContext, wglGetProcAddress; adapter_gl.c:286,356). Mesa d3d12 gallium needs DXIL (see risks); mesa-uwp patches are tiny (4 files `_XBOX_UWP`). | Medium: build mesa-uwp (2024-03-30, 2c7c63e, Mesa 24.0 era; stale) or rebase patches; our gdi32/opengl32 glue for HDC/pixel format/SwapBuffers->CoreWindow; build wine d3d9+wined3d+vkd3d-shader with mingw (PE, no unix side for the GL path). | Mid: D3D9->GLSL->NIR->DXIL, shader compile stutter at first use. Fine for 2D/indie (Spacewar, LIMBO, Undertale) | GLon12 on Xbox UWP: dhewm3 HDR, RetroArch (commit 8b1e4a1), gzdoom-uwp, Citra, Supermodel. Wine d3d9 over GL = Wine's default for 15 yrs. | **2-4 weeks** (best) |
| 2 | Own thin D3D9->D3D11 shim from donors (DXUP front-end ~2k lines device code + dx9asm SM1-3->DXBC 3k + D3D9On12 ShaderConverter + wined3d ffp_hlsl) | zlib / MIT / LGPL | DXUP: D3D9 API->D3D11, **SetTransform etc. are log stubs, no fixed function**, 1 commit 2019. d3d9-to-11 (GabrielMajeri): archived WIP. | Clean (own code). No GL/DXIL dependency. Uses only d3d11 + D3DCompile (optional). | Large: ~2-3 months for SM3+FFP+D3D9Ex breadth; best perf, full control. | Best | none for Xbox | 6-10 weeks to Spacewar-class, longer for LIMBO |
| 3 | wined3d + **new D3D11 backend** (adapter_d3d11.c) | LGPL | wined3d has a backend vtable (adapter_gl/adapter_vk/noop) + command stream (cs.c). vkd3d-shader d3dbc outputs only SPIR-V / GLSL / D3D_ASM in Wine 10 (not TPF) so we need d3dbc->HLSL or ->DXBC translation (donor: ShaderConverter, dx9asm). | Clean | Large; do only if #1 perf fails | Good | none | 2-3 months |
| 4 | DXVK d3d9 + Mesa **dzn** (Vulkan on D3D12) | zlib (DXVK) + MIT | Best D3D9 compat on Linux. dzn exposes Vulkan 1.2 but table (dzn_device.c:73-158) has NO robustness2, transform_feedback, depth_clip_enable, maintenance5/6, extended_dynamic_state; dualSrcBlend=false, logicOp=false, fullDrawIndexUint32=false. DXVK 3.x needs Vulkan 1.4 + those; DXVK wiki says dzn cannot run DXVK. | dxvk d3d9.dll v3.1.1 x64 is W^X-clean (.text RX only) but imports D3DKMT, SetupAPI, advapi32 registry, user32 display APIs (need stubs). Needs vulkan-1.dll (not in UWP; tiny forwarder to dzn possible). dzn UWP port: only 1 `_XBOX_UWP` tweak in mesa-uwp, "Vulkan/UWP less validated". | Large + fork DXVK to drop required features, double shader translation (SPIR-V->NIR->DXIL) | Heavy | no known UWP/Xbox DXVK | **unlikely to work; 2+ months if tried** |
| 5 | D3D9On12 | MIT (Microsoft) | DDI-level D3D9->D3D12 driver (35k lines + D3D12TranslationLayer). `OpenAdapter_Private` expects the OS D3D9 *runtime* (d3d9.dll) to call it. No runtime in UWP. | Needs WDK headers, dxbcsigner.dll, dxilconv, WinPixEventRuntime | Writing the missing D3D9 runtime = a big project | Good | none | Not recommended; **harvest ShaderConverter** |
| 6 | dgVoodoo2 2.87.6 | **Closed**, freeware; "cannot bundle inside launchers/frameworks for general use"; unmodified per-game ship OK | D3D9/D3D8/DDraw/Glide -> D3D11/12, mature, x64 + x86 DLLs | **Packed**: x64 D3D9.dll sections: one RWX 1.3 MB empty section (ent 0.00) + RWX 557 KB high-entropy stub (7.92); only 12 imports (VirtualProtect, LoadLibraryA, GetProcAddress...). Self-unpacking into RWX => fails W^X unless we write a fault-driven RW->RX unpacker. Cannot modify (EULA). | Drop-in if it worked | Fast | Excellent compat | none | Out. (1-2 day spike possible only to see if our mapper can unpack; low value) |
| 7 | d3d8to9 (crosire) / DxWrapper (elishacloud) | BSD-2 / zlib-mixed (`License.txt`) | D3D8->D3D9, DDraw->D3D9 shims. Add-on for old titles only | Clean, small | Small once a D3D9 layer exists | n/a | - | Add-on after #1 |
| 8 | ANGLE (GLES->D3D11) | BSD-3 | GLES 2/3 only: no D3D9, no desktop GL. Real UWP/Xbox use (RetroArch msvc-uwp ships xbox-homebrew/angle 239b5a4) | Fine | n/a for D3D9 | Good | Strong | Only for GLES games (Unity GLES builds, some emulators) |
| 9 | Wine wined3d Vulkan backend (adapter_vk.c) | LGPL | Experimental in Wine 10 (FIXMEs), needs Vulkan -> dzn gap again | - | - | - | - | No |
| 10 | SwiftShader / Mesa lavapipe (CPU) | Apache / MIT | CPU Vulkan with JIT (RWX) | Fails W^X | - | Slow | - | No |
| 11 | Mesa Nine / DXVK-native / ToGL / vkd3d(-proton) | various | Linux-only (Nine needs DRI), DXVK-native = Linux, ToGL = D3D9->GL macOS-only (Valve), vkd3d-proton is D3D12->Vulkan (wrong direction) | - | - | - | - | No. (vkd3d-shader library itself is already inside Wine 10) |

## 3. Why #1 wins on "least integration, acceptable licence"
- Zero new translation code: Wine's D3D9 is the most compatible open implementation, and it is the lowest-risk part for "some games broken, fine".
- All modules are source, buildable as PE with mingw from the tree already in /workspace/research/wine (wined3d, d3d9, d3d8, ddraw; libs/vkd3d). LGPL: ship as separate DLLs, publish modifications.
- GLon12 on Xbox UWP is not hypothetical: it runs shipped homebrew. Microsoft's own blog (devblogs.microsoft.com/directx/opengl-on-dx12-enables-steel-assault-to-launch-on-xbox) shows GLon12 on Xbox via GDK.
- Cost: two translations (D3D9->GL->D3D12), mesa-uwp fork is stale (2024-03), and shader compile hitching. Acceptable for the target games.

## 4. Plan (1-2 lanes)
Lane A: **glon12-uwp-dll** (parallel with item 37/42/43 prerequisites, no dependency on PE32 work)
1. Rebase or build aerisarn/mesa-uwp (2c7c63e) `libgallium_wgl`/GLon12 for x64 (MSVC or mingw; see aerisarn/SDL-uwp-gl and glxgears-uwp for build recipe). No ICD loader, no Win32 window hooks: build as non-ICD GL (Jesse Natalie's advice on RetroArch issue 13462: avoid opengl32 ICD loader, strip window hooks).
2. Our host loads it; provide a small "wgl glue" that creates the context offscreen and presents to the CoreWindow swapchain (route from lane swc).
3. Acceptance: x64 test exe `gltri.exe` (GL 3.3 triangle via wglCreateContext path) runs `xbox-test/tools/play-test.sh <gltri-appid>`: log shows GL_RENDERER containing D3D12, 300 presents, hr=0, no RWX/VirtualProtect denials. Record DXIL signing method used.
Lane B: **d3d9-wined3d-pe** (after A step 3)
1. mingw-build wine d3d9.dll, wined3d.dll, vkd3d-shader (PE, GL path only; d3d8/ddraw optional) from /workspace/research/wine (wine-10.0). Route their imports (user32/gdi32/advapi32/opengl32) in import_map: opengl32 -> Lane A glue; gdi32 HDC/pixel format/SwapBuffers -> CoreWindow swapchain; window APIs from items 42/43.
2. Write `d3d9tri.exe` (64-bit, D3D9 FFP + one SM3 shader triangle).
3. Acceptance: `play-test.sh <d3d9tri-appid>`: Direct3DCreate9 -> CreateDevice hr=0 -> 300 Present hr=0, visible colour via screenshot/log. Then `play-test.sh 480`: after PE32 mapper (47) + 32-bit thunk of the D3D9 boundary (49), log reaches Direct3DCreate9 -> CreateDevice OK -> Present. Then LIMBO 48000, Undertale 391540.
Decision gate after Lane B step 3: if frame time on d3d9tri-with-many-draws or shader hitches are unacceptable on the console, start option #3 (wined3d D3D11 backend with ShaderConverter/dx9asm donors) as its own lane.
Cheap parallel check (no lane): confirm on console that D3DCompile works in UWP and that D3D12 accepts DXIL (item 40-type probe), since both choices depend on them.

## 5. Honest uncertainty
- GLon12 DXIL on the console: Mesa d3d12 emits DXIL and normally signs through dxil.dll. mesa-uwp evidence says it works on retail Dev Mode, but I did not read how (validator fallback / experimental shader models). Must be re-checked when rebasing; if signing fails, lane A is dead and #2/#3 take over.
- mesa-uwp is a 2024-03 fork on an old Mesa; rebasing to current Mesa (ec7b2f3, 2026-10-26) may be heavy. aerisarn repos are unmaintained (last push 2024-05) and mesa-uwp has no licence field (code is MIT, Mesa upstream).
- wined3d GL requires GL 3.x+ core/compat features (e.g. dual-source blend, geometry shaders optional); GLon12 coverage on Series S not measured. Perf numbers: none measured.
- Wine's `wined3d` build without the Wine unix side was inferred from Makefile.in and grep, not built. Not verified that it loads against Mesa's WGL.
- Game renderers (LIMBO, Undertale) are from memory, not from import lists. Spacewar D3D9 from Steamworks docs.
- dgVoodoo2 packing is from section analysis (entropy/flags); closed-source behaviour not tested.
- d3dbc->TPF not exposed by vkd3d-shader in Wine 10 (only SPIR-V/GLSL/D3D asm); newer vkd3d may differ, not checked.
- DXVK-on-dzn "cannot run" is from DXVK wiki; I did not run it. Old DXVK (1.10.x) might need fewer features; long shot.
- No dependency verified for 32-bit boundary thunk size (approximate method count).

## 6. Sources (all read-only; clones in /workspace/research/d3d, binaries in /workspace/research/d3d/bin)
- microsoft/D3D9On12 f3352d1 (2026-09-30), MIT; README (DDI layer).
- doitsujin/dxvk cbd2206 (2026-10-24); release v3.1.1 (d3d9.dll imports analysed); wiki "Driver support".
- Mesa ec7b2f3 (2026-10-26) src/microsoft/vulkan/dzn_device.c, src/vulkan/wsi/wsi_common_win32.cpp.
- aerisarn/mesa-uwp 2c7c63e (2024-03-30): `_XBOX_UWP` in d3d12_screen.cpp, d3d12_compiler.cpp, dzn_device.c, libgl-gdi/stw_wgl.c.
- https://github.com/aerisarn/mesa-uwp, SDL-uwp-gl, glxgears-uwp, glew-uwp; https://www.phoronix.com/news/UWP-Mesa-Xbox-Port; https://github.com/libretro/RetroArch/issues/13462 (Jesse Natalie's UWP/Mesa constraints; closed 2024-12 via RetroArch commit 8b1e4a1).
- https://devblogs.microsoft.com/directx/opengl-on-dx12-enables-steel-assault-to-launch-on-xbox/
- dgVoodoo2 2.87.6 (dege-diosg/dgVoodoo2 release; MS/x64/D3D9.dll, MS/x86/D3D9.dll).
- Joshua-Ashton/dxup 7cce9ee (2019-02-04, zlib, archived); GabrielMajeri/d3d9-to-11; elishacloud/dxwrapper dba1ac6; crosire/d3d8to9 (255338f 2026-09-24, BSD-2).
- Wine 10.0 tree /workspace/research/wine: dlls/wined3d (Makefile.in, adapter_gl.c, adapter_vk.c, ffp_hlsl.c), libs/vkd3d/libs/vkd3d-shader/vkd3d_shader_main.c (d3dbc target list).
- ANGLE ab90d2a; xbox-homebrew/angle 239b5a4 via /workspace/ref/RetroArch/pkg/msvc-uwp/RetroArch-msvcUWP/ANGLE/README.md.
- Steamworks SDK docs (SteamworksExample D3D9): https://partner.steamgames.com/doc/sdk/api/example
- D3DCompile in UWP/Xbox: https://walbourn.github.io/hlsl-fxc-and-d3dcompile/, devdocs.xbox.com (packaging-framework-packages).
