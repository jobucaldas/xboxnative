# Contributing

- **Pull requests only.** Never push to `main`; open a PR from a feature branch.
- **Never weaken a test gate.** Do not loosen assertions, skip sections, shorten timeouts, or count a
  SKIP as PASS to get green. A failing test is evidence. `src/xbox-test/smoke-all.sh` must pass.
- **Owned games only; never enable piracy.** Play runs games the signed-in Steam account owns, installed
  from official depots. An emulated Steam API (e.g. gbe_fork) may answer an owned game's Steam calls;
  achievements and cloud saves must reach the player's real Steam account. Substitution happens in
  memory: never patch or strip game or Valve files on disk. No Xbox security/Dev-Mode restrictions bypass.
- **No secrets or personal data.** Never commit or print `DP_PASS`, Steam refresh/access tokens,
  `steam_session.bin`, signing keys/pfx, SteamIDs, or screenshots showing accounts. Review logs before
  committing.
- **No game files, Valve/Microsoft/Unity binaries, or large binaries (>5 MB).** External code is pinned
  in `deps.pin` and fetched, not copied (see `patches/`). Never commit third-party binaries (e.g.
  Wine-built DLLs); build them from a pinned source at setup time via `src/tools/fetch-*.sh` /
  `src/tools/build-*.sh`.
- Third-party code stays under its own licence and is listed in [NOTICE](NOTICE). Contributions are
  accepted under GPL-3.0-or-later.

Build and test commands: [README.md](README.md) and [docs/BUILD-AND-TEST.md](docs/BUILD-AND-TEST.md).
