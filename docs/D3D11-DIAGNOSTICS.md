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

This exists for A/B testing: if a device removal reproduces with the
diagnostics off, the hooks are exonerated; if it stops reproducing, the hooks
are the prime suspect. Delete the file (or reinstall without it) to turn the
diagnostics back on.
