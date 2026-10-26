# BACKLOG (critical path: Megabonk first frame on Series S). Format: id | base | steps | acceptance
1. dl-megabonk | lane:app | after QR login works on console: own Steam CDN downloader (depot manifests + chunks, João's token) writing Megabonk into LocalState\megabonk; resume support | all 69 files present, sizes match manifest (dp ls-dir + sha check)
2. uwph | x64-spike | POSIX/syscall host inside UWP for Wine's unix side; in-process wineserver (no AF_UNIX, no child processes) | test app runs wine `cmd /c echo ok` (or a tiny PE) on console, output in LocalState log
3. wx | lane:uwph | W^X via VirtualProtectFromApp for Wine-loaded PE code (Wine's own loader) | tiny x64 PE prints from LocalState log on console; no RWX pages requested
4. d3d-pass | lane:uwph | Wine d3d11/dxgi -> native console D3D11 passthrough on the app's CoreWindow | D3D11 triangle PE renders on console (dp screenshot)
5. steam-map | x64-spike | static interface map of pinned steamclient64 build 1788652215 only (refuse on hash mismatch, block token-revoking calls) | map file + validation script passes against the pinned file
6. megabonk-boot | lane:d3d-pass | boot Megabonk -force-d3d11 to Unity splash | dp screenshot shows Unity splash

# Added 22:35 BRT 2026-10-25 by watcher, from LANES.md §4 (plan + deploy lanes DONE and merged into x64-spike)
7. uwph-spike-dispatch | x64-spike | RUNNING as lane sd | xbox-test/uwph/spike-dispatch/run.sh prints "SHIM-ANSWERED NtQuerySystemTime"; verdict in LANE_NOTES.md
8. uwph-vrx-spike | g/launcher | DONE lane vrx: RWX fails err=87; RW-to-RX and FromApp both return 42 | runs/vrx/vrx.log from console with one result per RWX/FromApp case; verdict in LANE_NOTES.md
9. uwph-kernel32-ntdll | lane:sd | (1) xbox-test/uwph/host/ backend: VirtualAllocFromApp/ProtectFromApp memory, real CreateThread, real CreateFileW, sync, CreateProcessW clean-fail (2) test PE calling ~30 kernel32 fns (3) run on console | test PE log on console shows correct file I/O, threads, exec memory results
10. uwph-wineserver-inprocess | x64-spike (k32 merged) | in-process server for handles/sync/thread bookkeeping (no AF_UNIX, no child processes) | test PE: two threads sync via CreateEventW+WaitForSingleObject+DuplicateHandle, logged on console
11. uwph-win32u-corewindow-driver | lane:uwph-wineserver-inprocess | Wine display driver mapping HWNDs onto the single CoreWindow | test PE RegisterClassExW+CreateWindowExW+message loop survives N frames, WM_SIZE/WM_ACTIVATE logged on console
12. uwph-dll-passthrough-map | lane:uwph-win32u-corewindow-driver | d3d11/dxgi/ws2_32/crypto → real system DLLs, user32/kernel32/ntdll → uwph builtins, rest → stub builtins | one process renders a D3D11 frame with real d3d11 while uwph user32 owns the window (dp screenshot)
13. uwph-loader-smoke-test | lane:uwph-dll-passthrough-map | run an unmodified small Win32 PE through the stack | gets past "fails to load", ideally a frame or clean exit, logged

# Watcher 23:10 BRT 2026-10-25
- item 7 uwph-spike-dispatch: DONE (lane sd, LANE_DONE 22:56), acceptance rerun OK, merged into x64-spike as 29ac7987. Worktree /workspace/wt/sd kept (base of item 9).
- item 9 uwph-kernel32-ntdll: queued as lane k32 (worktree /workspace/wt/k32, branch lane/k32), starts with codex at 23:21 (inherited LANE_DONE removed in c18b4cc6).
- item 8 vrx: still running (claude limited until 03:00, lane.sh retries codex after 23:20).
14. depot-verify-tool | lane:dl | (1) xbox-test/dp/verify-depot.sh: list LocalState\megabonk on console via dp ls-dir, compare names+sizes to the depot manifest JSON written by /workspace/steam-login/dl/dl.js (2) optional --sha mode pulls each file and sha1-checks | `bash xbox-test/dp/verify-depot.sh --local /workspace/steam-login/dl/<out>` reports 69/69 OK; console run lists present/missing counts
15. uwph-import-map | x64-spike | (1) tools/xbox/import_map.json classifying every DLL imported by Megabonk.exe, UnityPlayer.dll, GameAssembly.dll, baselib.dll as system|uwph|stub per LANES.md §1 (2) tools/xbox/check_import_map.py using llvm-readobj --coff-imports | `python3 tools/xbox/check_import_map.py /workspace/megabonk-from-console` exits 0 and prints 0 unclassified DLLs
16. steam-map (= item 5, still ready, independent) | x64-spike | as item 5 | as item 5

# Watcher 23:40 BRT 2026-10-25
- item 9 uwph-kernel32-ntdll: DONE (lane k32, 52/52 checks on Series S), merged into x64-spike (7e92d2d2). spike-dispatch smoke rerun: ALL OK.
- item 8 vrx: DONE; its 2 commits cherry-picked onto x64-spike (lane/vrx is based on g/launcher, so not merged wholesale).
- item 1 dl-megabonk: lane dl (launcher downloader) works on console for Spacewar 480 + redist 1007 (anon). BLOCKED for Megabonk: both stored refresh tokens rejected by CM (eresult 15); needs a fresh QR scan from João. Lane dl runner was dead since 23:02; not restarted until the QR scan.
- item 10: now running as lane ws (codex), base x64-spike.
- item 15: now running as lane imap (codex).
- item 14: now running as lane dv (codex), base lane/dl, read-only on console.
- Next ready: item 5/16 steam-map, item 11 win32u-corewindow (after ws).
17. dl-megabonk-after-qr | lane:dl | (1) João scans fresh QR on TV (2) launcher restores session by CM (3) A on Megabonk downloads 3405340/depot 3405342 into LocalState\megabonk | verify-depot.sh reports 69/69 OK on console

# Watcher 00:05 BRT 2026-10-26
- lanes w32u (item 11) and smap (item 5/16) had been idling since start: lane.sh read the prompt with a relative path after cd into the worktree, so the CLI got an empty prompt. Fixed lane.sh (absolute path + refuse empty prompt); both restarted with codex.
- item 10 ws DONE (71 PASS on Series S) and item 15 imap DONE (0 unclassified DLLs): both merged into x64-spike (fb9d512e, 3ec3c2ee); spike-dispatch smoke ALL OK; imap acceptance rerun exit 0. Worktree imap removed; ws kept (base of item 11).
- item 14 dv DONE on lane/dv (based on lane/dl, merges with dl).
18. uwph-wx-loader | x64-spike | RUNNING as lane wxl: PE mapper in uwph host (Wine-loader logic), relocs+imports, VirtualProtectFromApp RW->RX, tiny test PE | console log "WXL-OK" from PE entry, every protect logged, zero RWX, k32/ws checks still pass
19. d3d11-pe-coreWindow | x64-spike | RUNNING as lane d3dt: PE DLL in uwph host renders via real d3d11/dxgi on CoreWindow | runs/d3dt.png shows triangle, 300 frames hr=0 in LocalState log
20. megabonk-load-probe | lane:wxl | (1) point the wxl mapper at Megabonk.exe + UnityPlayer.dll from /workspace/megabonk-from-console (2) resolve imports per tools/xbox/import_map.json, stub class gets logging stubs (3) run on console | console log lists every import resolved or the first missing symbol per DLL; no crash before the entry point
21. smoke-all | x64-spike | (1) xbox-test/smoke-all.sh runs spike-dispatch run.sh, check_import_map.py, and (console, under lock) k32+ws test apps (2) writes runs/smoke-<date>.log | smoke-all.sh exits 0 with every section PASS

# Watcher 00:36 BRT 2026-10-26
- item 19 d3dt DONE (Series S: 300 native D3D11 frames, every Present S_OK, 53.9 fps, magenta clear-colour screenshot runs/d3dt.png; no triangle yet; first launch vanished once, cause unknown). Merged into x64-spike (f1dd429b). Smoke rerun: spike-dispatch ALL OK, import map 0 unclassified. Worktree d3dt removed.
- item 21 smoke-all: now RUNNING as lane smoke (claude, base x64-spike).
- lanes w32u (item 11), smap (item 5/16), wxl (item 18) running on codex since 00:27.
22. d3dt-triangle | x64-spike | (1) in xbox-test/uwph/host d3dt test PE add a vertex+pixel shader (precompiled DXBC via fxc-compatible bytecode checked into the tree, no runtime D3DCompile dependency) drawing one coloured triangle over the clear colour (2) deploy with the existing d3dt identity via dp scripts (3) dp screenshot | runs/d3dt-tri.png shows a triangle; LocalState log 300 frames hr=0; k32/ws 71 checks still PASS
23. d3dt-relaunch-stability | x64-spike | (1) script xbox-test/uwph/host/relaunch-3x.sh: kill, launch, wait-log for 300 frames, screenshot, 3 consecutive times (2) if any run vanishes, capture dp ps + LocalState log and record cause in LANE_NOTES.md | 3/3 runs reach 300 frames hr=0 with a non-black screenshot each (runs/d3dt-relaunch-{1,2,3}.png), or a documented root cause with evidence
24. uwph-hid-setupapi-stub | x64-spike | (1) uwph stub backends for SETUPAPI (SetupDiGetClassDevsA/W, SetupDiEnumDeviceInterfaces, SetupDiDestroyDeviceInfoList) and HID.DLL (14 symbols Megabonk imports, per tools/xbox/import_map.json) that enumerate zero devices and return documented failure codes (2) test PE calling each import | console log "HIDSTUB-OK" with one line per function and zero crashes; check_import_map.py still 0 unclassified

# Watcher 01:00 BRT 2026-10-26
- item 21 smoke DONE (smoke-all.sh: k32/ws 71 PASS + d3dt 300 frames on Series S, --offline OK); fast-forwarded into x64-spike (a1b14f26). Worktrees smoke and ws removed (merged, clean).
- item 18 wxl DONE (WXL-OK on Series S, zero RWX) but merge into x64-spike conflicts with d3dt in xbox-test/uwph/host/*; now RUNNING as lane wxm (codex) to resolve + rerun smoke + wxl on console.
- item 24 hid: now RUNNING as lane hid (codex, base x64-spike).
- item 1/17 dl still BLOCKED on a fresh QR scan from João (claude limited; runner dead since 23:02, not restarted until QR).
- item 20 megabonk-load-probe: base changes to lane:wxm (start after wxm DONE).
25. wxl-tls-unwind | lane:wxm | (1) extend wxl_mapper.h: TLS directory (callbacks + TLS index via TlsAlloc), RtlAddFunctionTable for .pdata unwind info, DllMain(DLL_PROCESS_ATTACH) call for mapped DLLs (2) test PE with __declspec(thread) var, a TLS callback, and a C++ throw/catch across a mapped DLL (3) run on Series S | console log WXL-TLS-OK: TLS value correct, callback ran, exception caught; zero RWX; smoke-all.sh exits 0

# Watcher 01:35 BRT 2026-10-26
- Box was paused ~01:06-01:35 (all codex rollouts have a 29-min gap; process clocks lag wall clock). After resume, codex for w32u and hid never got a reply (no output 01:06-01:38, codex 5h window at 61-65%, not limited): killed at 01:38, lane.sh restarted both on codex at 01:38:58. smap resumed by itself (progressing). Claude session-limited until 03:00 BRT.
- x64-spike a1b14f26 (smoke fast-forward) had re-committed LANE_DONE, so lane/hid inherited a FALSE LANE_DONE (the smoke lane's marker). Removed in lane/hid a0e089f8; hid is NOT done (console HID-STUB-OK still to do; off-device HID-STUB-OK symbols=20 checks=23 only). x64-spike now drops LANE_DONE again (e5db18bd). Merge rule: always drop LANE_DONE from x64-spike after a merge/ff.
- item 18 wxl / wxm merge: DONE, fast-forwarded into x64-spike (cc407d82) + e5db18bd drop LANE_DONE. Watcher rerun of `bash xbox-test/smoke-all.sh` on x64-spike: 7 PASS 0 FAIL ON the Series S (dispatch+import map off-device; fresh launch, k32+ws 71, d3dt 300 frames, wxl WXL-OK zero RWX, cleanup on console) runs/smoke-2026-10-26-0140.log. Note: the first rerun failed 3/4 console sections only because the main checkout had no xbox-test/uwph/host/out (smoke does not build); built with build.sh (same test.pfx as wxm) and reran.
- item 14 dv: DONE; acceptance rerun (verify-depot.sh --local megabonk 69/69 OK, unit tests OK). Deliberately not merged into x64-spike (based on lane/dl); fast-forwarded into lane/dl (9051b4f3, dv LANE_DONE dropped).
- item 8 vrx: its two commits are patch-equivalent on x64-spike (git cherry '-'); branch kept, worktree removed.
- item 24 hid: RUNNING (codex, restarted 01:38).  item 11 w32u: RUNNING (codex, restarted 01:38).  item 5/16 smap: RUNNING (codex).
- item 20 megabonk-load-probe: base now x64-spike (contains wxm); RUNNING as lane mlp (codex, 01:41), own identity Joao.XboxSteam.Mlp.
- item 25 wxl-tls-unwind: base now x64-spike (contains wxm); READY, prompt lanes/tls.prompt (identity Joao.XboxSteam.Tls).
- item 22 d3dt-triangle: READY, prompt lanes/d3tri.prompt.  item 23 d3dt-relaunch-stability: READY, prompt lanes/rl3.prompt.
- item 1/17 dl: still BLOCKED on a fresh QR scan from João.
- Started only 4 codex lanes: Claude limited until 03:00; codex 5h window 65% (resets 04:21 BRT), weekly 73% (resets 2026-11-01 03:18 BRT).
26. smoke-build-guard | x64-spike | (1) smoke-all.sh: before console sections, if xbox-test/uwph/host/out/layout is missing or older than any tracked xbox-test/uwph/host source, run build.sh (reuse existing out/test.pfx) and log it; fail the section (never skip) if build fails (2) record build commit hash in the smoke log | from a fresh `git worktree add` of x64-spike, `bash xbox-test/smoke-all.sh` exits 0 with 7 PASS without manual build; `bash xbox-test/smoke-all.sh --offline` exits 0
27. mlp-entry-stubs | lane:mlp (after mlp DONE + tls DONE) | (1) for every first-missing symbol listed by mlp, implement or route it (uwph builtin or system) per LANES.md §1 (2) rerun the probe until every Megabonk.exe/UnityPlayer.dll import resolves | console log MLP-PROBE-DONE shows 0 unresolved imports for both binaries; check_import_map.py exit 0; smoke-all.sh exit 0
28. mlp-unityplayer-dllmain | lane:mlp-entry-stubs | (1) with tls (item 25) merged, call UnityPlayer.dll TLS callbacks + DllMain(DLL_PROCESS_ATTACH) on the console under a vectored exception handler that logs the faulting RVA (2) log the first faulting import/RVA if it crashes | console log shows UnityPlayer DllMain returned TRUE, or a logged fault RVA + symbolised import; zero RWX; smoke-all.sh exit 0

# Watcher 02:00 BRT 2026-10-26
- item 5/16 smap DONE (pinned steamclient 1788652215 map, 340 entries validate, fail-closed validator; Megabonk reached main menu with no Steam client under Wine OFF-device only). Merged into x64-spike (363e3e5b, notes combined, LANE_DONE dropped); smoke-all --offline exit 0, import map 0 unclassified, validator self-test exit 0. Worktree smap removed.
- item 24 hid DONE on lane/hid (Series S HID-STUB-OK symbols=20 checks=23) but merge conflicts with wxm in xbox-test/uwph/host/*: RUNNING as lane hmg (codex) to merge + console smoke + hid rerun. Keep wt/hid until hmg DONE.
- item 25 tls: RUNNING as lane tls (codex, base x64-spike 363e3e5b).
- RUNNING: w32u (11), mlp (20), hmg (24 merge), tls (25), all codex. Claude limited until 03:00 BRT; at 03:00 start d3tri (22) and rl3 (23) on claude.
29. kit-sideload-bundle | x64-spike | (1) xbox-test/kit/build-kit.sh: build every current test package (uwph host identities d3dt/wxl/hid, k32, ws) plus the launcher from lane/dl if present, into /workspace/x64-run/kit/<date>/ with each signed .msix, the public test cert (.cer, never the .pfx private key), and a manifest.json (identity, version, sha256) (2) xbox-test/kit/README.md: exact on-device Dev Mode steps (Device Portal Add app, install order, what each app should show/log) (3) xbox-test/kit/verify-kit.sh checks every msix signature with osslsigncode and the manifest hashes | `bash xbox-test/kit/build-kit.sh && bash xbox-test/kit/verify-kit.sh` exits 0; kit dir contains no .pfx; smoke-all.sh --offline exits 0

# Watcher 02:35 BRT 2026-10-26
- Box paused again ~02:03-02:27. After resume codex for w32u and tls never replied (no output since 02:02): killed, lane.sh reran them. mlp was mid-upload (curl to console, holds CONSOLE.lock, -m 600 timeout) - left alone; hmg waiting on the lock.
- Codex 5h window 94% (resets 04:21 BRT); Claude resets 03:00.
- item 22 d3tri: now RUNNING as lane d3tri (claude primary; falls over / waits per lane.sh), base x64-spike 363e3e5b.
- item 23 rl3: READY but shares the d3dt identity with d3tri; start after d3tri DONE.
- item 26 smoke-build-guard: READY.
29. smoke-add-hid-tls | x64-spike (after hmg + tls merged) | (1) add HIDSTUB-OK (hid test PE) and WXL-TLS-OK (tls test PE) console sections to xbox-test/smoke-all.sh under the same fd-9 CONSOLE.lock transaction (2) rerun on Series S | `bash xbox-test/smoke-all.sh` exits 0 with 9 PASS 0 FAIL, log in runs/smoke-<date>.log; `--offline` exits 0
30. dl-token-reject-falls-back-to-qr | lane:dl | (1) in the launcher, when CM rejects a stored refresh token (eresult 15 / AccessDenied), clear only the rejected token, log it, and show the QR screen with the Refresh button focused (2) offline unit test feeding a canned eresult 15 into the session-restore path | unit test passes; launcher on console with the current (rejected) tokens lands on the QR screen within 15 s (dp screenshot runs/dl-qr-fallback.png); Logout still clears everything

# Watcher 03:02 BRT 2026-10-26
- Claude reset at 03:00: all 5 lanes (w32u, mlp, hmg, tls, d3tri) restarted on claude at 03:02 after codex hit its 5h limit (codex resets 04:21 BRT).
- No new DONE since 02:35. hid DONE but unmerged (hmg running the merge); dv DONE, merged into lane/dl only (kept by design).
- READY: 26 smoke-build-guard, 29 kit-sideload-bundle, 29 smoke-add-hid-tls (after hmg+tls), 30 dl-token-reject-falls-back-to-qr; 23 rl3 after d3tri. Disk 107 GB free; nothing eligible for cleanup.

# Watcher 03:30 BRT 2026-10-26
- item 11/12 w32u DONE (claude, 03:06): merged into x64-spike (bffb6a51, LANE_DONE dropped, LANE_NOTES combined); full smoke-all.sh rerun started (runs/watcher-0330-smoke.out); worktree removed, branch kept.
- RUNNING on claude: mlp (20), hmg (24 merge), tls (25), d3tri (22). Codex limited until 04:21 BRT.
- item 29 kit-sideload-bundle: RUNNING as lane kit (claude primary, worktree /workspace/wt/kit, branch lane/kit) from 03:31.
- READY: 26 smoke-build-guard, 29 smoke-add-hid-tls (after hmg+tls), 30 dl-token-reject-falls-back-to-qr; 23 rl3 after d3tri. Disk 107 GB free.
31. backup-restore-drill | x64-spike | (1) xbox-test/tools/restore-drill.sh: clone every bundle written by x64-run/backup.sh into a fresh /tmp dir, check x64-spike and every lane/* head SHA matches the live repo (2) report missing/stale bundles | script exits 0 with "RESTORE-DRILL OK branches=N" and exits non-zero if any branch head differs; run once and save runs/restore-drill-<date>.log
32. kit-console-install-check | lane:kit (after kit DONE) | (1) xbox-test/kit/install-kit-console.sh installs the kit .msix files on the Series S via dp scripts under CONSOLE.lock in the README install order, skipping identities already installed at the same version (2) launches each and waits for its OK marker in LocalState | every kit app logs its OK marker on console; log runs/kit-install-<date>.log; no other lane's package uninstalled

# Watcher 04:00 BRT 2026-10-26
- No new DONE since w32u (03:06, merged). Running on claude: mlp (20, rejected preliminary console run, corrected delay-import mapper, redeploying), hmg (24), tls (25), d3tri (22, installing), kit (29). Smoke rerun from 03:30 still in progress (console sections waiting on CONSOLE.lock).
- item 30 dl-token-reject-falls-back-to-qr: STARTED as lane dlqr in /workspace/wt/dl (codex primary, keeps dl's uncommitted launcher edits), 04:01.
- Added items 31 backup-restore-drill (READY) and 32 kit-console-install-check (after kit).
- READY: 26 smoke-build-guard, 31 backup-restore-drill; 23 rl3 after d3tri; 29 smoke-add-hid-tls after hmg+tls; 32 after kit. Box RAM tight (~2 GB free with 5 claude sessions). Disk 107 GB free; nothing eligible for cleanup (hid/dv not merged into x64-spike).

# Watcher 04:30 BRT 2026-10-26
- CONSOLE OFFLINE since 03:29: box dropped from tailnet (404 node not found); saved TS_AUTHKEY rejected as invalid on re-join. See BLOCKED-console-offline.md. Needs a new key from João (report in the 08:00 morning summary; quiet hours now).
- 03:30 smoke: 2 passed (off-device dispatch, import map), 5 failed — all console sections, consistent with the outage (not a code regression).
- Running: mlp (20, codex), hmg (24), tls (25), d3tri (22), kit (29), dlqr (30, codex). No new DONE. Console-touching steps in these lanes will fail until the key is fixed; lanes should finish off-device parts and leave console acceptance pending.
- 33. smoke-console-preflight | x64-spike | (1) smoke-all.sh: before console sections, probe DP root through localhost:1055 with 20 s timeout; if unreachable, mark each console section "FAIL: console unreachable (network)" with one line, no Python tracebacks; do NOT count as PASS or SKIP | `bash xbox-test/smoke-all.sh` with console offline prints that line for each console section and exits non-zero; with console online behaviour is unchanged | READY (off-device)
- READY: 26 smoke-build-guard, 31 backup-restore-drill, 33 smoke-console-preflight; 23 rl3 after d3tri; 29 smoke-add-hid-tls after hmg+tls; 32 after kit AND console back. RAM ~2 GB free, not starting more lanes now. Disk 107 GB free; nothing eligible for cleanup.

# Watcher 05:00 BRT 2026-10-26
- Console still OFFLINE (tailnet node dropped 03:29, TS_AUTHKEY invalid); needs João's new key, goes in the 08:00 morning summary.
- Running: d3tri (22, claude), mlp (20, claude), tls (25, claude), kit (29, claude), hmg (24, claude), dlqr (30, codex). All active (<35 min since last agent start). No new DONE since w32u. Nothing eligible for worktree cleanup. Disk 107 GB free, RAM ~2 GB free: not starting more lanes.
- 34. kit-offline-verify | lane:kit (after kit DONE) | (1) xbox-test/kit/verify-kit-offline.sh: for every .msix in the kit, unpack with makemsix to /tmp, check AppxManifest.xml parses (xmllint --noout), Identity Publisher matches the signing cert subject, and every file listed in AppxBlockMap.xml exists (2) exit non-zero with the failing file named | `bash xbox-test/kit/verify-kit-offline.sh && echo OK` prints OK; corrupting one byte of a copied .msix makes it fail
- READY: 26 smoke-build-guard, 31 backup-restore-drill, 33 smoke-console-preflight; 34 after kit; 23 rl3 after d3tri; 29 smoke-add-hid-tls after hmg+tls; 32 after kit AND console back.
- 35. kit-install-guide | lane:kit (after kit DONE) | (1) xbox-test/kit/INSTALL.md: numbered on-device steps for João (Dev Home/Device Portal sideload of each .msix + cert, launch order, what success looks like on the TV, how to collect logs back) using only paths/scripts that exist in the kit (2) xbox-test/kit/check-install-guide.sh: extracts every backticked path/script from INSTALL.md and fails naming any that does not exist | `bash xbox-test/kit/check-install-guide.sh` exits 0; `grep -c '^[0-9]\+\.' xbox-test/kit/INSTALL.md` >= 5 | READY after kit (off-device)
### watcher 05:35
- READY: 26 smoke-build-guard, 31 backup-restore-drill, 33 smoke-console-preflight; 34+35 after kit; 23 rl3 after d3tri; 29 after hmg+tls; 32 after kit AND console back. Disk 107 GB free; nothing eligible for cleanup.
- tls lane: one Claude safeguard flag at 04:34 (cybersecurity classifier); lane retried the same prompt unchanged in a new session (05:15, running). If it is flagged again, write REFUSED-tls and report; do not reword.
- 05:45 BRT: Tailscale rejoined with new TS_AUTHKEY, Device Portal reachable again (401 = up).

# 05:55 BRT 2026-10-26 refocus (João asked to refocus on what is actually needed) - see PLAN-0600.md
- DEFERRED: 23, 26, 29b, 31, 32, 33, 34, 35. Do not start lanes for these.
- Critical path: mlp (20) + tls (25) -> Unity first frame -> Steam host -> audio/controller.
36. launcher-grid-scroll-fix | lane:dl (after dlqr DONE) | João reports on the console that the library grid flips to another page by itself about once per second without input and does not scroll. In xbox-test/app/main.cpp find why m_top/m_sel changes with no input (log every m_top/m_sel change with cause: input, resort, list reload, cover load), fix it; make D-pad scroll one row at a time smoothly and keep the selected row in view | boot.log on console shows no m_top changes for 30 s with no input; D-pad Down scrolls one row; screenshot

# Watcher 06:10 BRT 2026-10-26
- item 24 hid/hmg DONE (Series S HID-STUB-OK + merged smoke): merged into x64-spike (dadc79c4, LANE_DONE dropped). Worktrees hid, hmg removed (branches kept).
- item 29 kit DONE: merged into x64-spike (1e960d9d, LANE_DONE dropped). Worktree kit kept (base of deferred 32/34/35).
- item 22 d3tri DONE (native triangle on Xbox, 300 presents) but merge conflicts with hmg in smoke-all.sh + uwph host manifest: RUNNING as lane d3m (codex, wt/d3m from x64-spike a938e235) to merge + full smoke on console + kit rebuild. Keep wt/d3tri until d3m DONE.
- Running: mlp (20, codex), tls (25, codex), dlqr (30, codex), d3m. Claude limited. Console back online since 05:45. Per PLAN-0600 refocus no deferred items started. Disk 107 GB free.

# Watcher 06:30 BRT 2026-10-26
- item 30 dlqr: lane loop STOPPED 06:29 (codex re-verified every 2-5 min since 05:03 without progress). Code + offline test DONE and committed on lane/dl (426eb8f9, launcher 0.4.4.0 deployed, session-restore.sh PASS). Console rejection proof PENDING: the stored token is now ACCEPTED (runs/dlqr/boot-current.log eresult=1, restored 2.29 s), so the "rejected token -> QR in 15 s" gate cannot run. NOT marked done, gate unchanged; checked opportunistically when a token is genuinely rejected; no fake/injected rejection counts. See BLOCKED-dlqr.md.
- item 22 d3tri via d3m: DONE, fast-forwarded into x64-spike (18f6c58c) + 1a7fc999 drop LANE_DONE.
- item 20 mlp: DONE, merged into x64-spike (c1b19fe3; conflicts resolved by watcher: build.sh keeps hid+mlp modes, smoke-all keeps mlp's locked newest-installed Uwph selection; notes combined; LANE_DONE dropped). 004c6b39: mlp_routes.h regenerated by build.sh from the merged import map (ntdll, win32u routes).
- item 25 tls: DONE on lane/tls but merge conflicts non-trivially (smoke-all retargeted to Tls identity, build.sh lane modes, main.cpp, wxl_mapper.h): RUNNING as merge lane tlm (codex, wt/tlm from x64-spike c1b19fe3). Keep wt/tls until tlm DONE.
- Smoke on x64-spike 004c6b39-equivalent (after build.sh): 8 PASS 0 FAIL on the Series S (dispatch, import map, fresh launch, k32+ws 71, d3dt triangle 300 frames hr=0, wxl W^X, hid 20/23, cleanup); log runs/smoke-2026-10-26-0630.log (runner output runs/watcher-0630-smoke.out).
- item 27 mlp-entry-stubs: RUNNING as lane stubs (codex, wt/stubs, branch lane/stubs from x64-spike c1b19fe3; merges lane/tlm when tlm has LANE_DONE). Input: mlp's missing-symbol lists (kernel32 213 + user32 111 MISSING; gdi32/hid/imm32/opengl32/setupapi/shell32/shlwapi/winmm STUB).
- item 36 launcher-grid-scroll-fix: RUNNING as lane grid (codex, wt/dl, branch lane/dl), prompt lanes/grid.prompt.
- item 17 dl-megabonk-after-qr: UNBLOCKED (the saved login is valid now, QR scan no longer needed). READY on lane/dl right after grid (same worktree + launcher identity Grok.XboxTest); acceptance unchanged (real download by the launcher, verify-depot.sh 69/69 OK on console).
37. hwnd-swapchain-corewindow | x64-spike | (PLAN B prep) (1) small test PE loaded through the wxl mapper that does what Unity does: RegisterClassExW+CreateWindowExW (w32u CoreWindow driver), D3D11CreateDevice + IDXGIFactory2::CreateSwapChainForHwnd (and CreateSwapChain/DXGI_SWAP_CHAIN_DESC fallback) on that HWND; uwph dxgi route maps the HWND swap chain onto CreateSwapChainForCoreWindow (2) clear to a colour, Present 300 frames (3) run on console under CONSOLE.lock, own identity Joao.XboxSteam.Swc | console log 300 Presents hr=0 from the mapped PE through the HWND path, dp screenshot runs/swc/swc.png shows the colour; zero RWX; smoke-all.sh exit 0 | READY
38. shared-game-dir | x64-spike + lane/dl | (PLAN A/B prep: the host must read the files the launcher really downloads; testing is via real launcher downloads) (1) use a documented UWP mechanism so the uwph host can read the launcher's Megabonk install: publisher cache folder (windows.publisherCacheFolders; launcher Grok.XboxTest and Joao.XboxSteam.* share Publisher CN=grok-xbox-dev) or host inside the launcher package; record the choice in LANE_NOTES (2) launcher writes a test file into the shared folder, Mlp-style host reads and logs it | on console the host logs the launcher-written file's sha256 matching; both packages installed side by side; no other identity changed | READY (independent of 17; after 17 switch the launcher's download target to the shared folder)
39. unity-registry-playerprefs | x64-spike | (PLAN A: Unity reads PlayerPrefs from HKCU\Software\Ved\Megabonk at startup; registry is restricted in AppContainer) (1) test PE through the wxl mapper calling exactly the advapi32 Reg* imports UnityPlayer.dll uses (list from llvm-readobj) on HKCU\Software\Ved\Megabonk: create/set/query/enum/delete (2) if the console refuses any, route those imports to a uwph builtin backed by LocalState\registry.json (same signatures, documented error codes) and update tools/xbox/import_map.json | console log one line per Reg* call with result, all succeed (natively or via builtin), REG-OK; check_import_map.py exit 0; smoke-all.sh exit 0 | READY
- READY (critical path): 17 dl-megabonk (after grid on lane/dl), 37 hwnd-swapchain-corewindow, 38 shared-game-dir, 39 unity-registry-playerprefs. Next after stubs: 28 mlp-unityplayer-dllmain, then Unity first Win32/D3D11 calls (PLAN A acceptance) and swap chain/splash (B, uses 37). Not started now: 3 codex lanes already running (tlm, stubs, grid). DEFERRED items untouched.
- Cleanup: removed worktrees d3m, d3tri, mlp (LANE_DONE, merged, clean, no open files; branches kept). Kept tls (unmerged), kit (base of deferred), dl (grid), dv/app/g-* (unmerged or no LANE_DONE), g-dp (merged but no LANE_DONE). disk-guard.sh and backup-loop.sh running. Disk 107 GB free, RAM 12 GB available.
# Watcher 07:00 BRT 2026-10-26
- Running (codex, all active, console uploads/installs in progress): tlm (25 merge), stubs (27), grid (36). Claude session-limited until 08:00 BRT. Box RAM ~2 GB available, so no 4th lane now.
- Prepared lanes/swc.prompt for item 37 (worktree wt/swc from x64-spike, identity Joao.XboxSteam.Swc). Start it on claude at/after 08:00 (`./lane.sh swc /workspace/wt/swc lanes/swc.prompt claude` after `git worktree add -b lane/swc /workspace/wt/swc x64-spike`), then 38/39 as RAM allows.
- No new DONE since 06:30; no cleanup candidates (tls unmerged, kit base of deferred, dv/app/g-* kept). Disk 107 GB free.
- 07:30 grid lane (codex) stopped by João's request; Grok executor finishes console validation in /workspace/wt/dl

# Watcher 07:36 BRT 2026-10-26
- Codex usage-limited since ~07:10 ("try again at 9:01 AM"). tlm and stubs lane.sh loops alive, retrying codex every 5 min (cheap, rc=1); no Claude fallback (ALLOW_CLAUDE absent). grid stopped by João; Grok executor handles console validation in wt/dl.
- 08:00 summary must tell João: Codex limited until ~09:01 BRT, so tlm/stubs are paused waiting; he can use one of his 2 Codex resets if he wants them moving sooner. swc (item 37) moves to codex per 07:06 override, start after codex returns.
- No new DONE/BLOCKED/REFUSED; no cleanup eligible (dv/gkit/gtools unmerged, g-dp no LANE_DONE, tls base of tlm, kit base of deferred, dl in use). Disk 107 GB free; disk-guard running.

# Watcher 08:07 BRT 2026-10-26
- 36 launcher-grid-scroll-fix: LANE_DONE 07:34 on lane/dl (launcher 0.4.6.0 on console; 64 s idle zero grid changes; synthetic one-shot Down scrolls one row; real-controller press not yet verified by João). lane/dl stays the launcher line (not merged into x64-spike).
- 37 hwnd-swapchain-corewindow: RUNNING as lane swc (wt/swc, branch lane/swc from x64-spike 004c6b39), codex.
- tlm (25 merge) and stubs (27): restarted on codex-only lane.sh (old loops had fallen over to Claude at 08:01; stopped).

# Note for later lanes (Codex) from gdet 2026-10-26
- TEST WITH `xbox-test/tools/play-test.sh 3405340` ("Play Megabonk via the launcher"); it is the standard end-to-end Megabonk progress check. Replace the probe step in launch.h start_launch() when a real loader/host path exists, keep the `launch: RESULT` line + "Launch attempted:" on screen. Never leave synthetic input running; cmd.txt is one-shot.

# Research pointer 2026-10-26 (Grok executor, Wine-as-base)
- Report: research/WINE-BASE-REPORT.md; data: research/wine-coverage/ (megabonk-imports-vs-wine.csv, summary-by-dll.csv, required-syscalls.csv, wine-dll-table.csv, analyze.py, dll-table.py).
- Verdict: hybrid. Do NOT run Wine's user32/win32u/ntdll (unix side + wineserver ~200K lines). Use Wine as spec/checklist + PE-only donor. Megabonk imports: 35% pure PE, 38% Nt*-only, 27% need win32u; of the 188 console gap, 56% need win32u, so user32 on CoreWindow is the keystone.
- 32-bit: first a console spike for native compat mode (ljmp 0x23, FS/TEB32 unknown); fallback x86 engine with API-boundary break-out. D3D9 is NOT available in UWP: gates Spacewar/LIMBO/Undertale.
- Proposed new items 40-49 (see report section D): wine-gap-tool, kernel32-nt-tail, user32-pure-and-class-core, user32-message-pump-corewindow, user32-input-cursor-focus, donor-wine-pe, x86-compat-mode-spike, pe32-mapper-and-gap, d3d9-layer-decision, x86-engine-or-native-32. Not scheduled; João/watcher decides.

# Queued 2026-10-26 08:55 BRT by João's request (Wine-base research items 40-49)
- RULE for every lane on these items: FIRST read research/WINE-BASE-REPORT.md and research/wine-coverage/*.csv and review them critically (check claims against the actual Wine source in /workspace/research/wine and against the console; write corrections into LANE_NOTES.md and the report's section if wrong). Use Wine's *.spec files and behaviour as the spec. Test via xbox-test/tools/play-test.sh <appid> (launcher on lane/dl+gdet, branch lane/gdet) where the acceptance says so. Codex only.
- Start order (max 3 concurrent codex lanes; start next when a slot frees): 40 wine-gap-tool, 46 x86-compat-mode-spike (console, own identity Joao.XboxSteam.Cs32), 41 kernel32-nt-tail, then 42, 43 (after 37 swc), 45, 47, 44, 49 (after 46), 48 last.
| 40 | **wine-gap-tool** | Productise `research/wine-coverage/analyze.py` as `tools/xbox/wine_gap.py`: input = any game dir (PE list) + `import_map.json` → CSV (route, wine class, needed Nt*/NtUser*) + one summary line; run on Megabonk, Spacewar, LIMBO, Undertale; wire the summary into `launch_probe` output | Offline: `wine_gap.py` exit 0 and CSV for 3405340 equals `megabonk-imports-vs-wine.csv` ±0 rows; then `play-test.sh 3405340` prints a `GAP: total=… missing=… win32u=…` line matching the CSV; the same for `play-test.sh 480` |
| 41 | **kernel32-nt-tail** | Implement the 10 kernel32 MISSING imports (Toolhelp32, CreateWaitableTimerA, GetComputerNameW, GetStartupInfoA, GetSystemPowerStatus, SetDllDirectoryW, SetThreadAffinityMask, VerifyVersionInfoW) and the other PE_PURE/NT-only symbols (≈82) using uwph/system calls, following Wine's behaviour | `play-test.sh 3405340` launch log shows `kernel32 missing=0`; smoke-all.sh exit 0 |
| 42 | **user32-pure-and-class-core** | user32 PE_PURE 20 symbols + class/atom/window-property table in the host (no CoreWindow yet; windows are records) | `play-test.sh 3405340`: `user32 missing` drops from 111 to ≤ 91; a test PE doing RegisterClassExW/CreateWindowExW/GetWindowLongPtr round-trips (log lines) |
| 43 | **user32-message-pump-corewindow** (builds on swc item 37) | Message queue (GetMessage/PeekMessage/PostMessage/SendMessage/Translate/Dispatch, timers, WM_PAINT/SIZE/ACTIVATE/CLOSE) driven by CoreWindow dispatcher events | `play-test.sh 3405340`: UnityPlayer reaches its first `GetMessage/PeekMessage` and log shows ≥300 pump iterations without the app being killed; screenshot (dp) shows non-black swap chain |
| 44 | **user32-input-cursor-focus** | GetAsyncKeyState/GetKeyState, cursor clip/capture/visibility, focus/foreground, monitor/DisplayConfig metrics, raw input registration → CoreWindow keyboard/pointer/gamepad | `play-test.sh 3405340` log: Unity input init completes, controller button press (one-shot `cmd.txt`) arrives as WM_/XInput event; no synthetic input left running |
| 45 | **donor-wine-pe** | Build/vendor Wine PE-only DLLs that need only Nt*: hid, setupapi, version, shcore, avrt; route in import_map as `wine-pe`; prove the loader runs them with the mapper | smoke-all.sh exit 0; `play-test.sh 3405340`: `hid/setupapi/winmm STUB` count from 67 → ≤ 40 |
| 46 | **x86-compat-mode-spike** (32-bit, console) | Throwaway identity `Joao.XboxSteam.Cs32`: low alloc via `VirtualAllocFromApp`, `ljmp 0x23` round-trip, WoW64 probes, selector dump, FS strategy test | One console log with `CS32-OK` or the exact exception code/selector values; written conclusion in LANE_NOTES: native-32 viable yes/no. Does not need play-test (no game) — gate = `smoke-all.sh` still exit 0 |
| 47 | **pe32-mapper-and-gap** | wxl maps a PE32 (relocs, imports classified by wine_gap, no execution); i386 import list per game | Offline test maps Spacewar and LIMBO PE32 with 0 reloc errors; `play-test.sh 480` and `play-test.sh 48000` print `PE32 mapped, imports=N, gap=M` and the `launch: RESULT` line |
| 48 | **d3d9-layer-decision** | 1-week-sized design+spike: a minimal D3D9→D3D11 path for SM3/fixed-function (own shim vs ports), plus a D3D9 triangle test exe through it | Test exe draws a D3D9 triangle on the console (300 presents, hr=0); `play-test.sh 480` log reaches `Direct3DCreate9` → `CreateDevice` OK |
| 49 | **x86-engine-or-native-32** (after 46) | Either (a) native 32→64 thunk layer from Wine's wow64 tables or (b) Boxedwine CPU core as library behind the thunks; run a hello 32-bit console exe | `play-test.sh 480` shows the 32-bit entry point reached and returns from `main`/`WinMain` with API calls logged; no RWX |

# Appended 2026-10-26 09:3x BRT: D3D layers research (refines item 48)
- Read research/D3D-LAYERS-REPORT.md. Finding: no "DXVK->D3D11"; best reuse = Wine d3d9+wined3d(GL) on Mesa GLon12, which already runs in Xbox UWP (aerisarn/mesa-uwp). DXVK+dzn blocked, dgVoodoo2 out (closed, RWX-packed), D3D9On12 = donor only (ShaderConverter).
- Item 48 becomes two lanes (replaces "own shim vs ports" decision; keep own shim as fallback option):
| 48a | **glon12-uwp-dll** | Build/rebase mesa-uwp GLon12 (non-ICD GL, no window hooks) as DLL for the loader; wgl glue presents to CoreWindow (lane swc); record how DXIL gets validated/signed | Test exe `gltri.exe` via `play-test.sh <gltri-appid>`: GL_RENDERER shows D3D12, 300 presents, hr=0, no RWX denial |
| 48b | **d3d9-wined3d-pe** (after 48a step 3) | mingw-build wine-10 d3d9/wined3d/vkd3d-shader PE; route opengl32/gdi32 HDC/window imports to 48a glue + items 42/43; D3D9 triangle (FFP + SM3 shader) | `play-test.sh <d3d9tri-appid>`: Direct3DCreate9 -> CreateDevice hr=0 -> 300 Present hr=0; later `play-test.sh 480` (after 47/49 + 32-bit D3D9 boundary thunk) reaches CreateDevice OK + Present; then 48000, 391540 |
- 48c (gate, only if 48b perf/hitches fail): wined3d D3D11 backend using ShaderConverter/dx9asm donors.
- 48d (cheap probe): on console confirm D3DCompile works in UWP and D3D12 accepts DXIL.

# Queued 2026-10-26 09:25 BRT (João: queue approved research follow-ups without asking)
- Items 48a (GLon12/Mesa OpenGL-over-D3D12 DLL + GL triangle) and 48b (Wine d3d9/wined3d on it: D3D9 triangle, then play-test 480, 48000, 391540) from research/D3D-LAYERS-REPORT.md are APPROVED, Codex only, same per-lane rule (review report critically first). Order: after 40/46/41 start; 48a before 48b. If DXIL validation/signing fails on console in 48a, stop and record the exact failure, fallback = wined3d D3D11 backend (new item).
- STANDING RULE from João: approved research follow-ups are added to the queue directly, not asked about.

# Status 2026-10-26 09:45 BRT (watcher)
- DONE + merged into x64-spike (d8b9326b): item 27 mlp-entry-stubs (lane stubs: MLP 0.1.32.0 resolves all 707 imports, zero RWX, smoke 9 PASS) and item 37 hwnd-swapchain-corewindow (lane swc: guest HWND + legacy swap chains present 300 frames S_OK to CoreWindow, green screenshot). Item 43 is now unblocked (base x64-spike, which includes swc).
- RUNNING (codex): 40 wgap, 46 cs32, 41 k32t. Next when a slot frees: 42, 43, 45, 47, 44, 48a, 48b, 49 (after 46).

# Status 2026-10-26 10:05 BRT (watcher)
- DONE + merged into x64-spike (ff da79cd20, then 8231a299 drops the tracked LANE_DONE so new lanes don't inherit it): item 46 x86-compat-mode-spike. Result: native-32 viable = NO for the documented-UWP direct 0x23 transition (0xc0000005 before any x86 instruction; wow64*.dll LoadPackagedLibrary error 126). Smoke 9/9 PASS on the identical tree (runs/cs32/smoke-2026-10-26-0944.log).
- Item 49 consequence: option (a) native 32->64 thunks via compat mode is ruled out on this path; 49 must take option (b) (32-bit CPU engine, e.g. Boxedwine's CPU core as a library behind Wine wow64-style thunks, no RWX: use the VirtualAllocFromApp/VirtualProtectFromApp JIT pattern or an interpreter). 49 is now unblocked.
- wgap (40): lane found the historical megabonk CSV omits 237 import pairs (628 real vs 391); watcher authorised correcting the oracle per the lane rule (old file archived, gate stays exact equality), see lanes/wgap.prompt.
- RUNNING (codex): 40 wgap, 41 k32t, 42 u32c (new, lane/gdet + x64-spike 8231a299). Next: 43, 45, 47, 44, 48a, 48b, 49.

# Status 2026-10-26 14:40 BRT (watcher)
- DONE + merged into x64-spike 86960ae8 (smoke 9/9 PASS after each merge, runs/smoke-2026-10-26-1432.log and -1435.log): item 40 wine-gap-tool (wine_gap.py; corrected Megabonk oracle 628 imports/458/120, Spacewar 136/107/18; 7 tests pass), item 41 kernel32-nt-tail (Megabonk Play: 383 kernel32 imports, 0 missing, 59 behavioural checks), item 42 user32-pure-and-class-core (user32 121 total, 83 missing, class/window round-trip PE returns 42). lane/gdet launcher line is now in x64-spike (conflict-free via u32c); launcher manifest 0.5.43.0.
- RUNNING (codex): 43 upmp, 45 dwpe, 47 pe32 (all from x64-spike 86960ae8). Next when a slot frees: 44, 48a, 48b (after 48a step 3), 49 (option b, 32-bit CPU engine).

# 2026-10-26 14:45 João: RENDERING FIRST. 48a (lane gl12, started 14:45, 4th lane) and then 48b move ahead of 44 and 49. Next free slot goes to 48b as soon as 48a step 3 passes (or earlier if wined3d d3d9 PE build can start in parallel offline).
50. registry-sqlite-backend | x64-spike (after 39/ureg merged) | João's idea: back the uwph registry builtin with SQLite instead of registry.json (transactional, crash-safe, inspectable; public-domain amalgamation built as PE, or winsqlite3.dll from the OS if available to UWP, whichever works). Keep exactly the same Reg* API semantics/error codes; one table (key path, value name, type, data), WAL/atomic commit, one-time import of existing registry.json. | Reg test lane still prints REG-OK calls=25 failures=0 twice (relaunch persistence) on console; kill-mid-write test leaves a consistent db; smoke-all.sh exit 0 | READY (low priority, after rendering items)
51. d3d11-badge | launcher | João 14:43: show a "D3D11" support badge on apps in the grid and detail screen. Detect from the game's PE imports (d3d11.dll/dxgi.dll present => D3D11; d3d9/d3d8/ddraw/opengl32 only => label "D3D9"/"Legacy", unsupported for now) once installed; before install, use Steam store metadata if available, else "unknown". | Launcher shows badge on console for installed Megabonk (D3D11) and a D3D9 game (e.g. LIMBO); smoke-all.sh 9/9 | RUNNING with Grok (14:45)
SCHEDULING RULE (João 14:48): Codex lanes have console priority; Grok tasks yield CONSOLE.lock when a Codex lane needs it. Never leave a lane idle waiting on the console: when it is busy, every lane must have offline/local-only work (builds, unit tests, host-side tools, research, code review) to do instead. Watcher: keep offline-capable items allocated at all times.
52. play-runs-guest | x64-spike | TOP PRIORITY (found by upmp 14:50): Play only MAPS PEs and never executes guest code (gameCodeExecuted=false). Wire a full-game loader: dependency-order load, PEB/TEB, TLS callbacks, unwind registration, DllMain, exe entry on a guest thread, with precise result reporting; iterate play-test.sh 3405340 fixing the first blocker each time until Unity reaches window/D3D11/GetMessage. | play-test.sh 3405340 reports gameCodeExecuted=true with the exact last event | RUNNING lane prun (14:52). Item 43 (upmp) is BLOCKED until 52 reaches Unity entry.
53. missing-windows-calls | x64-spike | João 14:54: Claude lane u32m (lane-claude.sh, Claude-only, explicit João approval) implements input/cursor/focus (item 44) + remaining non-pump user32/gdi32/shell32/shlwapi/imm32/winmm calls from wine_gap output. | Megabonk missing count drops per batch; smoke 9/9 | RUNNING (14:56)

# 2026-10-26 (Grok executor): research/X86-32BIT-PLAN.md written (item 49 scope). Engine = Boxedwine normal core as library (plan B Unicorn-TCI, JIT later via W^X); API-level thunks generated from Wine .spec; M0 hello32 ~2-3 wk, Spacewar CPU-side ~7 wk, on screen ~11 wk, all 3 games need D3D9 (48b); recommend not in Megabonk beta critical path, only an offline M0 side lane after 52.

# Status 2026-10-26 15:05 BRT (watcher)
- DONE + merged into x64-spike 75b719bc: item 47 pe32-mapper-and-gap (Spacewar 7887 relocs verified, LIMBO 0 relocs/0 errors; Play prints `PE32 mapped, imports=N, gap=M`; maps only, no 32-bit execution). Full smoke rerun after merge: runs/smoke-2026-10-26-1505.log.
- Item 43 (upmp) PAUSED: lane kept re-running only to re-confirm its blocker (needs item 52). Restart it when prun reports gameCodeExecuted=true and Unity reaches its pump.
- Item 44 is covered by Claude lane u32m (item 53). Freed Codex slot -> item 49 lane x86e, scoped to M0 (Boxedwine normal core as lib + hello32) per research/X86-32BIT-PLAN.md, offline-first.
- RUNNING: codex prun (52), dwpe (45), x86e (49); claude u32m (53); Grok regsql (50), d11badge (51). Deferred: 48a/48b/48c/48d (D3D9).
# 2026-10-26 15:06 João: 32-bit target = Spacewar (480) ONLY, and only AFTER Megabonk is playable. Lane x86e (item 49 M0) stopped, branch lane/x86e kept. Do not start any 32-bit/x86 or D3D9 work until João says Megabonk is playable. 47 pe32 (done) stays merged.
54. megabonk-steam-api-path | x64-spike | legit Steam API path for steam_api64 (research/STEAM-API-PATH.md + smallest SteamAPI_Init slice) | RUNNING lane stm (15:12, Codex, offline first)
55. audio-xaudio2 | x64-spike | route Megabonk audio imports to real UWP XAudio2/WASAPI; test PE with SamplesPlayed counters | RUNNING lane aud (15:12, Codex, offline first)
# PLAYABLE.md defines "Megabonk playable"; 32-bit(Spacewar)/D3D9 start automatically when its 6 criteria are evidenced (João 15:09: proof, not announcement).
CONSOLE RULE (15:10, from prun): lanes sharing the Grok.XboxTest identity collide on version numbers (Device Portal refuses a lower version: -2147009274). Hold CONSOLE.lock across build-version check + install + test (check the installed version first, choose a higher one, use errexit so an install failure never tests another lane's package); prefer a lane-specific identity (e.g. Joao.XboxSteam.<Lane>) for experiments.
