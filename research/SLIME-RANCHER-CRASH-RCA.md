# Slime Rancher device-removal crash — RCA (0.5.279.0, attempt 2292:1)

Date: 2026-11-11. Evidence: read-only console pull in `/tmp/xbn-evidence-crash-0713/`
(`boot.log`, `launch_probe.log`, `vis-player.log`, `storage_report.txt`), plus prior
0.5.275 runs in `/tmp/xbn-evidence-275/slime-relaunch/` for comparison.

## Signature

```
[1636.702] exception stack[0] unityplayer.dll+0x452c83 (RVA 0x452ba0)
            … → +0x466dc8 → +0x45525b → +0x5082ca → +0x50f22d → +0x50f308
            → +0x83dfa8 → xbtest.exe+0x23a183 → ntdll.dll+0xaac32
[1636.702] unhandled exception code=0xc0000005 address=0x271d4452c83
           unityplayer.dll+0x452c83 access=0 (READ) target=0x0 (null deref)
```

- `vis-player.log` ends with two Unity render-texture creation failures
  (800x600, fmt 27 and fmt 19, `error 0x887a0005` = DXGI_ERROR_DEVICE_REMOVED),
  matching the probe's failed `CreateTexture2D` calls.
- Device-removal reason from `GetDeviceRemovedReason`: **0x887a0007
  DXGI_ERROR_DEVICE_RESET** ("device reset (invalid command)") — not a TDR
  timeout and not budget eviction.
- Memory was **not** exhausted: local segment used 556 MB of a 3,986 MB budget
  (`device removed: memory local budget=3986266522 used=556945408 …`).

## Timeline (boot.log)

| t (s) | event |
|-------|-------|
| 1577.9 | launch via controller; game boots healthy, 60 fps, 8.6 ms presents |
| 1593–1623 | three healthy 10 s frame windows; resizes 1→3 (800x600 → 1920x1080) |
| ≈1623–1633 | the 4th swapchain resize window; device removed (Present hr=0x887a0005, reason 0x887a0007) |
| 1633.1 | frames summary: n=5, avg 2004.6 ms — Present blocked ~2 s per call |
| 1636.7 | unityplayer null-deref crash (above) |
| 1643.1 | last frames line: resizes=4, last resize hr=0x887a0005 |
| 1686→1986 | only discord-ipc retries; guest is a zombie (xbtest.exe alive, 0 % CPU) |

## Causal chain

1. The game reconfigured its presentation (the 4th `ResizeBuffers` in the run).
2. The console's virtual-GPU submission path rejected/hung on a command in
   that window; the OS/device recovered as DXGI_ERROR_DEVICE_RESET
   (0x887a0007) with the swapchain resize surfacing 0x887a0005.
3. Unity 2019.4's device-lost recovery ran and dereferenced a null pointer
   (unityplayer+0x452c83), crashing the guest before it could recreate the
   device. The crash is secondary; the primary event is the driver-level
   device reset.

## When it appeared (build history)

| build | evidence | device removal (0x887a0005 / reason 0x887a0007) |
|-------|----------|--------------------------------------------------|
| 0.5.252 | `/tmp/xbn-runs/play-252-433340/` (+ `boot-252-*.log`) | **none** — 2 resizes, every resize `hr=0x00000000`, zero removal lines |
| 0.5.275 | `/tmp/xbn-evidence-275/slime-relaunch/run{1,2,3}` | present in all 3 runs (25/12/47 `0x887a0005` lines; reason 0x887a0007) |
| 0.5.279 | `/tmp/xbn-evidence-crash-0713/` | present; this run the removal killed the guest (crash above) |

The removal is first seen in kept evidence on 0.5.274-and-later builds (no
0.5.274-specific capture is kept; 0.5.252 predates it and is clean, 0.5.275
shows it). It has **not** been bisected against #131 (HID pad presentation),
#133 (device-removal reporting) or #137 (GS draw diagnostics) — #131 and #133
are already in 0.5.275, so "predates those PRs" cannot be claimed either way.
The kill-switch experiment below (locking the swapchain to its first mode) is
the discriminator: if the game survives with resizes suppressed, the trigger is
in the resize path, not in the PRs' code paths.

## Proven vs. unproven

**Proven:** device removal with reason DXGI_ERROR_DEVICE_RESET between the 3rd
and 4th swapchain resize; memory was not exhausted; the guest zombie state
follows Unity's failed device-lost recovery; the same signature family on
0.5.275 (three runs) and 0.5.279, with 0.5.252 clean.

**Unproven (needs a follow-up console run with more diagnostics):** the exact
command that triggered the reset. The d3d11_diag Create*/Check*/Map hooks show
no failed resource creation immediately before the removal, so the invalid
command reached the driver at Present/flush time. Unity's crash is downstream.
Also unproven: which change between 0.5.252 and 0.5.275 introduced the trigger
(candidates #131/#133/#137 are untested; the lockmode experiment discriminates
resize-path vs not, not PR-vs-PR).

## Next steps

1. Extend d3d11_diag to keep a small ring of the last N successful
   context commands and flush it when `GetDeviceRemovedReason` first reports a
   removal — that names the triggering submission.
2. Test whether disabling the 4th resize (locking the game to its initial
   mode, the `LocalState\lockmode.txt` opt-in shipped with this RCA) avoids
   the reset; that would confirm the trigger is in the resize/mode-change
   path rather than in scene-load rendering. It also bounds the regression:
   if 0.5.275+ survives under lockmode where 0.5.252 survived without it,
   the trigger is resize-coupled regardless of which PR moved it.
3. Make Unity's recovery survivable: the null-deref is in Unity's own code, so
   the practical mitigation is preventing the reset (1) or returning a
   recoverable error code from the resize path instead of letting the reset
   propagate.

## Kill-switch A/B (0.5.302.0 @ a9c9cb4, 2026-11-12)

Six 4-minute synthetic launches (menu only, no settings interaction), three
with `LocalState\d3d11diag.txt` (diagnostics off) and three without. Evidence:
`/tmp/xbn-evidence-302/slime-ab/run{1,2,3}-diagoff/`, `run{4,5,6}-diagon/`.

| run | diag | removal | resizes |
|-----|------|---------|---------|
| 1 | off | Present 0x887a0005, reason 0x887a0020, ~2 min into gameplay | #1,#2 hr=0 |
| 2 | off | Present 0x887a0005, reason 0x887a0020, ~2 min into gameplay | #1,#2,#3 hr=0 |
| 3 | off | none (clean 240 s) | #1,#2 hr=0 |
| 4-6 | on | none | #1,#2 hr=0 |

What this settles:

* **The kill switch works**: the `diagnostics off (LocalState\d3d11diag.txt):
  no vtable hooks installed` line appears in every OFF run and no ON run, and
  the per-resize logging stays live with the switch armed.
* **The vtable hooks are NOT supported as the cause — the A/B points the
  other way**: the removal reproduced only in the diag-OFF arm (2/3) while
  the hooks-installed arm was clean (0/3). If the hooks caused the removal,
  the ON arm would show more removals, not fewer. What the data actually
  show: the removal exists **without any of our hooks installed**, so it is
  a pre-existing driver/vGPU or Play-path problem, not a hook bug (the
  slot audit in #152 already found no mismatch statically). The ON arm's
  cleanliness is consistent with the hooks' overhead perturbing timing and
  masking a timing-sensitive bug — but it is equally consistent with plain
  noise: two-sided Fisher exact on 2/3 vs 0/3 gives p≈0.3, not significant.
  Both readings remain open at n=3.
* **The removal is not resize-coupled here**: both failures landed ~2 min into
  gameplay with every completed resize at hr=0, unlike the 0.5.279 crash
  (4th-resize window). The lockmode experiment remains the discriminator for
  the settings-change path the owner actually hit.
* **The reason code is first-read vs second-read**: with the hooks off, the
  only reader is `presentFailure`, which reports 0x887a0020
  (DRIVER_INTERNAL_ERROR). The 0.5.275-era 0x887a0007 "invalid command" line
  came from `noteFailure` (#133), a second `GetDeviceRemovedReason` call that
  only runs when the hooks are installed — the driver answered differently on
  the later read. "Invalid command" was possibly never the driver's first
  answer; evidence collected with hooks on must be read with that in mind.

Next discriminators, in order: more diag-OFF runs (6-8) to establish the
OFF-arm rate; then the `lockmode.txt` opt-in for the settings-change path the
owner actually hit.

## Lockmode A/B (0.5.303.0 @ 0efeb97, 2026-11-12, gateway)

Four synthetic launches on 0.5.303.0 (settings change + new session), two with
`LocalState\lockmode.txt` (later resizes suppressed with S_OK after #1/#2 go
through) and two without. Evidence: `/tmp/xbn-evidence-303/slime-lockmode/`.

| arm | removals | behaviour |
|-----|----------|-----------|
| lockmode ON (2 runs) | 0 | #1/#2 hr=0, then resizes suppressed with S_OK (marker line present) |
| lockmode OFF (2 runs) | 0 | #1–#4 all hr=0, second device created hr=0 |

No device removal in any of the four runs — 0.5.303.0 survives both arms in
synthetic runs. With the same caveat as the kill-switch A/B (n=2 per arm,
failure is stochastic, two-sided Fisher on 0/2 vs 0/2 is uninformative), this
neither confirms nor refutes the resize-path trigger; it only says the
synthetic menu-only run does not reproduce the owner's settings-change crash
in either arm. The lockmode marker line and the suppression semantics worked
as designed in both ON runs.

## Next discriminators (updated after both A/Bs)

1. More diag-OFF runs (6-8) to establish the OFF-arm base rate.
2. A longer diag-OFF soak (30+ min into real gameplay, past the ~2 min mark)
   to check whether the removal correlates with the ~2-minute mark — the
   asset-burst/GPU-memory window both A/B failures landed in.
3. The `lockmode.txt` opt-in on the owner's real settings-change path (the
   synthetic runs do not exercise it hard enough to discriminate).
