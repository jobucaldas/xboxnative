# D3D11 coverage matrix: what the console's D3D11-on-12 layer must answer

Written 2026-11-12 (coverage push, owner order "don't stop until every single
piece works"). Scope: the D3D11 features games actually use, on the console's
native `d3d11.dll` (D3D11-on-12) reached through Play's routes. Evidence
labels: **[covered]** a fixture pass proves it off-device (Wine) and the same
fixture is packaged for the console self-test; **[off-device only]** proven
under Wine, console run pending; **[gap]** nothing proves it; **[console
unverified]** packaged but never run on the console.

The proving fixture is `src/xbox-test/app/tests/play-d3d11.cpp` (self-test
mode `d3d11`, packaged by `build-diag-fixtures.sh`), plus the diagnostics
contract `d3d11-diag.cpp`/`play-runtime.sh` mode `d3d11-diag`. `d3d11_diag.h`
itself (hook audit, kill switch) is another work stream and out of scope here.

## Matrix

| # | Feature games use | Proved by | Status |
|---|---|---|---|
| 1 | Device creation, feature levels 11_1/11_0 requested and answered | `play-d3d11.cpp` triangle pass; `d3d11-diag` log lines (`D3D11CreateDevice request/feature level got`) | covered; console unverified |
| 2 | Swap chain on a guest HWND (CoreWindow adapter), FLIP_SEQUENTIAL | `play-d3d11.cpp` (`CreateSwapChainForHwnd`, one `Present`) | covered; console unverified |
| 3 | VS/PS pipeline from committed DXBC, input layout, viewport, draw | `play-d3d11.cpp` triangle + staging readback (exit 42/43) | covered; console unverified |
| 4 | Staging-texture pixel readback (Map READ) | `play-d3d11.cpp` | covered; console unverified |
| 5 | Geometry shader (point expansion, `gs_5_0`, TriangleStream) | `play-d3d11.cpp` GS pass (exit 44-47) | covered; console unverified |
| 6 | Rejected Create*/Check*/Map calls logged once each | `d3d11-diag` mode assertions in `play-runtime.sh` | covered |
| 7 | MSAA (4x render target, ResolveSubresource, quality levels) | `play-d3d11.cpp` MSAA pass (exit 50-54) | off-device only |
| 8 | GenerateMips (mip chain texture, SRV, linear sampler) | `play-d3d11.cpp` mips pass (exit 55-58) | off-device only |
| 9 | Deferred contexts + FinishCommandList + ExecuteCommandList | `play-d3d11.cpp` deferred pass (exit 60-63) | off-device only |
| 10 | Stream-out (SO buffers, `CreateGeometryShaderWithStreamOutput`, capture readback) | `play-d3d11.cpp` stream-out pass (exit 64-67) | off-device only |
| 11 | Indirect draws (DrawInstancedIndirect from a MISC_DRAWINDIRECT_ARGS buffer) | `play-d3d11.cpp` indirect pass (exit 68-71) | off-device only |
| 12 | Compute shaders + UAVs + structured buffers | — | gap (needs new committed cs_5_0 DXBC) |
| 13 | Tessellation (HS/DS) | — | gap (needs new committed DXBC) |
| 14 | Texture arrays (RTV per slice, CopySubresourceRegion slice→slice) | `play-d3d11.cpp` array pass (exit 87-90) | off-device only |
| 15 | BC1-7 compressed formats (create + copy round-trip; sampling needs a sampler PS) | `play-d3d11.cpp` BC1 pass (exit 91-94) | off-device only |
| 16 | Queries (occlusion query, non-blocking GetData poll with bounded spin) | `play-d3d11.cpp` query pass (exit 72-75); predication still a gap | off-device only |
| 17 | ResizeBuffers (back-buffer refs released first, re-fetched, redraw, present) | `play-d3d11.cpp` resize pass (exit 76-79) | off-device only |
| 18 | Indexed draws (IB + DrawIndexed, rotated index order) | `play-d3d11.cpp` indexed pass (exit 80-82) | off-device only |
| 19 | Depth-stencil (DSV, LESS_EQUAL test, dynamic VB upload per draw) | `play-d3d11.cpp` depth pass (exit 83-86) | off-device only |
| 20 | Blend/dither state variety, dual-source blend | — | gap (diagnostics count only) |
| 21 | Capability probes a game branches on (CheckFormatSupport, CheckMultisampleQualityLevels, CheckFeatureSupport) logged | `d3d11-diag` mode | covered |
| 22 | Device-removed reason after a failed Present | `d3d11_diag.h` overlay (another stream) | covered there |

## Notes

- Everything in this repository's fixtures draws with **committed DXBC**
  (`shaders/triangle_bytecode.h`); no shader compiler runs at build or package
  time, and no D3DCompile dependency is added. New shader stages need a new
  committed `.dxbc` + embedded array, generated off-device like
  `shaders/README.md` says.
- Exit codes name the step (the fixture's own convention): 42 = all passes
  landed, 43/47 = BLOCKED pixel mismatch (never a pass), anything else names
  the first failed step. New passes continue 50+ so an old console log's codes
  stay meaningful.
- The console's D3D11 is D3D11-on-12; Wine's is GL-backed (or d3d12 via
  vkd3d). A pass under Wine proves the API contract and Play's routing, not
  the console's answers — every row stays "console unverified" until the
  self-test runs there and the log is read.
- Console check to run later: package, install as an upgrade, request
  `selftest.txt` mode `d3d11`, and read `SELFTEST RESULT d3d11 PASS` plus the
  `PRUN D3D11` diagnostics lines (MSAA quality levels, mips, command list,
  stream-out, indirect, query counts in the draw summary).
