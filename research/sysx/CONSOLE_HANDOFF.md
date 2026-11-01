# sysx console integration handoff

This branch implements the sysx routes. Whole-area acceptance remains open.
The console-owning vis worktree currently lacks `sysx_native` integration;
saved console export counts therefore cannot certify this branch.

## Production changes to integrate

Review these local commits in order (do not copy lane notes over vis notes):

- `eebb0017`: native dbghelp/WinINet allowlist and validated IME detach.
- `db0fdde8`: eleven-export discovery diagnostics.
- `f106736c`: DWM calls against validated guest window records.
- `da6c9e2a`: foreign IME context errors.

Production files are `xbox-test/app/host/mlp_probe.h`,
`xbox-test/uwph/sysx/native.h`, `xbox-test/uwph/imm32/route.h`, and
`xbox-test/uwph/dwmapi/route.h`. Keep the existing user32 record callbacks:
passing guest HWND values to desktop DWM is invalid. Resolve native symbols
before the DLL-level stub fallback. Invoke `sysx_native::discover` from the
existing one-time discovery path. Missing native exports must remain unresolved.

## Verification gates

Before console installation, run the unchanged `xbox-test/smoke-all.sh --offline`
and host regressions documented in `xbox-test/uwph/sysx/README.md`, plus IMM32
and DWM regressions. Host passes establish no Xbox API-operation claim.

Console ownership remains with vis. Use `/workspace/x64-run/CONSOLE.lock`, a
fresh monotonically increasing package version, and a short milestone run.
Capture all eleven `SYSX NATIVE ... available=` lines, import summaries and
actual operation results. Export discovery alone is insufficient: exercise
symbol-session initialization/lookup and HTTP request/query/close behavior
under the real AppContainer policy before clearing pending native rows.
Preserve unavailable-export and unsupported-operation errors. Networking must
use permitted TCP/IP, never AF_UNIX or policy exemptions.

The saved vis run stops at the legitimate Steam real-client prerequisite after
CoreWindow swapchain creation. Do not remove that guard to obtain sysx evidence.
Steam/process integration belongs to stm/execution owners. No menu, gameplay,
Xbox native-operation result or whole-area completion is implied here.

## Pending inventory

Regenerate with `python3 research/sysx/audit.py`. At this handoff there are
222 owned imports and 20 pending rows: eleven native-export/API checks, two
partial DWM entries, and seven explicitly deferred audio entries. DWM policy
changes return `E_NOTIMPL` because the guest backend has no policy renderer.
`LANE_DONE` must remain absent until the requested area is implemented and
verified on Xbox; deferred audio stays outside first-version scope.
