# Vulkan coverage: what the fixtures prove

SPDX-License-Identifier: GPL-3.0-or-later

Companion to [VULKAN-PLAN.md](VULKAN-PLAN.md) (why dzn exists, how the loader is
packaged) and [D3D-LAYERS-REPORT.md](D3D-LAYERS-REPORT.md) (why DXVK cannot host
on dzn). This is the feature/API → test → status matrix the coverage brief asks
for: for each Vulkan feature a game relies on, the fixture that proves it and
where it stands. Console status is **unverified** until the console test
service returns a run for the packaged fixtures; off-device status is from the
Wine runner (`app/tests/play-runtime.sh`) on this host.

## The fixtures

* `play-vulkan-dzn` (`app/tests/play-vulkan-dzn.cpp`, mode `vulkan-dzn`) — the
  startup path: instance → physical device → win32 surface → device →
  swapchain → render pass → two cleared presents. Proves the driver creates
  things; **not** that it renders.
* `play-vulkan-draw` (`app/tests/play-vulkan-draw.cpp`, mode `vulkan-draw`) —
  the render path: graphics pipeline from the checked-in SPIR-V
  (`tests/spirv/*.spv`, embedded through `tests/vulkan_spirv.h`), vertex
  buffer, one triangle into an offscreen target, compute dispatch, timeline
  semaphore, synchronization2 barriers, dynamic rendering, and buffer + pixel
  readbacks. Proves the driver **renders**; a driver that answers every query
  but draws nothing fails here. Exit 42 only when everything read back; 43 is
  BLOCKED (never a pass).

The SPIR-V is compiled off-device by glslang (SPIR-V 1.0, Vulkan 1.0 target)
and checked in beside the GLSL sources, so the build needs no shader compiler
and the fixture runs against a Vulkan 1.0 driver as well as dzn's 1.2. Every
enum and struct in the fixture is hand-declared (mingw has no `vulkan.h`, and
a guest must not depend on the loader's headers) and cross-checked against the
pinned Vulkan-Headers.

## Matrix

| Feature / API | Proven by | Off-device | Console |
|---|---|---|---|
| Loader discovery via `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` | `vulkan` mode: loader mapped from runtime dir, registry lists manifests | PASS (registry publication asserted) | **unverified** |
| Instance + enumeration (Khronos loader 1.4.313) | `vulkan-dzn`: instance-created, physical-device count | PASS (real device from dzn) | **unverified** |
| Null ICD fallback (`PLAY_DZN=0` runtime) | `vulkan` vs `PRUN_RUNTIME_DIR` without dzn: zero devices, `VK_ERROR_INITIALIZATION_FAILED` | PASS | **unverified** |
| `VK_KHR_win32_surface` on a guest HWND | `vulkan-dzn`: win32-surface-created | PASS | **unverified** |
| Swapchain on CoreWindow (HWND path, patch 0009) | `vulkan-dzn`: CreateSwapChainForHwnd hr=0 | PASS (Wine) / console PASS at 0.5.270.0 | PASS (0.5.270.0) |
| Render pass + cleared present | `vulkan-dzn`: two frame-presented | PASS / console PASS at 0.5.270.0 | PASS (0.5.270.0) |
| Graphics pipeline (VS+FS from SPIR-V) | `vulkan-draw`: graphics-pipeline stage | see below | **unverified** |
| Vertex buffers + vertex input (R32G32 pos, R32G32B32A32 colour) | `vulkan-draw`: draw-recorded → pixel-readback | see below | **unverified** |
| Draw + raster + clear-then-draw pixels | `vulkan-draw`: pixel-readback (inside red, outside clear) | see below | **unverified** |
| Dynamic rendering (no render pass; `VK_KHR_dynamic_rendering` / core 1.3) | `vulkan-draw`: rendering-info chain names the colour format | see below | **unverified** |
| Compute dispatch + storage buffer descriptor | `vulkan-draw`: compute-readback (64 uints ×3) | see below | **unverified** |
| Push constants | `vulkan-draw`: the ×3 multiplier travels by push constant | see below | **unverified** |
| Timeline semaphores (`VK_KHR_timeline_semaphore` / core 1.2) | `vulkan-draw`: signal 1 → host wait → host signal 2 → host wait | see below | **unverified** |
| Synchronization2 barriers (`VK_KHR_synchronization2` / core 1.3) | `vulkan-draw`: buffer barrier (compute→copy) + image barriers (layout transitions) | see below | **unverified** |
| Buffer↔image copy (readback/screenshot path) | `vulkan-draw`: vkCmdCopyImageToBuffer + row-pitch check | see below | **unverified** |
| Host-visible coherent memory, one allocation, aligned offsets | `vulkan-draw`: buffers-mapped stage | see below | **unverified** |
| Feature gating (`vkGetPhysicalDeviceFeatures2` chain) | `vulkan-draw`: features line names timeline/sync2/dynamicRendering; stages degrade honestly | see below | **unverified** |
| MSAA (4x/8x resolve) | none | gap | gap |
| Mip generation / array / cube images | none | gap | gap |
| Descriptor indexing (bindless; `EXT_descriptor_indexing`) | none — dzn gates it at shader model ≥ 6.6 | gap | gap |
| Pipeline caches (`VK_EXT_pipeline_creation_cache_control`) | none | gap | gap |
| Multi-draw / index buffers (`fullDrawIndexUint32`) | none — **dzn reports false** | gap (dzn limit) | gap (dzn limit) |
| Dual-source blend, logic ops | none — **dzn reports false** | gap (dzn limit) | gap (dzn limit) |
| Tessellation, multi-viewport, large points | none — **dzn reports false** | gap (dzn limit) | gap (dzn limit) |
| `robustness2`, `transform_feedback`, `maintenance5/6`, `extended_dynamic_state` | none — **absent from dzn** | gap (dzn limit) | gap (dzn limit) |
| 32-bit Vulkan (x86 guest) | none | gap ([X86-32BIT-PLAN.md](X86-32BIT-PLAN.md)) | gap |
| Vulkan layers (validation, GPU-assist) | none — loader default discovery only | gap | gap |

"see below" rows were run under Wine on this host against Mesa's lavapipe
(software Vulkan 1.4) where the runner can host a driver; the draw fixture's
first run on this host is recorded in the PR. The dzn rows above it are the
same fixture against the packaged dzn build.

## The dzn limits that matter to games (static, from mesa-25.3.6 `dzn_device.c`)

Read from the pinned source, not probed: `dzn` advertises Vulkan 1.2 with
`dualSrcBlend=false`, `logicOp=false`, `fullDrawIndexUint32=false`,
`tessellationShader=false`, `multiViewport=false`, `largePoints=false`, and no
`robustness2` / `transform_feedback` / `maintenance5` / `maintenance6` /
`extended_dynamic_state`. Consequences:

* **DXVK cannot host on dzn** — DXVK requires `fullDrawIndexUint32`,
  `dualSrcBlend` and `logicOp` for D3D10+ feature levels. D3D9 games stay on
  the d3d9 shim; dzn is not a second D3D route. (Full argument in
  [D3D-LAYERS-REPORT.md](D3D-LAYERS-REPORT.md) §2 #4.)
* A native Vulkan engine that stays inside the covered matrix (triangle
  pipelines, compute, timeline semaphores, dynamic rendering) can run; one
  that needs tessellation, multi-viewport or bindless cannot, and the fixture
  set deliberately does not paper over that — the gaps stay gaps.

## What this slice does not claim

* No console claim beyond the recorded 0.5.270.0 `vulkan-dzn` PASS; the draw
  fixture's console status is **unverified** until the console test service
  returns a run naming it.
* No game claim: no owned title runs Vulkan today; the acceptance is synthetic
  by design (VULKAN-PLAN.md §"A Vulkan game to test with").
* BLOCKED is never a pass: both fixtures print BLOCKED with the failing stage
  and the runner counts it separately.
