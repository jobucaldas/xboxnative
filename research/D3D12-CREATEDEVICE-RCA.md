# D3D12CreateDevice DEVICE_RESET on console — root-cause analysis

## Symptom (console evidence, PR #160)

Selftest mode `d3d12` on 0.5.302/0.5.303 (built from head `0efeb97`):

```
PRUN EVENT guest D3D12GetDebugInterface hr=0x0
PRUN EVENT guest D3D12CreateDevice caller=play-d3d12.exe+0x11af
PRUN EVENT guest D3D12CreateDevice request adapter=default minimum=0xb000 iid=189819f1-1db6-4b57-be54-1821339b85f7
PRUN EVENT guest D3D12CreateDevice returned hr=0x887a0007 device=0000000000000000
PRUN EVENT EXE returned code=11
```

`0x887a0007` = `DXGI_ERROR_DEVICE_RESET`. Reproduced 3/3 alone on fresh
launcher starts, and 1/1 immediately after a `d3d11` PASS — not
order-dependent, not a dirty-state effect. Raw evidence (read-only):
`/tmp/xbn-evidence-302/d3d12-alone/`, `/tmp/xbn-evidence-303/selftest-d3d12.log`.

Under Wine 11.0 the same fixture creates the device fine and runs to the
compute readback (exit 43 = BLOCKED), so the failure is console-specific.

## The route

`src/xbox-test/app/host/play_runtime.h` hooks `d3d12.dll!D3D12CreateDevice`
(`createD3D12Device`, ~line 1630). What it does with the guest's adapter
argument:

- **adapter given**: the guest factory hands out wrappers
  (`corewindow_dxgi::Adapter` in `src/xbox-test/shared/corewindow_factory.h`);
  the route unwraps them through the private `realAdapterId` query
  (`real_adapter()`) and passes the real adapter to native
  `D3D12CreateDevice`. This is the same pattern the D3D11 route uses.
- **adapter == nullptr**: `real` stays null and the call goes through as
  `fn(nullptr, minimum, id, device)` — the console's native d3d12.dll is
  asked to pick an adapter itself.

The fixture (and therefore the log line `adapter=default`) passed nullptr.

## Why nullptr fails on the console

The console's adapter is `SraKmd_arden` (the Xbox APU, reported by the
D3D11 diagnostics: `vendor=0x1414 device=0xd000`). The key comparison:

- **D3D11 selftest PASSES on the same console, same process shape.** D3D11
  on this stack goes through d3d11on12, which internally must create a
  D3D12 device. So a native D3D12 device is creatable in-process.
- The guest's D3D12 call differs from what d3d11on12 does internally:
  **adapter = nullptr**. d3d11on12 is handed (or enumerates) an explicit
  adapter.

`D3D12CreateDevice(nullptr, ...)` on a normal Windows machine means "use
the default adapter" (dxgi's default enumeration inside d3d12.dll). On the
console's driver stack (SraKmd, not WDDM's dxgi), the null default path
apparently cannot resolve an adapter and the device creation is reset
(`DEVICE_RESET`) rather than falling back. The debug interface call
succeeds (`hr=0x0`), so d3d12.dll itself is loaded and alive — only the
device creation with a null adapter fails.

Hypotheses considered:

1. **nullptr adapter not honoured by the console's d3d12** — fits all the
   evidence; the explicit-adapter path is what d3d11on12 effectively does
   and what the D3D11 route already does (`real_adapter`).
2. Feature level 0xb000 too low / wrong — unlikely: the console supports
   FL 11_0 for D3D11, and D3D12 device creation failing with DEVICE_RESET
   (not E_INVALIDARG / DXGI_ERROR_UNSUPPORTED) points at the adapter
   resolution, not the level. Still tested by the fixture (12_0 fallback).
3. Device already owned by the Play swapchain/d3d11 stack — unlikely: the
   d3d12-alone runs (no d3d11 in the process, fresh launcher start) fail
   identically, so there is nothing to be owned by.
4. IID mismatch — ruled out: `189819f1-...` is `ID3D12Device`, the same IID
   the off-device Wine run (which succeeds) logs.

## Fix in the fixture (play-d3d12.cpp)

The device-creation step now tries the matrix a game would, and the host
route logs each attempt separately (`adapter=default|given`,
`minimum=0xb000|0xc000`), so the console log names the working shape:

1. `D3D12CreateDevice(nullptr, FL 11_0)` — the original shape (exit 11 if
   it fails and nothing else works).
2. `CreateDXGIFactory2` + `EnumAdapters1(0)` →
   `D3D12CreateDevice(adapter, FL 11_0)` — the explicit-adapter path.
3. Same explicit adapter at `FL 12_0`.
4. All fail → exit 83 (a distinct step number, not a pass, not BLOCKED).

The adapter handed over is the guest factory's wrapper; the Play route
unwraps it to the real adapter before calling native d3d12 (that unwrap is
exercised only when the wrapper is passed, which attempt 2/3 do).

## What the next console run decides

- If attempts 2/3 succeed (`adapter=given` + `returned hr=0x00000000`), the
  RCA is confirmed: the console requires an explicit adapter; the fixture
  proceeds and the rest of the coverage (swapchain, triangle readback,
  compute, resize) runs as under Wine.
- If all three fail, the log will now show all attempts separately, which
  narrows the RCA to the native call itself (driver-level), and exit 83
  names the step unambiguously.

## Status

- Fixture change: implemented, mingw compile-check OK, diag contract
  unaffected (route unchanged — the fix is on the guest side).
- Console verification: **unverified** — pending a run via the console
  test service on the new head.
