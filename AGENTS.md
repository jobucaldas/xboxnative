# Contributing rules (humans and agents)

1. **Pull requests only.** Never push to `main`. Branch `lane/<name>` or `feat/<name>`; open a PR; Cursor Bugbot reviews it.
2. **Never weaken a gate.** Do not loosen assertions, skip sections, shorten timeouts, or count a SKIP as PASS to get green. A failing test is evidence.
3. **Owned games only; never enable piracy.** Play runs games the signed-in Steam account owns, installed from official depots. An emulated Steam API (e.g. gbe_fork, pinned and fetched, never committed) may answer an owned game's Steam calls; achievements and cloud saves must reach the player's real Steam account. Substitution happens in memory: never patch or strip game or Valve files on disk (no Steamless or cracks). No Xbox security/Dev-Mode restrictions bypass. A launch-time ownership check is required before an emulator ships to users.
4. **No secrets or personal data.** Never commit or print `DP_PASS`, Tailscale keys, Steam refresh/access tokens, `steam_session.bin`, signing keys/pfx, SteamIDs, or screenshots showing accounts. Logs must be reviewed before commit.
5. **No game files, Valve/Microsoft/Unity binaries, or large binaries (>5 MB).** External code is pinned in `deps.pin` and fetched, not copied (see `patches/`).
6. **Acceptance test is the launcher:** `src/xbox-test/tools/play-test.sh <appid>` ("Play Megabonk via the launcher"). Evidence goes in the PR (file names and summary), not raw captures with account data.
7. Console access is shared: take the console lock (`dp-lib.sh` does it); bump the package version before reinstalling.
8. Keep third-party code under its own licence and listed in `NOTICE`. Repo licence is GPL-3.0-or-later (SPDX `GPL-3.0-or-later` in new scripts/sources is welcome).
9. **Never commit third-party binaries** (e.g. Wine-built DLLs). Build them from a pinned source at setup time: add a `deps.pin` entry and a `src/tools/fetch-*.sh` / `src/tools/build-*.sh` script (see `src/tools/build-wine-donors.sh`).
