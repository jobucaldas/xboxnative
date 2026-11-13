# D3D11 vtable diagnostics

`src/xbox-test/app/host/d3d11_diag.h` hooks entries of the D3D11 device and
immediate-context vtables (and the DXGI swap chain's Present/ResizeBuffers, in
`guest_overlay.h`) to log what a guest actually calls and what fails. The logs
answer questions the guest's own output cannot: did a `Create*` call fail, what
does the guest draw, and why did a device get removed.

## What gets hooked

* Every `ID3D11Device` `Create*` factory and the capability probes
  (`CheckFormatSupport`, `CheckMultisampleQualityLevels`,
  `CheckFeatureSupport`): a failed call is logged once per distinct
  (method, hr, key parameters).
* The immediate context's draw/dispatch/clear/copy family: counted with
  relaxed atomics and summarised through the sink every ~10 s for the first
  minutes of a run (draws bucketed by primitive topology, hull/domain/geometry
  shader presence and target format; render-target sets; clears; MSAA
  resolves; deferred-context command lists).
* The DXGI swap chain's `Present`, `Present1` and `ResizeBuffers`: the first
  failed Present asks `GetDeviceRemovedReason` once and reports the removal
  reason; every resize is logged with its index in the run, its parameters,
  and how many render targets were still bound on the immediate context when
  the guest asked for it (outstanding-RTV evidence at resize time).
* With `d3d11resizetrace.txt` present, each Present result and failure reason,
  the swap-chain descriptor at each resize, each resize result (including
  elided/suppressed S_OK results), and a bounded ring of the last 64 intercepted
  D3D11 calls are logged when a Present fails. The ring covers hooked calls, not
  every D3D11 interface method.

## Slot audit (build-time)

The hook table names vtable entries by number. Those numbers are checked
against the Windows SDK's own vtable layout at build time by
`src/xbox-test/app/tests/d3d11-diag-slots.sh`: it derives each slot with
`offsetof` on the SDK's C-style `*Vtbl` structs and asserts it equals the
number the diagnostics hook by. A wrong slot fails that build instead of
forwarding a guest call to the wrong method on the console. CI runs it in the
`registry-persistence` job; it needs only the mingw cross compiler (no Wine,
no device).

## Kill switch

The file `d3d11diag.txt` in the launcher's `LocalState` folder disables every
D3D11 vtable patch for the whole run (its presence is what matters; contents
are ignored). The launcher checks it once at boot, before any guest runs.
Default is ON (hooks installed) when the file is absent.

With the switch armed the run's log still contains one line:

```
PRUN D3D11 diagnostics off (LocalState\d3d11diag.txt): no vtable hooks installed
```

so a log always shows which mode it had. The DXGI resize logging above stays
live with the switch armed — it reports through the same sink but does not
depend on the vtable patches.

## Resize-removal trace (opt-in)

Create `d3d11resizetrace.txt` in `LocalState` to enable the focused trace for
one run; absent is the default and leaves the call ring off. The trace includes
swap-chain descriptor fields, ResizeBuffers arguments and results, Present
results, a device-removed reason for each failed Present, and the newest 64
intercepted D3D11 calls when a Present fails. It is a diagnostic trace, not a
complete API recorder; calls through unhooked D3D11 methods are not represented.
The trace is independent of `d3d11diag.txt`, but the D3D11 call ring requires the
regular vtable hooks to be installed.

This trace can support A/B testing of device-removal timing, but a single run
cannot establish causality; compare repeated runs and treat differences as
suggestive until reproduced. Delete the file to turn this trace off.
