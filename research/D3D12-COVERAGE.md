# D3D12 coverage matrix (Play's D3D12 surface)

Status labels: **[fixture]** the console self-test fixture `play-d3d12.cpp` exercises it (exit 42 step or
readback); **[diag]** `host/d3d12_diag.h` hooks it and `d3d12-diag.cpp` pins the contract off-device;
**[gap]** nothing proves it yet; **[console: unverified]** nothing has run on the Xbox for it.
Exit-code conventions: 42 = PASS, 43 = BLOCKED (platform said no, never a pass), any other code names the
failing step. The fixture list and verdict mapping live in `selftest_plan.h` (mode `d3d12`).

The fixture (src/xbox-test/app/tests/play-d3d12.cpp) covers today: debug-interface probe, device create,
SHADER_MODEL query, direct queue, factory, 640x480 flip-discard chain on a guest HWND, RTV heap + views,
allocator, list, fence + event, one triangle through a minimal PSO (committed SM 5.0 DXBC, upload-heap VB,
input layout) with pixel readback through a READBACK heap, three presented frames, two deliberately
rejected `CreateCommittedResource` calls (dedupe check), debug-layer error drain.

## Matrix

| Feature a D3D12 game uses | What proves it today | Status |
|---|---|---|
| `D3D12CreateDevice` (null adapter, FL 11_0) | fixture step 11 + diag route log | [fixture] [console: unverified] |
| `D3D12GetDebugInterface` + EnableDebugLayer | fixture step (optional path) | [fixture] [console: unverified] |
| `CheckFeatureSupport(SHADER_MODEL)` (ask 6.6) | fixture step 12 + diag `feature=7(SHADER_MODEL)` | [fixture] [console: unverified] |
| `CheckFeatureSupport(FEATURE_LEVELS/ARCHITECTURE/OPTIONS*)` | diag capability hooks (d3d12-diag.cpp) | [diag] [console: unverified] |
| Command queue (DIRECT) create + ExecuteCommandLists | fixture steps 13/… | [fixture] [console: unverified] |
| Flip-model swap chain on a guest HWND (queue as device) | fixture step 15 + 3 presents | [fixture] [console: unverified] |
| `ResizeBuffers` after present | nothing | **[gap]** |
| RTV descriptor heap + per-backbuffer views | fixture steps 16-17 | [fixture] [console: unverified] |
| Command allocator/list, Reset, Close | fixture steps 18-20/23-25 | [fixture] [console: unverified] |
| Fence Signal/SetEventOnCompletion + CreateEventEx wait | fixture steps 21-22/27-29 | [fixture] [console: unverified] |
| Graphics PSO (SM 5.0 DXBC VS/PS, input layout, no blend/depth) | fixture step 55 + readback | [fixture] [console: unverified] |
| Root signature 1.0 (empty, serialized + created) | fixture steps 45-46 | [fixture] [console: unverified] |
| Root signature 1.1 (`D3D12SerializeVersionedRootSignature`) | nothing | **[gap]** |
| Root constants / CBV / descriptor tables (non-empty root) | nothing | **[gap]** |
| Compute PSO (`CreateComputePipelineState`) + `Dispatch` | nothing (diag hook only) | **[gap]** |
| UAV: `CreateUnorderedAccessView`, UAV barrier, structured-buffer readback | nothing (diag hook only) | **[gap]** |
| Bundles (`CreateCommandList(BUNDLE)`, `ExecuteBundle`) | nothing (diag hook only) | **[gap]** |
| Resource barriers (TRANSITION, ALL_SUBRESOURCES) | fixture draw + frame loops | [fixture] [console: unverified] |
| UAV + SPLIT barriers | nothing | **[gap]** |
| `ExecuteIndirect` (CommandSignature + arg buffer) | nothing (diag hook only) | **[gap]** |
| DXIL SM 6.x PSO (console DXIL acceptance) | nothing; D3D12-PLAN milestone 2 | **[gap]** (console-critical) |
| Mesh/amplification shaders (`OPTIONS7`? no — `MeshShaderTier` in OPTIONS… via `CheckFeatureSupport`) | nothing | **[gap]** (query only; console is unlikely to expose tier 1) |
| Upload/READBACK/DEFAULT heap committed resources + `GetCopyableFootprints` | fixture steps 40-41/54 | [fixture] [console: unverified] |
| `Map`/`Unmap` readback + row-pitch handling | fixture step 53 | [fixture] [console: unverified] |
| Rejected calls logged once per distinct call | fixture steps 30-31 + diag dedupe | [fixture] [diag] |
| Debug-layer error drain (InfoQueue) | fixture step 56 | [fixture] [console: unverified] |
| `D3D12EnableExperimentalFeatures` | route logs the answer once (D3D12-PLAN) | [diag-route] [console: unverified] |
| Device-removed reason, adapter LUID reporting | diag adoption report | [diag] |
| Factory levels 3-7 forwarding, adapter wrapping | graphics-factory.cpp (CoreWindow factory) | [fixture] (graphics-factory.sh) |
| Swap-chain resize (flip model, after present) | nothing for D3D12 (D3D11 has resize_drain.h) | **[gap]** |
| Query heaps / occlusion, `ResolveQueryData` | nothing (diag hook only) | **[gap]** (low priority: few games start with it) |
| Video decode (d3d12video) | nothing | **[gap]** (out of scope for Play's game path) |

## How to read the gaps

- **Compute PSO + UAV readback** is the highest-value gap: Unity's D3D12 backend and most engines run a
  compute pass before the first frame (skin caching, texture setup), and it exercises `CreateComputePipelineState`,
  `SetComputeRoot*`, `Dispatch`, UAV barriers and structured-buffer readback — none of which the graphics
  triangle touches. It reuses the same readback convention (exit 42 only when the GPU wrote the expected
  values), so a silent no-op dispatch fails the fixture.
- **Root signature 1.1** is the second: `D3D12SerializeVersionedRootSignature` is the entry point every
  newer engine calls; the versioned serialize path is a separate export from the 1.0 one and Play's route
  table logs only the 1.0 pair today.
- **ExecuteIndirect** and **bundles** are mid-value: common in later frames, rarely on the startup path.
- **DXIL SM 6.x** is the console-critical unknown (D3D12-PLAN milestone 2) but needs a real DXIL blob; the
  repo carries none and DXC is not a pinned dependency, so it stays a listed gap here and is flagged for a
  follow-up with a pinned DXC or a checked-in minimal blob decision.

## Conventions (unchanged)

- 42 = PASS, 43 = BLOCKED, any other code = the failing step. `selftest_plan.h` maps `d3d12` 43 to
  `BLOCKED pixel-readback`.
- The fixture is built by `build-diag-fixtures.sh` (package time) and `play-runtime.sh` (off-device proof);
  both link the same sources, so a fixture that compiles here is what the console runs.
- Console status for everything is **unverified** as of this matrix: the console self-test has never run
  mode `d3d12` to completion. The check to run: `LocalState\selftest.txt` containing `d3d12`, then read
  `SELFTEST RESULT d3d12 …` plus the `PRUN D3D12 …` lines in the runner log.
