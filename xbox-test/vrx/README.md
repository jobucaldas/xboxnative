# VRX allocation probe

The committed `results/console-vrx.log` contains three native case outcomes:
direct RWX allocation, ordinary RW-to-RX protection, and the FromApp RW-to-RX
path. To validate their expected text outcomes offline, run from the repository
root with Node.js 24 (also used in CI):

```sh
node xbox-test/vrx/tests/review-vrx-log.mjs xbox-test/vrx/results/console-vrx.log
```

The CLI requires exactly one explicit local log path. Exit codes are 0 for the
expected outcomes, 1 for invalid/missing/duplicate/contradictory case results,
and 2 for usage or file-read errors. It does not contact a console.

## Offline tests and discovery

Run both test files explicitly from the repository root:

```sh
node --test xbox-test/vrx/tests/check-vrx-log.test.mjs xbox-test/vrx/tests/review-vrx-log.test.mjs
```

The suite has 10 tests (3 parser tests and 7 CLI tests), all using synthetic
inputs. CI runs this command in addition to the existing offline checks. The
CLI tests locate the script relative to their own module, not the working
directory, so they also work with `cd xbox-test/vrx/tests && node --test`.

Do not treat an exit code of 0 with **0 tests** as validation. On Node.js 24, the
older command `node --test xbox-test/vrx/tests/*.test.mjs` can silently discover
zero tests if run outside the repository and the glob matches nothing. Explicit
filenames make missing files fail instead. Python's default unittest discovery
does not run these JavaScript tests either. Check the reported test/pass counts,
not just the exit code.

## Evidence limits

This checks log text only. A successful review does not establish freshness,
identify the device, or prove a new device or guest execution.

- [Historical lane notes](../../docs/xbox/lane-notes/vrx.md#final-result--verified-on-real-xbox-series-s-2026-10-25)
  attribute the capture to an Xbox Series S run on 2026-10-25 using the VRX
  0.1.0.0 package. This is a historical repository claim, not independently
  verified device identity.
- The log was imported in commit `bd208bf` (2026-10-26). Its timestamps are
  elapsed times only; it has no wall-clock capture date, run identifier, or
  device identifier. Git dates and checkout file modification times do not
  establish when or where it ran.
- The committed log SHA-256 is
  `9ea9ce471a9073b7a7428e7f2b6bb551da6e84d903da632dcb842507ca4cef81`.
  This identifies the reviewed bytes, not their origin or freshness.

Fresh console execution and device identity remain **unverified**. Synthetic
unit tests, historical log text, and `smoke-all.sh --offline` are not console
acceptance evidence. The existing full console smoke and launcher play-test
gates in [AGENTS.md](../../AGENTS.md) remain unchanged.
