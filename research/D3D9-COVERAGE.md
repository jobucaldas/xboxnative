# D3D9 coverage matrix (Play's D3D9-on-D3D11 shim)

Status labels, matching research/D3D9-PLAN.md: **[proven]** a test in this repo
fails without the behaviour and passes with it; **[unverified]** implemented and
built but never run on the console; **[gap]** not implemented, honestly refused
or absent. The off-device proof is `src/xbox-test/uwph/d3d9/test.sh` (Wine,
"shim-contract"): it drives the real shim DLL against Wine's d3d11.dll with the
real translator. The on-console proof is the packaged fixtures
(`selftest_plan.h` modes `d3d9`, `d3d9-triangle`, `d3d9-triangle-variants`,
`d3d9-textured`), which run the same shim inside Play. Console status is
recorded per row; every console claim needs the self-test run.

Shim source: src/xbox-test/uwph/d3d9/d3d9.cpp (device), ffp.h (fixed-function
shader synthesis), src/tools/d3d9-translator (DXUP dx9asm, D3D9 SM1-3 -> DXBC).

## 1. Factory and entry points

| Feature | Proven by | Status |
|---|---|---|
| `Direct3DCreate9` returns `IDirect3D9`, wrong SDK version refused | shim-contract checks (test.cpp:84) | proven, console unverified |
| `Direct3DCreate9Ex` returns `IDirect3D9Ex`, wrong SDK -> `D3DERR_INVALIDCALL` | shim-contract (test.cpp:86-92) | proven, console unverified |
| `IDirect3D9` and `IDirect3D9Ex` are the same object (QI) | shim-contract (test.cpp:95-98) | proven, console unverified |
| One adapter; `GetAdapterIdentifier` (VendorId 0x1414), display mode 1920x1080@60 | shim-contract (test.cpp:107-119) | proven, console unverified |
| `GetAdapterModeCount`/`EnumAdapterModes` one mode per valid format, 0 for unknown | shim-contract (test.cpp:116-119) | proven, console unverified |
| `GetAdapterLUID` returns the D3D11 adapter's LUID | shim-contract (test.cpp:120-121) | proven, console unverified |
| `EnumAdapterModesEx`/`GetAdapterDisplayModeEx`/`GetAdapterModeCountEx` (Ex variants) | build only; the Ex path is exercised by CreateDeviceEx in every fixture | unverified |
| `CheckDeviceType`/`CheckDeviceFormat`/`CheckDeviceMultiSampleType`/`CheckDepthStencilMatch`/`CheckDeviceFormatConversion` refuse formats with no DXGI equivalent | shim-contract (test.cpp:129-142) | proven, console unverified |
| `CreateDevice` refuses REF/NULLREF, wrong adapter, null parameters | shim-contract (test.cpp:162-163) | proven, console unverified |
| `CreateDeviceEx` creates the device on a guest HWND; Play maps d3d9.dll from runtime | play-runtime.sh d3d9-diag log greps (mapping d3d9.dll, guest D3D11CreateDevice) | proven off-device, console unverified |

## 2. Device capabilities

| Feature | Proven by | Status |
|---|---|---|
| `GetDeviceCaps` on factory and device: VS/PS 3.0, 256 v-consts, 224 p-consts, 8 blend stages, 16 streams, 4 RTs, 6 clip planes, 16384 textures | shim-contract (test.cpp:124-127, 174-175) | proven, console unverified |
| `GetCreationParameters`, `GetDirect3D`, `TestCooperativeLevel`, `GetNumberOfSwapChains` | shim-contract (test.cpp:171-181) | proven, console unverified |
| `GetAvailableTextureMem` (fixed 512 MB), `EvictManagedResources`, `ValidateDevice` | build only | unverified |

## 3. Swap chain and present

| Feature | Proven by | Status |
|---|---|---|
| Swap chain on a guest HWND through the CoreWindow adapter | play-runtime.sh d3d9-diag + d3d9 fixture exit 42 | proven off-device, console unverified |
| `GetSwapChain`/`GetBackBuffer`/back buffer == render target 0 | shim-contract (test.cpp:185-218), d3d9 fixture step 17 | proven, console unverified |
| `GetPresentParameters`, `GetDisplayMode`, `GetRasterStatus` | shim-contract (test.cpp:189-191) | proven, console unverified |
| `Present`/`PresentEx` three frames; DEVICELOST mapped from DXGI | play-d3d9 fixture steps 22; shim-contract Present check | proven off-device, console unverified |
| `Reset` rebuilds the swap chain (windowed; exclusive fullscreen refused) | shim-contract (test.cpp:622-626) | proven, console unverified |
| `ResetEx` same path | build only (ResetEx -> rebuildSwapChain) | unverified |
| `EnableAutoDepthStencil` + `AutoDepthStencilFormat` creates and binds the depth surface | play-d3d9 fixture step 18, shim-contract depth desc check | proven, console unverified |
| Additional swap chains (`CreateAdditionalSwapChain`) | refused with `D3DERR_INVALIDCALL` + log line | gap (reported honestly) |
| `GetFrontBufferData`, gamma ramps beyond identity | refused/identity | gap (reported honestly) |

## 4. Resources: buffers, textures, surfaces

| Feature | Proven by | Status |
|---|---|---|
| `CreateVertexBuffer` (all pools), Lock/Unlock with DISCARD and NO_OVERWRITE semantics, offset addressing | shim-contract triangle draw uses MANAGED VB; test.cpp:556-579 | proven, console unverified |
| `CreateIndexBuffer` INDEX16/INDEX32, others refused | shim-contract indexed UP draw | proven, console unverified |
| `CreateTexture` MANAGED/DEFAULT/SYSTEMMEM, `Levels=0` full mip chain (8x8 -> 4 levels) | shim-contract (test.cpp:439-473, 543-551) | proven, console unverified |
| LockRect on MANAGED texture with mip chain through staging shadow; write reaches the texture | shim-contract (test.cpp:444-466) | proven, console unverified |
| `GetSurfaceLevel` of a RENDERTARGET texture carries an RTV (SetRenderTarget accepts it) | shim-contract (test.cpp:459-461 GetRenderTargetData path) | proven, console unverified |
| Default-pool lock refused exactly as on Windows | shim-contract (test.cpp:211-212) | proven, console unverified |
| `CreateRenderTarget`/`CreateDepthStencilSurface`/`CreateOffscreenPlainSurface` (+Ex variants forward) | shim-contract target/staging draws | proven, console unverified |
| `GetRenderTargetData` render target -> SYSTEMMEM readback, per-subresource size check | every fixture's readback; shim-contract (test.cpp:376) | proven, console unverified |
| `CreateVolumeTexture`, `CreateCubeTexture` | refused with log | gap (reported honestly) |
| `UpdateSurface`, `UpdateTexture`, `StretchRect`, `ColorFill` | refused | gap (reported honestly) |
| DXT1/2/3/4/5 -> BC1/BC2/BC3 mapping in dxgiFormat | mapped in the table (d3d9.cpp:65-69); no fixture samples a DXT texture | gap: no pixel-readback proof |
| D3DFMT_A8/L8/A8L8/L16/R5G6B5/A4R4G4B4 etc. mapping | mapped; not exercised by any fixture draw | gap: no pixel-readback proof |
| `SetPrivateData`/`GetPrivateData`/`FreePrivateData` on resources | forwarded to D3D11 inner | unverified |

## 5. Vertex declarations and FVF

| Feature | Proven by | Status |
|---|---|---|
| `CreateVertexDeclaration` + `SetVertexDeclaration`, GetDeclaration counts terminator, small-buffer refused | shim-contract (test.cpp:510-533) | proven, console unverified |
| `SetFVF` replaces the declaration; FVF decode (XYZRHW, DIFFUSE, SPECULAR, TEXCOUNT, TEXCOORDSIZE1 mask semantics) | shim-contract FVF draws; test.cpp:538-539 | proven, console unverified |
| Layout built from the translated shader's ISGN, not the declaration (extra elements dropped) | textured fixture's vs_1_1 draw (exit 42) | proven off-device, console unverified |
| POSITIONT input carries SV_Position in both signatures (the dxilconv crash fix) | src/tools/d3d9-translator/test-positiont.sh in CI | proven (CI), console unverified |
| FFP pixel shader declares no sampler for a textureless stage (the second dxilconv crash fix) | src/tools/d3d9-translator/test-ffp-no-texture.sh in CI | proven (CI), console unverified |

## 6. Shaders and constants

| Feature | Proven by | Status |
|---|---|---|
| `CreateVertexShader`/`CreatePixelShader` SM1-3 through d3d9translator.dll; END-token length scan; truncated blob refused | shim-contract (test.cpp:477-505), textured fixture | proven off-device, console unverified |
| `GetFunction` size-then-copy contract, short buffer refused without write | shim-contract (test.cpp:489-498) | proven, console unverified |
| `Set/GetVertexShaderConstantF`, `Set/GetPixelShaderConstantF` (256/224 registers) | shim-contract (test.cpp:268-277) | proven, console unverified |
| Constants reach the translated shader (textured draw's c0..c3 transform) | play-d3d9-textured exit 42 | proven off-device, console unverified |
| ps_3_0 game shaders (the caps advertise 3.0) | translator handles SM1-3 [read]; no fixture compiles ps_3_0 | gap: no fixture draws a ps_3_0 |
| vs_2_0/ps_2_0 game shaders | translator handles SM1-3 [read]; no fixture draws SM2 | gap: no fixture draws SM2 |
| `Set/GetVertexShaderConstantI/B`, pixel I/B | I/B setters return D3D_OK (stored nowhere), getters INVALIDCALL | gap (reported honestly) |
| `ProcessVertices` | refused | gap (reported honestly) |

## 7. Fixed-function pipeline (ffp.h synthesis)

| Feature | Proven by | Status |
|---|---|---|
| FFP vertex shader: WVP transform (4x dp4), POSITIONT viewport fixup, colour/texcoord passthrough | shim-contract FVF triangle draws; play-d3d9-triangle exit 42 | proven, console unverified |
| FFP pixel shader: stage ops DISABLE/SELECTARG1/2/MODULATE/MODULATE2x/4x/ADD/SUBTRACT; args DIFFUSE/CURRENT/TEXTURE/TFACTOR with COMPLEMENT and ALPHAREPLICATE | ffp.h emits them; only SELECTARG1/DIFFUSE and MODULATE-with-texture proven by fixtures | gap: no readback fixture for MODULATE2x/4x/ADD/SUBTRACT, TFACTOR, COMPLEMENT |
| Unbound stage samples white (1x1 fallback), not black | play-d3d9-triangle-variants v2 (outside check only) | partially proven, console unverified |
| Two-stage chain (stage 0 textureless, stage 1 textured) | variants v3 (outside check only) | partially proven, console unverified |
| Fixed-function lighting (`SetLight`, `LightEnable`, materials beyond diffuse) | reported once, drawn unlit | gap (scoped out in the decision record) |
| Fog (`D3DRS_FOGENABLE`, FOG vertex usage) | not implemented, no report | gap |
| Alpha test (`D3DRS_ALPHATESTENABLE`/`ALPHAFUNC`/`ALPHAREF`) | stored, read back; never applied to the pipeline | gap |
| Point sprites (`D3DRS_POINTSPRITEENABLE`, PSIZE) | stored; PSIZE declaration decodes but no point-size path | gap |
| Clipping planes (`SetClipPlane`, `D3DRS_CLIPPLANEENABLE`) | accepted and ignored | gap (reported honestly) |
| Texture-coordinate generation (`D3DTS_TEXTUREn`, D3DTSS_TEXCOORDINDEX) | transforms accepted and ignored with a log line | gap (reported honestly) |

## 8. Render states actually consumed

| Feature | Proven by | Status |
|---|---|---|
| Blend: ALPHABLENDENABLE, SRC/DESTBLEND, BLENDOP, separate alpha blend, COLORWRITEENABLE -> D3D11 blend state | signature rebuild in syncPipeline; blending never proven by a readback | gap: no readback fixture with blending on |
| Depth: ZENABLE, ZWRITEENABLE, ZFUNC -> D3D11 depth state | depth state built; z-buffer behaviour never proven by readback (fixtures disable Z) | gap: no depth-test readback fixture |
| Raster: CULLMODE (CCW->BACK mapping), FILLMODE, SCISSORTESTENABLE | cull mapping proven by fixtures drawing CULL_NONE; CCW culling itself unproven | gap: no cull/wireframe readback |
| Stencil (STENCILENABLE, ops, funcs, masks) | depth-stencil state always builds StencilEnable=FALSE; Clear honours D3DCLEAR_STENCIL | gap: no stencil readback |
| Alpha test, fog, point sprites, texture factor in shaders | see section 7 | gap |
| All 256 render states stored and read back | shim-contract (test.cpp:257-260) | proven, console unverified |
| State blocks (`CreateStateBlock`/`BeginStateBlock`/`EndStateBlock`) | refused with log | gap (reported honestly) |

## 9. Samplers

| Feature | Proven by | Status |
|---|---|---|
| MIN/MAG/MIPFILTER combinations -> D3D11 filter enum | filterOf covers all; only POINT proven by fixtures | gap: no linear-filtering readback |
| ADDRESS WRAP/MIRROR/CLAMP/BORDER/MIRRORONCE | addressOf covers all; only default WRAP used | gap: no address-mode readback |
| 16 stages of state storage and readback | shim-contract (test.cpp:261-263) | proven, console unverified |
| Mip LOD bias, MaxMipLevel | stored, not applied | gap |

## 10. Streams, indices, draws

| Feature | Proven by | Status |
|---|---|---|
| `DrawPrimitive` TRIANGLELIST, TRIANGLESTRIP, POINTLIST, LINELIST, LINESTRIP | topologyOf maps all; only TRIANGLELIST proven by readback | gap: no readback for other topologies |
| `DrawIndexedPrimitive` with BaseVertexIndex folding | indexed UP fixture; SetIndices path proven | proven off-device, console unverified |
| `DrawPrimitiveUP`/`DrawIndexedPrimitiveUP` with MinVertexIndex>0 array semantics | shim-contract (test.cpp:573-619), variants | proven, console unverified |
| Triangle fans | reported, not drawn (D3D11 has no fan topology) | gap (reported honestly) |
| `SetStreamSourceFreq` instancing | accepted and ignored | gap (reported honestly) |
| Multiple streams (SetStreamSource slots 1+) | storage and issue() bind up to 16; no fixture uses 2 streams | gap: no multi-stream readback |
| `DrawRectPatch`/`DrawTriPatch` | refused | gap (reported honestly) |

## 11. Queries and diagnostics

| Feature | Proven by | Status |
|---|---|---|
| `CreateQuery` (occlusion, event, etc.) | refused with log | gap (reported honestly) |
| Shader dump diagnostics (`D3D9_SHIM_DUMP_SHADERS`) | build only | unverified |
| `SetCursorProperties`/`GetClipStatus`/palettes | honest refusal/identity | gap (reported honestly) |

## 12. Console self-test coverage (selftest_plan.h modes)

| Mode | Fixture | What it proves on console | Console status |
|---|---|---|---|
| `d3d9` | play-d3d9.exe | shim served, device + swap chain + auto depth, clear, 3 presents | unverified (needs selftest run) |
| `d3d9-triangle` | play-d3d9-triangle.exe | FFP draw + pixel readback (red inside, black outside) + present | unverified |
| `d3d9-triangle-variants` | play-d3d9-triangle-variants.exe | v1 textureless, v2 unbound-texture arg, v3 two-stage, v4 game ps_1_1 textured, v5 repeat | unverified |
| `d3d9-textured` | play-d3d9-textured.exe | game vs_1_1/ps_1_1 + translator + texture + declaration + constants | unverified |

## 13. Priority order for closing gaps off-device

1. **FFP texture-op readback fixture** (Wine host test, then console fixture):
   MODULATE with a real texture, TFACTOR, COMPLEMENT (invert), ADD, SUBTRACT,
   MODULATE2x/4x — the ops a GameMaker/2D game actually sets.
2. **Blending readback**: ALPHABLENDENABLE with SRCALPHA/INVSRCALPHA over a
   known background — the single most common 2D-game state.
3. **Depth-test readback**: z-enabled draw with a quad behind/in front.
4. **Alpha-test readback** (needs shim work: ALPHATESTENABLE is currently
   stored-only — implement as discard in the generated pixel shader).
5. **SM2/SM3 shader fixture** (needs translator verification for ps_2_0
   c-register semantics and ps_3_0 s# samplers).
6. **Linear filtering + address modes** readback.
7. Triangle strip / indexed non-UP draw readback.
