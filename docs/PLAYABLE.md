# Definition of "Megabonk playable" (proved by tests, not announced) — João 15:09
All proven by `src/xbox-test/tools/play-test.sh 3405340` evidence (log + screenshots/video) on the Series S:
1. Play runs Megabonk's code (gameCodeExecuted=true), Unity reaches its window and D3D11 device, no crash.
2. Main menu visible via screenshot, stable for >= 5 min.
3. Controller input (real pad, or one-shot synthetic labelled) moves through the menu and starts a run.
4. Audio: XAudio2 output has non-zero samples.
5. Steam API calls succeed through the legitimate real-client path (no DRM bypass).
6. A run is playable for >= 5 min at a measured fps (record it).
When 1-6 are evidenced, 32-bit (Spacewar only) and D3D9 work start automatically; no need for João to say so.

## Launch progress and failure

A guest window or D3D device is a progress milestone, not a successful launch or
proof of playability. `launch_result.json` records `state=running`,
`terminal=false` and `ok=false` while execution continues. A later guest return,
exception or missing prerequisite replaces that progress with a terminal result.
`attemptId` ties the request, result and current boot log together so an older
result cannot satisfy the test.

If the guest has taken over the screen and then fails, a native failure dialog
explains the stop and offers **Close launcher**. Reopen the launcher before another
Play attempt: guest modules, threads and graphics resources are retained rather
than unsafely unloaded or reused. A genuine-Steam prerequisite error means the
runtime cannot yet start the required real client; no replacement API or bypass
is used.

`play-test.sh` rejects failed, stale and nonterminal results. Exit 0 requires a
fresh observed guest completion with exit code zero plus the existing GAP check;
it still does **not** establish the visual, controller, audio, real-Steam or
five-minute gameplay requirements above. A still-running guest at the timeout is
not converted into acceptance success.
