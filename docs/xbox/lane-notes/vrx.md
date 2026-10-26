# LANE_NOTES — lane `vrx` (BACKLOG uwph-vrx-spike)

## Objective (from `x64-run/lanes/vrx.prompt`, base `g/launcher`)
Find out on the real console how AppContainer's real `kernel32.dll` handles:
  (a) `VirtualAlloc(MEM_COMMIT, PAGE_EXECUTE_READWRITE)`
  (b) `VirtualAlloc` RW then `VirtualProtect` to `PAGE_EXECUTE_READ`
  (c) `VirtualAllocFromApp` RW + `VirtualProtectFromApp` to `PAGE_EXECUTE_READ` (documented UWP API)
— in each case: write a tiny `mov eax,42; ret` stub and call through a function pointer, wrapped in SEH.
This answers which memory-allocation path Wine's loader must use for JIT/codegen-style W^X code inside
the Xbox AppContainer sandbox (feeds into `uwph-kernel32-ntdll`, item 3 in `x64-run/LANES.md` §"uwph
track"). Separate throwaway package `Joao.XboxSteam.Vrx` so it never collides with the `Grok.XboxTest`
launcher package that lanes `app`/`dl` are actively using on the same shared console.

## Resume state (2026-10-25, session start)
Branch `lane/vrx` was freshly forked from `g/launcher` tip (`56acf688`) with no vrx-specific commits yet
— `lanes/vrx.log` only had the "starting claude" line. `xbox-test/vrx/` did not exist. This session is
building it from scratch.

## Plan
1. `xbox-test/vrx/main.cpp` — minimal CoreWindow+D3D11 UWP app (copied/trimmed from the M1 app,
   commit `2ab0b569`, not the full launcher) that on boot runs the 3 SEH-wrapped test cases once, logs
   `caseA/B/C: ... OK value=42 | FAILED err=N | EXCEPTION code=0x...` to `LocalState\vrx.log`, then idles
   rendering a heartbeat frame (so it behaves like a normal foreground UWP app and doesn't get reaped).
2. `xbox-test/vrx/AppxManifest.xml` — Identity `Joao.XboxSteam.Vrx`, same Publisher CN convention as
   `xbox-test/app` (`CN=grok-xbox-dev`, so the existing self-signed test cert flow in build.sh just works
   unmodified — the prompt's example `CN=BoxedwineDev` was illustrative, what matters is a distinct
   package Name). `codeGeneration` capability included (needed for the FromApp JIT path, case c).
3. `xbox-test/vrx/build.sh` — copy of `xbox-test/app/build.sh` pointed at the new dir/manifest/exe name.
4. Build, `xbox-test/dp/install.sh`, find the new PackageFullName (list via Device Portal packagemanager
   endpoint — no existing dp script lists packages, may add a tiny one-off query), launch, `wait-log.sh`
   for `vrx.log`, `get-file.sh` to pull it to `x64-run/runs/vrx/vrx.log`.
5. Write the verdict into this file (which path Wine's loader should use) and update
   `x64-run/BACKLOG.md` line 8 status.

## Must NOT do
- Never log/print `DP_PASS`/`TS_AUTHKEY`.
- `xbox-test/dp/*.sh` take `CONSOLE.lock` themselves — don't double-wrap with an outer `flock`.
- Console is shared with other running lanes (`app`, `deploy`/`dl`) — keep sessions short, only
  kill/uninstall the `Joao.XboxSteam.Vrx` package, never touch `Grok.XboxTest`.
- Local-only branch, never push.

## Status
In progress — building the app now.

## Resume/build milestone
Recovered the uncommitted app and built successfully with clang-cl + makemsix + osslsigncode.
Fixed case C to supply the required old-protection output pointer, flushed generated code before
execution, and included allocations inside SEH. Manifest includes codeGeneration; all allocations
use MEM_COMMIT|MEM_RESERVE at a null address. Console testing is next.

## Final result — verified on real Xbox Series S (2026-10-25)
Installed the signed isolated package successfully, launched via Device Portal (HTTP 200), and
fetched LocalState\\vrx.log to `/workspace/x64-run/runs/vrx/vrx.log`. A copy is committed at
`xbox-test/vrx/results/console-vrx.log`. Package full name:
`Joao.XboxSteam.Vrx_0.1.0.0_x64__dk715arakrne0`, publisher `CN=grok-xbox-dev`.

- A: VirtualAlloc(MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE) failed, GetLastError=87.
- B: VirtualAlloc(RW), write stub, VirtualProtect(RX), flush instruction cache, call: OK value=42.
- C: VirtualAllocFromApp(RW), write stub, VirtualProtectFromApp(RX), flush instruction cache,
  call: OK value=42 (manifest codeGeneration enabled).
- D3D11 initialized and the app rendered its first heartbeat frame. Stopped only the VRX package
  after fetching the completed log; no other package was killed or uninstalled.

Verdict: Wine's loader/code generator must avoid requesting writable executable pages directly.
Use RW allocation, write/relocate code, then switch to RX before executing. The documented UWP
VirtualAllocFromApp + VirtualProtectFromApp route is verified and is the recommended host API
path. Ordinary VirtualAlloc + VirtualProtect also succeeds for this RW-to-RX sequence on this
console. These results do not test rewriting code after execution, other capabilities, or other OS
versions. All tests were native on-device; no Wine/off-device result is used as console evidence.

Completed acceptance. No further lane work remains. Commits and backups are local only.
