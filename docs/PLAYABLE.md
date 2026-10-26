# Definition of "Megabonk playable" (proved by tests, not announced) — João 15:09
All proven by `xbox-test/tools/play-test.sh 3405340` evidence (log + screenshots/video) on the Series S:
1. Play runs Megabonk's code (gameCodeExecuted=true), Unity reaches its window and D3D11 device, no crash.
2. Main menu visible via screenshot, stable for >= 5 min.
3. Controller input (real pad, or one-shot synthetic labelled) moves through the menu and starts a run.
4. Audio: XAudio2 output has non-zero samples.
5. Steam API calls succeed through the legitimate real-client path (no DRM bypass).
6. A run is playable for >= 5 min at a measured fps (record it).
When 1-6 are evidenced, 32-bit (Spacewar only) and D3D9 work start automatically; no need for João to say so.
