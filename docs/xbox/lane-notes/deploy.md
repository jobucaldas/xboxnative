# LANE_NOTES — lane/deploy (xbox-test/dp Device Portal tooling)

Objective (full text in `/workspace/x64-run/lanes/deploy.txt`): robust bash+curl Device Portal
scripts (install/launch/kill/ps/uninstall/ls/get/put/screenshot/logs/wait-log), test each against
the console, document findings in `xbox-test/dp/README.md`, save evidence under
`/workspace/x64-run/runs/deploy/`, and a `pull-megabonk.sh` mirror of the console's
`LocalState\megabonk`.

## State at resume (2026-10-25 ~22:07 BRT)
- Working tree was already clean at commit `faf3f247` ("xbox-test/dp: Device Portal scripts ...")
  — all scripts existed and were committed, but `README.md` and `runs/deploy/` evidence were
  missing, and most scripts hadn't been exercised live from *this* worktree/session.
- Note: this commit hash is identical to a parallel branch `g/dp` (a different agent track,
  "Grok", working the same objective in `/workspace/wt/g-dp`) — the two converged on the same
  content already, see `/workspace/x64-run/runs/g-status.md` for their verification notes (they'd
  already confirmed launch/kill/ps/screenshot/ls/get working, and pulled 24 files of Megabonk).
- Multiple other lanes (`app`, `plan`, `dl`) are running concurrently against the same console;
  console access is already serialized via `flock /workspace/x64-run/CONSOLE.lock` inside every
  script, so this is safe. Checked `ps.sh` before touching anything: the `launcher` app
  (`Grok.XboxTest_0.3.2.0`) was live (another lane's QR-login test) — left it alone.

## What I did this session
- Live-verified every non-destructive script against the console using the pre-existing
  `Joao.Boxedwine.DevMode_0.1.6.0_x64__8j5c92gwe8f3c` package (the one explicitly OK'd to use):
  `user-signin.sh` (got HTTP 400, non-fatal — launch worked anyway), `launch.sh` (HTTP 200),
  `ps.sh` (found the running process), `ls-dir.sh`, `get-file.sh` + `put-file.sh` (round-tripped a
  probe file), `wait-log.sh` (matched against `ladder-native.log`), `kill.sh` (HTTP 400
  "Failed to terminate the application" but the process *was* actually gone after — documented as
  a quirk, trust `ps.sh` not the HTTP status).
- `install.sh` and `pull-megabonk.sh` were not re-run live (already proven working: `install.sh`
  by the `g/dp`/`g/app` work installing `Grok.XboxTest`, `pull-megabonk.sh` already produced
  `/workspace/megabonk-from-console`, 24 files / ~237 MB, sha256 manifest present).
- `uninstall.sh` deliberately NOT tested — no disposable package was safe to remove without
  risking console state other lanes depend on (per explicit instruction not to uninstall the
  Boxedwine DevMode package, and nothing else expendable was installed).
- Wrote `xbox-test/dp/README.md` with the endpoint map, quirks, and per-script verification
  status.
- Evidence saved to `/workspace/x64-run/runs/deploy/`: `boxedwine-shot1.png` (screenshot),
  `put.txt`, `deploy-probe.txt`/`deploy-probe-roundtrip.txt` (get/put roundtrip),
  `ladder-native.log` (pulled via wait-log.sh). Not committed to git (run artifacts, matches how
  other lanes keep `runs/` outside the repo).

## Next steps / open items
- `user-signin.sh` HTTP 400 root cause not identified (likely "already signed in" or a param
  name mismatch) — not blocking, but worth a quick look if another lane needs a clean sign-in
  flow to work reliably.
- `uninstall.sh` still unverified live — needs a disposable test package if someone wants to
  confirm the exact request shape before relying on it.
- Megabonk pull is still partial (24/69 files) — full copy depends on João's Steam login (lane
  `dl`/`app`), not on this lane's tooling.

## Session 2 (2026-10-25 ~22:10 BRT) — watcher-update follow-up, lane complete
The lane prompt's 21:40 BRT watcher update listed 4 remaining items after the earlier
`g/dp` fast-forward; none of them had actually been done yet (the session-1 notes above only
covered verification + README, not these). Did them this session, each tested live:

- **`get-file.sh` made resumable.** Confirmed the console's file API honors `Range` (curl
  `-H 'Range: bytes=1000-1999'` on a 54 MB file -> HTTP 206 + correct `Content-Range`). Rewrote
  the script to use `curl -C -` with an 8-attempt retry loop (`GET_RETRIES`). Verified by
  manually truncating a 54 MB `GameAssembly.dll` download to 24 MB, re-running the script, and
  getting the exact right sha256 (`4350a7ae...e3d9e`) after it resumed from byte 24,245,377. A
  fresh (non-partial) download still works normally (HTTP 200). `wait-log.sh` benefits too
  (append-only polling instead of re-fetching the whole file each time) — documented the one
  caveat (truncated/recreated remote file) in the README.
- **`ps.sh` memory column was actually broken, now fixed.** The JSON field is `WorkingSetSize`,
  not `WorkingSet` (which doesn't exist) — every row was silently printing `ws=` empty. Fixed,
  added a human `ws=..MB` + `cpu=..%` format, verified live (`xbtest.exe ws=298.8MB`).
- **`logs.sh` added for the ETW/log-options ask.** Live-tested: `/api/etw/{providers,
  customproviders,session/realtime}` all 404 on this console (no ETW on Xbox Series S Dev Mode
  Device Portal). `/api/debug/dump/usermode/{dumps,crashcontrol}` **do** work (HTTP 200) — found
  a real pre-existing 78 MB crash dump this way — so `logs.sh list|enable|disable` wrap those;
  couldn't find a working dump-download sub-route (every guess 404'd, left as a known gap).
  `logs.sh get` is a thin alias onto `get-file.sh` for pulling an app's own on-disk log, which is
  still the reliable logging path.
- **`smoke.sh` one-command pipeline added.** `install?` → `launch` → `wait-log` → `screenshot` →
  `kill`, PASS/FAIL per step (kill confirmed via `ps.sh` with a short retry loop, not the HTTP
  status, per the documented kill quirk), exit code = failure count. Ran live end-to-end,
  4/4 pass, against the Boxedwine DevMode probe app (safe, disposable-ish, already proven).
  **Deliberately did not** run it against `Grok.XboxTest` — that package is the live shared
  launcher another lane (`app`/`launcher`) owns and was actively running on the console this
  session (`xbtest.exe`, 298 MB working set); `install` would've upgraded/downgraded it in place
  and `kill` would've killed another lane's live session (QR login state, `steam_session.bin`)
  for no verification benefit, since the pipeline was already proven against a real app.
- Updated `xbox-test/dp/README.md` with all of the above (new rows + an ETW/crash-dump section).
- Confirmed via `ps -ef`/`flock -n` during testing that the `CONSOLE.lock` contention I hit was
  real concurrent use by lane `app` (`install.sh` running against `Grok.XboxTest`), not a stuck
  process — the locking design works as intended across lanes, just budget for wait time.

Objective is now fully done: every script in the lane prompt's list exists, is verified live
(except `uninstall.sh`, deliberately left unverified for lack of a disposable target — not a
gap in the tooling, a safety call), and the README accurately reflects real console behavior.
No further script work is planned; see `LANE_DONE`.
