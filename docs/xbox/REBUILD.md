# REBUILD.md — lessons from before the box reset (Sun 2026-10-25 ~20:02 BRT)

## Linux UWP environment preflight

Run `tools/check-uwp-build-env.sh` before the Linux UWP/MSIX build. It is read-only and exits
nonzero when a tool, pinned wrapper checkout, or expected SDK/CRT file is missing. Tool overrides
use `PYTHON3_PATH`, `CLANG_PATH`, `CLANG_CL_PATH`, `MAKEMSIX_PATH`, and `OSSLSIGNCODE_PATH`;
set `UWP_CROSSBUILD`, `UWP_SDK_ROOT`, and `UWP_CRT_ROOT` for non-default checkout/cache paths.
The script checks file presence only: it does not establish SDK/CRT usability or ABI compatibility.
In particular, LLVM-MinGW is not evidence of MSVC CRT compatibility. A ready result is a
prerequisite report only, never package or Xbox runtime acceptance.

The box was reset: /workspace/Boxedwine-xbox-64 (with its local-only `x64-spike` branch), worktrees,
x64-run, Megabonk files, tailscale, Claude/Codex logins, DP_PASS and TS_AUTHKEY were lost.
GitHub (jobucaldas/Boxedwine-xbox-64) only has `master`. Nothing is recoverable. Surviving on the console: the installed Boxedwine dev app + its results.
Surviving in the agent store: earlier screenshots/proof videos (grok-bot/agents/*/attachments).

RULES: no pushes anywhere, no posts to other people's repos, no secrets in logs/files.
Back up work with `x64-run/backup.sh` (git bundles into the persistent agent store) after each milestone.
