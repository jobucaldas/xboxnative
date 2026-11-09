# Definition of "Megabonk playable" (proved by tests, not announced) — João 15:09
All proven by `src/xbox-test/tools/play-test.sh 3405340` evidence (log + screenshots/video) on the Series S:
1. Play runs Megabonk's code (gameCodeExecuted=true), Unity reaches its window and D3D11 device, no crash.
2. Main menu visible via screenshot, stable for >= 5 min.
3. Controller input (real pad, or one-shot synthetic labelled) moves through the menu and starts a run.
4. Audio: XAudio2 output has non-zero samples.
5. Steam API calls succeed (genuine client or an emulated API for the owned game), and achievements and cloud saves reach the player's real Steam account.
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
than unsafely unloaded or reused. A Steam prerequisite error means no Steam API
provider is installed yet for the game; Play never patches game files to avoid it.

`play-test.sh` rejects failed, stale and nonterminal results. Exit 0 requires a
fresh observed guest completion with exit code zero plus the existing GAP check;
it still does **not** establish the visual, controller, audio, real-Steam or
five-minute gameplay requirements above. A still-running guest at the timeout is
not converted into acceptance success. When the first press starts an install,
the script waits for the download and presses the main button again, as a player
does: the launcher never launches a game by itself, and a launch request that
follows a finished download without that second press fails the run.
