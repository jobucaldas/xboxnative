# D3D12 for Play: plan and milestone 1

Status: milestone 1 (routes, diagnostics, fixture) implemented and verified off-device under Wine.
No console run has happened and nothing here claims a game renders. Evidence labels: **[verified]** read in
the pinned source, run off-device, or observed in a Wine probe; **[unverified]** needs the console.

## Problem

Play runs owned Steam games in-process on the console. D3D11 guests work (Megabonk, Halo Wars, Going Under,
Slime Rancher). D3D12 guests have no route at all: `d3d12.dll` resolves to the console's own DLL for every
export, but nothing in Play wraps `D3D12CreateDevice`, nothing patches the device, and the DXGI factory a
D3D12 guest creates only answered up to `IDXGIFactory2`. A D3D12 game would either fail at the factory call
or hand a guest HWND to native DXGI, which does not know guest windows.

## What a D3D12 guest needs, and where Play blocked it

| Need | Before | Now |
|---|---|---|
| `D3D12CreateDevice` | fell through to the system route (unwrapped) | `play_runtime::createD3D12Device`: unwraps a guest adapter to the real one, logs caller/request/result, adopts the device for diagnostics and for the guest-window factory, marks milestone 2 **[verified off-device]** |
| `D3D12GetDebugInterface` | system route; a guest that treats `DXGI_ERROR_SDK_COMPONENT_MISSING` as fatal died silently | wrapper logs the answer once **[verified: Wine returns `0x887a002d`]** |
| `D3D12SerializeRootSignature`, `D3D12CreateRootSignatureDeserializer` | system route, no trace | wrappers log the failure once with version/parameter count and byte count **[verified off-device]** |
| `D3D12EnableExperimentalFeatures` | system route | wrapper logs the result once; the DXIL path depends on it (see risks) **[verified off-device: call reaches the system DLL]** |
| DXGI factory levels | `corewindow_dxgi::Factory` answered `IUnknown`..`IDXGIFactory2` only, so `CreateDXGIFactory2(…, __uuidof(IDXGIFactory4), …)` failed | answers `IDXGIFactory3`..`7` only when the real factory has the level, forwarding every call; `EnumAdapterByLuid`/`EnumWarpAdapter`/`EnumAdapterByGpuPreference` wrap the adapter they return **[verified off-device: 52 factory checks, 0 failures]** |
| Swap chain on a guest HWND with the command queue as the device argument | `CreateSwapChainForHwnd` already mapped a guest HWND to the CoreWindow for D3D11, and takes any `IUnknown*` device, so a queue works unchanged | unchanged; the D3D12 fixture exercises it **[verified off-device under Wine; console unverified]** |
| The factory a D3D12 device's adapter belongs to | a D3D12 device is not an `IDXGIDevice`, so `adopt_device` never found its factory | `play_graphics::adopt_factory_of` reads `GetAdapterLuid`, asks a real `IDXGIFactory4` for the adapter, takes its parent factory and adopts that class **[verified off-device]** |
| Diagnostics | none for D3D12 | `host/d3d12_diag.h`: 20 device hooks, queue/list/fence/resource/allocator hooks, capability answers, bounded dedupe **[verified off-device: 110 checks, 0 failures]** |
| Fences and events | guest `CreateEventEx`/`WaitForSingleObject` already work through uwph kernel32 | unchanged; the fixture waits on a fence each frame **[verified off-device under Wine]** |
| HLSL/DXIL | the guest compiles its own shaders or ships DXIL; Play does not compile | unchanged. The console reported shader model 6.0 under Wine; the console's DXIL acceptance is unverified (see risks) |
| Resource heaps, W^X | D3D12 heaps are ordinary `VirtualAllocFromApp` memory; no RWX is involved | unchanged |

## Milestone 1: what was built

- `src/xbox-test/app/host/d3d12_diag.h` — the diagnostics, modelled on `d3d11_diag.h`. Failed create,
  query, submit, map and fence calls are logged once per distinct call (keyed on the arguments that
  identify the failure, so one texture failing at several sizes logs once but a different format logs
  again), capped at 200 failures and 160 capability answers. On adoption it reports the answers that pick a
  game's renderer path: shader model, architecture (UMA/tile-based), options, options5, GPU virtual address
  bits, device-removed reason and the adapter LUID.
- `src/xbox-test/shared/corewindow_factory.h` — `Factory` now implements `IDXGIFactory7`, answering higher
  levels only when the real factory has them and wrapping every adapter it hands out.
- `src/xbox-test/app/host/play_runtime.h` — five `d3d12.dll` routes.
- `src/xbox-test/app/host/play_graphics.h` — `adopt_factory` extracted from `adopt_device`, and
  `adopt_factory_of` for D3D12 devices.
- `src/xbox-test/app/tests/play-d3d12.cpp` — the fixture guest: device, shader-model query, direct queue,
  factory, 640x480 R8G8B8A8 flip-discard chain with three buffers on a guest HWND, RTV heap and views,
  allocator, list, fence, event, three frames of barrier → clear → barrier → close → execute → present →
  signal → wait, then two rejected `CreateCommittedResource` calls. Exit code 42 means every step passed;
  any other code names the step.
- `src/xbox-test/app/tests/d3d12-diag.cpp` and `d3d12-diag.sh` — off-device contracts of the diagnostics
  against a native Wine d3d12 device.
- `play-runtime.sh` gained a `d3d12-diag` mode and `play-runner.cpp` the matching branch.

### Off-device results (Wine 11, vkd3d-proton-backed d3d12)

```
OFF-DEVICE D3D12 failure diagnostics checks=110 failures=0 lines=211; no rendering claim
OFF-DEVICE CoreWindow factory contracts checks=52 failures=0; no rendering claim
```

and the fixture through Play:

```
PRUN EVENT guest D3D12CreateDevice request adapter=default minimum=0xb000 iid=…
PRUN EVENT guest D3D12CreateDevice returned hr=0x00000000 device=…
PRUN D3D12 CheckFeatureSupport feature=7(SHADER_MODEL) size=4 highest=0x60
```

## Which owned games use D3D12

Static imports of the installed copies (`<local steam-owned games directory>`, never committed) **[verified with
`llvm-readobj --coff-imports`]**:

| Game | Module | `d3d12.dll` imported | Notes |
|---|---|---|---|
| Megabonk | `UnityPlayer.dll` | yes (`D3D12SerializeRootSignature`, two ordinals) | Unity; the launcher forces D3D11 today |
| Slime Rancher | `UnityPlayer.dll` | yes | Unity |
| Say No! More | `UnityPlayer.dll` | yes | Unity |
| Fields of Mistria | `SDL3.dll` | loads `d3d12.dll` by name at runtime | its renderer is OpenGL, not D3D12 |
| Halo Wars: DE, Puyo Puyo Tetris 2, Mega Man Legacy Collection | — | no | D3D11 / D3D9 |

So **no owned game is currently a D3D12 renderer**; three Unity titles import the DLL because Unity's
`UnityPlayer.dll` links every backend it can use. The D3D11 route Play already has is what makes them run.
The first real D3D12 target has to come from the store badges in `docs/GAMES.md` (any title whose Steam
requirement text says DirectX 12) rather than from this set.

## Risks and unknowns

1. **DXIL acceptance on the console [unverified].** Wine's d3d12 reports shader model 6.0, so the API surface
   is there. Whether the console runtime accepts DXIL from a guest module — through `dxil.dll` signing or
   `D3D12EnableExperimentalFeatures(ExperimentalShaderModels)` — is the single largest unknown, the same one
   `OPENGL-PLAN.md` records for Mesa's d3d12 driver. Milestone 2 should answer it with a fixture that
   creates a tiny DXIL pipeline state.
2. **`d3d12core.dll` [unverified].** Windows ships the D3D12 core as a separate DLL that `d3d12.dll` loads.
   The console has it (the OS uses D3D12 itself), but whether an AppContainer guest can reach it is untested.
3. **Resource heaps in a UWP app [unverified].** D3D12 heaps are ordinary committed memory, which uwph
   already serves, but the console's memory budget for a guest in Game mode (5 GiB) is the same constraint
   the D3D11 titles hit.
4. **W^X [verified off-device, not a D3D12 issue].** D3D12 does not need executable guest memory; the
   existing RWX emulation covers shader JIT (Mono) separately.
5. **The debug layer is absent** (`DXGI_ERROR_SDK_COMPONENT_MISSING`), so a D3D12 game that enables it must
   tolerate the failure. The wrapper logs it once so a silent death is traceable.

## Next milestones

2. **DXIL on the console.** A fixture that builds a minimal DXIL pipeline state (from a pinned DXC build or a
   pre-compiled blob) and creates it; record the exact HRESULT on the console. If the runtime refuses DXIL,
   stop and record it — the fallback is the same one `OPENGL-PLAN.md` names.
3. **A real D3D12 game.** Pick an owned title whose store badge says DirectX 12, install it, and run it
   through the launcher. The diagnostics from milestone 1 name the first failing call.
4. **Flip-model presentation through the CoreWindow.** The fixture's chain goes through the same
   `CreateSwapChainForCoreWindow` path D3D11 uses; the console run must confirm the frame actually appears.

## Licensing and policy

No third-party code was added: the diagnostics, routes and fixtures are this repository's own. No game or
Valve file is touched; D3D12 is substituted in memory like the other Play modules. Nothing here weakens a
gate: a fixture that cannot create a swap chain exits with the step number rather than counting as a pass.
