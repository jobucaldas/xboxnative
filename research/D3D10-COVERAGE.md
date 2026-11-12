# D3D10 coverage matrix (Play's D3D10 shim over the console's D3D11)

Written 2026-11-12 (BRT). Coverage push slice: Direct3D 10/10.1. The shim is
`src/xbox-test/uwph/d3d10/d3d10.cpp` (one PE answering d3d10.dll, d3d10_1.dll
and d3d10core.dll); its contract test is `src/xbox-test/uwph/d3d10/test.sh`
(Wine, local) and the console self-test's mode is `d3d10`
(`src/xbox-test/app/tests/play-d3d10.cpp`). Everything below routes onto the
console's native d3d11.dll through Play's device hook and CoreWindow factory.

Legend: **proven** = a test fails without the behaviour; **off-device only** =
proven under Wine against Wine's d3d11, console unverified; **gap** = nothing
proves it; **gap (platform)** = the platform cannot provide it, reported
honestly instead of faked.

## 1. Device and DXGI

| Feature / API | Proven by | Status |
|---|---|---|
| `D3D10CreateDevice` SDK-version check (sdk != D3D10_SDK_VERSION -> E_INVALIDARG) | uwph/d3d10/test.cpp; play-d3d10.cpp step 11 | proven |
| Device at feature level 10_0 (the level a D3D10 game asks for) | uwph/d3d10/test.cpp (`GetFeatureLevel()==10_0`) | proven |
| `D3D10CreateDevice1` at 10_1, `D3D10_1_SDK_VERSION` | uwph/d3d10/test.cpp | off-device only |
| `D3D10CreateDeviceAndSwapChain(1)` on a guest HWND | uwph/d3d10/test.cpp; play-d3d10.cpp | proven (present path) |
| Legacy `DXGI_SWAP_CHAIN_DESC` -> CoreWindow adapter conversion | play-runtime.sh d3d10-diag greps (swapchain hr=0) | proven |
| `IDXGISwapChain::GetBuffer` as ID3D10Texture2D + Present | uwph/d3d10/test.cpp; play-d3d10.cpp | proven |
| `ID3D10Device1` reachable by QI from `ID3D10Device` | uwph/d3d10/test.cpp; play-d3d10.cpp step 14 | proven |
| Unknown IIDs forwarded to the D3D11 device (ID3D11Device, IDXGIDevice) | uwph/d3d10/test.cpp | off-device only |
| `ID3D10Multithread` (separate object, Enter/Leave no-ops) | uwph/d3d10/test.cpp | off-device only |
| DRIVER_TYPE REFERENCE/SOFTWARE -> WARP fallback with a log note | shim code path only | gap (no test drives it) |
| `D3D10CoreCreateDevice` | shim export only | gap |
| Exclusive fullscreen refused; multisampled swapchain refused | d3dshim::createSwapChain noteOnce | gap (no test drives it) |

## 2. Resources and views

| Feature / API | Proven by | Status |
|---|---|---|
| `CreateBuffer` + `Map`(WRITE_DISCARD)/`Unmap` + `GetDesc` | uwph/d3d10/test.cpp | proven |
| Buffer as ID3D10Resource (GetType), `GetDevice` identity | uwph/d3d10/test.cpp | proven |
| `CreateTexture2D` + `GetDesc` | uwph/d3d10/test.cpp | proven |
| `CreateTexture1D` / `CreateTexture3D` + Map | shim code only | gap |
| Texture Map (readback through a staging copy) | **new**: d3d10-triangle fixture | gap -> new fixture |
| `CreateRenderTargetView` / `CreateDepthStencilView` + Clear | uwph/d3d10/test.cpp; play-d3d10.cpp | proven |
| View `GetResource` returns the same wrapper the guest holds | uwph/d3d10/test.cpp | proven |
| Cubemap texture (ArraySize=6, MISC_TEXTURECUBE) + TEXTURECUBE SRV | **new**: d3d10-triangle fixture | gap -> new fixture |
| `GenerateMips` + readback of the generated mip | **new**: d3d10-triangle fixture | gap -> new fixture |
| `ResolveSubresource` (4x MSAA -> 1x) + readback | **new**: d3d10-triangle fixture | gap -> new fixture |
| `CopySubresourceRegion` / `CopyResource` / `UpdateSubresource` | staging readback in the new fixture | gap -> new fixture |
| `OpenSharedResource` | shim: E_NOTIMPL + noteOnce | gap (platform) |

## 3. Pipeline, draw and readback

| Feature / API | Proven by | Status |
|---|---|---|
| `CreateInputLayout` (D3D10 element desc -> D3D11) | **new**: d3d10-triangle fixture | gap -> new fixture |
| `CreateVertexShader` / `CreatePixelShader` (vs_4_0/ps_4_0 DXBC) | **new**: d3d10-triangle fixture | gap -> new fixture |
| `IASetVertexBuffers`/`IASetIndexBuffer` + readback identity | uwph/d3d10/test.cpp (vertex) | partial |
| `Draw` that lands where it should (pixel readback) | **new**: d3d10-triangle fixture | gap -> new fixture |
| `DrawIndexed` | **new**: d3d10-triangle fixture | gap -> new fixture |
| `DrawInstanced` / `DrawIndexedInstanced` | shim forwards | gap (rarely first-frame; same path as Draw) |
| `CreateGeometryShader` + `GSSetShader` + point expansion | **new**: d3d10-triangle fixture | gap -> new fixture |
| `CreateGeometryShaderWithStreamOutput` + `SOSetTargets` + SO readback | **new**: d3d10-triangle fixture | gap -> new fixture |
| `DrawAuto` (the SO consumer) | shim forwards | gap (needs a working SO first) |
| State objects: blend/DS/rasterizer/sampler Create+GetDesc+bind | uwph/d3d10/test.cpp | proven |
| `OMSetBlendState` etc. readback returns the same wrapper | uwph/d3d10/test.cpp (vertex buffers only) | partial |
| Viewports/scissor rects (int corners -> float) | uwph/d3d10/test.cpp | proven |
| State blocks: mask arithmetic, Capture/Apply | uwph/d3d10/test.cpp | partial (single field) |

## 4. Queries, reflection and tools

| Feature / API | Proven by | Status |
|---|---|---|
| `CreateQuery` (occlusion) Begin/End/GetData | uwph/d3d10/test.cpp | off-device only |
| `D3D10_QUERY_SO_STATISTICS` Begin/End/poll-GetData (S_FALSE until answered) | **new**: d3d10-triangle fixture pass 4 | gap -> new fixture |
| `CreatePredicate` + `SetPredication` | shim code only | gap |
| Timestamp/disjoint query pair | shim code only | gap |
| `D3D10ReflectShader` (D3D11 reflector narrowed to the D3D10 method set) | **new**: uwph/d3d10/test.cpp reflection checks | gap -> new test |
| `D3D10CompileShader` (routes to d3dcompiler_47) | uwph/d3d10/test.cpp | off-device only |
| `D3D10CreateBlob`, signature blobs, shader profiles ("vs_4_0") | uwph/d3d10/test.cpp | off-device only |
| `D3D10DisassembleShader` | shim code only | gap |
| `D3D10PreprocessShader`, `D3D10GetVersion`, `D3D10RegisterLayers` | export stubs, as Wine's | gap (deliberate) |
| `CheckFormatSupport` / `CheckMultisampleQualityLevels` | uwph/d3d10/test.cpp (format) | off-device only |
| `SetExceptionMode` -> E_NOTIMPL (debug-layer feature) | uwph/d3d10/test.cpp | off-device only |

## 5. FX10 effects

| Feature / API | Proven by | Status |
|---|---|---|
| `D3D10CreateEffectFromMemory` / `...EffectPoolFromMemory` / `...CompileEffectFromMemory` | shim: E_NOTIMPL + noteOnce | gap (platform, deliberate): Wine's FX10 is a 345 KB parser welded to its own device; nothing here could run it. A game that requires FX10 fails at load with a log line naming it. |

## 6. Console self-test modes (selftest_plan.h)

| Mode | Fixture | Covers | Console status |
|---|---|---|---|
| `d3d10` | play-d3d10.exe | device, swapchain, RTV, clear, 3 presents | unverified this slice; last known PASS on console |
| `d3d10-triangle` **(new)** | play-d3d10-triangle.exe | the draw path: input layout, vs_4_0/ps_4_0, Draw/DrawIndexed readback, GS point expansion, stream-out, cubemap+GenerateMips, MSAA resolve | **unverified** — needs a console run |
| `d3d10-diag` | play-d3d10.exe (off-device runner name) | shim mapping + CoreWindow swapchain greps | off-device |

Exit codes for `d3d10-triangle`: 42 = every readback matched; 43 = the pipeline
ran but the pixels did not read back as drawn (BLOCKED, never a pass); other
codes name the step that failed, as the other fixtures do.

## 7. Honest gaps that remain after this slice

- FX10 effects (above) — the largest real gap for D3D10-era games; needs an FX
  runtime or a donor, out of scope here.
- Performance counters, `OpenSharedResource`, `D3D10PreprocessShader`,
  `D3D10GetVersion`, `D3D10RegisterLayers`, `D3D10DisassembleEffect` — E_NOTIMPL
  stubs, matching Wine; a game hitting them logs the refusal once.
- `DrawInstanced`/`DrawIndexedInstanced`/`DrawAuto`, predication, timestamp
  queries — forwarded but unproven by any fixture (same forwarding path as the
  proven calls; the risk is descriptor conversion, which the proven calls share).
- Reflection on the console: d3dcompiler_47 is documented as available to UWP on
  Xbox but is unverified on this device; the reflection proof is the Wine test.
- DRIVER_TYPE fallbacks, exclusive-fullscreen/multisampled-swapchain refusals —
  code paths with no driving test.
