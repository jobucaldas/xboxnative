# xbox-test/dp — Device Portal tooling

Bash+curl scripts for the Xbox Series S Device Portal (`https://<console>:11443`), reached either
directly on the LAN or via a tailscale SOCKS5 proxy (through "Encom", default `localhost:1055`).
All scripts source `dp-lib.sh` and serialize console access with
`flock /workspace/x64-run/CONSOLE.lock` (safe for multiple concurrent lanes).

Auth: HTTP Basic (`jobucaldas:$DP_PASS`, `DP_PASS` from env, never printed/logged) plus a
CSRF token read back from the `CSRF-Token` cookie set by an initial `GET /api/os/info` — every
subsequent request sends `X-CSRF-Token: <cookie value>`. `dp-lib.sh` handles this automatically
via a per-process cookie jar.

Env overrides: `DP_HOST` (default `192.168.15.96`, LAN IP can change), `DP_SOCKS` (default
`localhost:1055`), `DP_LOCK`, `DP_TIMEOUT` (default 120s per request).

## Scripts (all verified live against the console 2026-10-25, except where noted)

| Script | Verified | Notes |
|---|---|---|
| `install.sh PKG.msix [CERT.cer]` | yes | Upload via `POST /api/app/packagemanager/package`, then polls `GET /api/app/packagemanager/state` for `Code: 0`. Used successfully to install `Grok.XboxTest_0.2.0.0_x64__dk715arakrne0` (HTTP 202 -> state Success). |
| `launch.sh RID FULL` | yes | `POST /api/taskmanager/app?appid=<b64 PackageRelativeId>&package=<b64 PackageFullName>` with `Content-Length: 0` (omitting it gets HTTP 411). `RID` is `Family!App` (PackageRelativeId), `FULL` is the full package name. Returns HTTP 200 and the process appears in `ps.sh` within ~1-2s. |
| `kill.sh FULL` | yes (with caveat) | `DELETE /api/taskmanager/app?package=<b64 full>&forcestop=yes`. **Quirk**: can return HTTP 400 `{"ErrorMessage":"Failed to terminate the application.","ErrorCode":-2147483635}` even though the process is actually gone afterward — always confirm with `ps.sh`, don't trust the HTTP status alone. |
| `ps.sh [grep-pattern]` | yes | `GET /api/resourcemanager/processes`; prints `name pid=.. cpu=..% ws=..MB pkg=..`, optional case-insensitive regex filter on the formatted line. Memory field in the console's JSON is `WorkingSetSize` (bytes) — not `WorkingSet`, which doesn't exist and silently printed empty; fixed and verified live (e.g. `xbtest.exe ws=298.8MB`). |
| `uninstall.sh FULL` | **not tested** | `DELETE /api/app/packagemanager/package?package=<urlenc full>`. Deliberately not exercised against real packages in this pass (nothing disposable was installed that was safe to remove without risking the Boxedwine DevMode or Megabonk work other lanes depend on). |
| `ls-dir.sh FULL REMOTE_DIR` | yes | `GET /api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=..&path=..`. Lists `LocalState\*` (and subdirs) with type (`d`/`-`) and size. |
| `get-file.sh FULL REMOTE_DIR FILENAME LOCAL_OUT` | yes | `GET /api/filesystem/apps/file?...&filename=..&path=..`. Round-tripped a small text file byte-for-byte. **Resumable**: the endpoint honors `Range` requests (verified: `Range: bytes=1000-1999` on a 54 MB file -> HTTP 206 + correct `Content-Range`), so the script uses `curl -C -` and retries (`GET_RETRIES`, default 8) on a dropped connection, picking up from the local partial file instead of restarting. Verified live: truncated a 54 MB `GameAssembly.dll` download to 24 MB, re-ran, got the remaining bytes via HTTP 206, final sha256 matched the known-good manifest exactly. |
| `put-file.sh FULL REMOTE_DIR LOCAL_FILE` | yes | `POST /api/filesystem/apps/file?...` multipart upload into `LocalAppData\<pkg>\<REMOTE_DIR>`. Round-tripped with `get-file.sh`. |
| `screenshot.sh OUT.png` | yes | `GET /ext/screenshot` (not the undocumented `/api/xbox/screenshot` or other `/ext/xbox*` guesses — those were not needed, `/ext/screenshot` alone returns a real PNG, HTTP 200, ~180-900 KB depending on scene). |
| `wait-log.sh FULL FILE PATTERN [TIMEOUT_S] [OUT]` | yes | Polls `get-file.sh` every 3s and greps the pulled copy with `grep -E`. Since `get-file.sh` is now resume-aware, repeated polls against a growing log append rather than re-downloading the whole file each time; if the remote file is ever truncated/recreated smaller than the local copy, delete the local `OUT` first (Range past EOF returns HTTP 416, treated as "already complete" rather than "stale"). |
| `user-signin.sh [USER]` | partially | `PUT /ext/user?user=..`. Returned HTTP 400 when the user was (most likely) already signed in; launch worked regardless without calling this first in that case. Treat failures here as non-fatal and retry `launch.sh` anyway — not yet root-caused.
| `pull-megabonk.sh [LOCALDIR] [REMOTE_SUBPATH]` | yes | Recursively mirrors `LocalState\megabonk` (default, override via args/`PKG_OLD`) to a local directory with retries + a final `sha256sum` manifest. Used to pull 24 files / ~237 MB off the console (old `Joao.Boxedwine.DevMode_0.1.6.0...` package) into `/workspace/megabonk-from-console` — this is a **partial** copy (24 of the known 69 files / ~523 MB); a full copy still needs a fresh Steam download via João's login, not a console pull. |
| `logs.sh list\|enable\|disable\|get ...` | yes | See "ETW / crash dump findings" below. |
| `smoke.sh RID FULL LOGFILE PATTERN [...]` | yes | One-command `install?`→`launch`→`wait-log`→`screenshot`→`kill` pipeline with a PASS/FAIL summary (exit code = failure count). Verified live end-to-end (4/4 pass) against the Boxedwine DevMode probe app: `./smoke.sh 'Joao.Boxedwine.DevMode_8j5c92gwe8f3c!App' Joao.Boxedwine.DevMode_0.1.6.0_x64__8j5c92gwe8f3c ladder-native.log 'corewindow-swapchain PASS'`. Not run against `Grok.XboxTest` (the live shared launcher owned by lane `app`/`launcher`) — the `install`+`kill` steps would upgrade/downgrade and then terminate that package in place, which would have clobbered another lane's live session (QR login, `steam_session.bin`) rather than testing anything new. |

## ETW / crash dump findings (`logs.sh`)
- `/api/etw/providers`, `/api/etw/customproviders`, `/api/etw/session/realtime` all return
  **HTTP 404** on this console — Xbox Series S Dev Mode does not expose the Device Portal ETW
  REST/websocket API that exists on desktop/HoloLens/IoT Device Portal builds. Don't rely on it.
- `/api/debug/dump/usermode/dumps[?packageFullName=..]` and `.../crashcontrol?packageFullName=..`
  **do work** (HTTP 200): `dumps` lists real `.dmp` crash files already on the console (found one
  from an earlier Boxedwine crash, 78 MB), `crashcontrol` reads/sets per-package dump capture
  (`crashdumptype=true|false` via POST). `logs.sh list|enable|disable` wrap these.
- Could **not** find a working crash-dump download route — several `.../dumps/download?...`
  and path-based filename guesses all 404'd. If a dump needs pulling off later, check
  `ls-dir.sh`/`get-file.sh` against whatever folder the dump actually lives in, or revisit this.
- The practical, reliable log path remains an app's own on-disk log file (e.g.
  `ladder-native.log`) pulled via `get-file.sh`/`wait-log.sh` — `logs.sh get` is a thin alias for
  that so callers have one script name for "get me this app's logs".

## Other endpoint notes
- File APIs live under `/api/filesystem/apps/{file,files}` with `knownfolderid=LocalAppData` —
  the older-looking `/api/filemanager/*` path 404s on this OS build, don't use it.
- IDs: Device Portal wants `PackageRelativeId` (`Family!App`, e.g.
  `Joao.Boxedwine.DevMode_8j5c92gwe8f3c!App`) and `PackageFullName`
  (e.g. `Joao.Boxedwine.DevMode_0.1.6.0_x64__8j5c92gwe8f3c`) base64-encoded then URL-encoded for
  the taskmanager endpoints, but passed as plain URL-encoded strings (no base64) for the
  filesystem and packagemanager endpoints.

## Evidence
Screenshots/logs from this verification pass are under `/workspace/x64-run/runs/deploy/`
(not committed — console output, kept with the other run artifacts).

## Combined lane smoke runner

From the repository, `bash xbox-test/smoke-all.sh` checks off-device dispatch,
the Megabonk import map, and the installed `Joao.XboxSteam.Uwph` 0.1.5.0 app
(71 k32/ws checks plus 300 D3D frames). `--offline` skips console work.
Logs append to `/workspace/x64-run/runs/smoke-YYYY-MM-DD-HHMM.log`; each run
keeps its own evidence directory. A failed section makes the runner exit nonzero.
Only the existing Uwph test identity is stopped/launched; no package is installed.
The runner overwrites that identity's old k32.log while stopped before launching.

For multi-helper transactions, callers can open and flock `CONSOLE.lock`, then
export its inherited descriptor number as `DP_LOCK_FD`. `dp_lock` verifies the
fd points to `DP_LOCK` and acquires/reuses its lock; ordinary helper invocations
retain their existing per-call locking. Do not close/unlock the descriptor until
the transaction is finished.
