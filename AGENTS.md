# Contributing rules (humans and agents)

1. **Pull requests only.** Never push to `main`. Branch `lane/<name>` or `feat/<name>`; open a PR; Cursor Bugbot reviews it.
2. **`xbox-test/smoke-all.sh` must pass** (full run on a console; `--offline` is not enough for changes that touch on-device code). State the result in the PR.
3. **Never weaken a gate.** Do not loosen assertions, skip sections, shorten timeouts, or count a SKIP as PASS to get green. A failing test is evidence.
4. **Legitimate Steam path only.** No DRM bypass, no Steamless/gbe_fork/cracked or emulated Steam API, no patching of Valve or game files, no Xbox security/Dev-Mode restrictions bypass.
5. **No secrets or personal data.** Never commit or print `DP_PASS`, Tailscale keys, Steam refresh/access tokens, `steam_session.bin`, signing keys/pfx, SteamIDs, or screenshots showing accounts. Logs must be reviewed before commit.
6. **No game files, Valve/Microsoft/Unity binaries, or large binaries (>5 MB).** External code is pinned in `deps.pin` and fetched, not copied (see `patches/`).
7. **Acceptance test is the launcher:** `xbox-test/tools/play-test.sh <appid>` ("Play Megabonk via the launcher"). Evidence goes in the PR (file names and summary), not raw captures with account data.
8. Console access is shared: take the console lock (`dp-lib.sh` does it); bump the package version before reinstalling.
9. Keep third-party code under its own licence and listed in `NOTICE`. Repo licence is GPL-3.0-or-later (SPDX `GPL-3.0-or-later` in new scripts/sources is welcome).
10. **Never commit third-party binaries** (e.g. Wine-built DLLs). Build them from a pinned source at setup time: add a `deps.pin` entry and a `tools/fetch-*.sh` / `tools/build-*.sh` script (see `tools/build-wine-donors.sh`).
