# Build and test

One entry point, `./xn`, from the repository root. It reuses the existing scripts
(`src/xbox-test/app/build.sh`, `src/xbox-test/dp/*.sh`, `src/xbox-test/tools/play-test.sh`) and adds no new way of
talking to the console. `./xn --help` lists everything.

| Command | Does | Needs |
|---|---|---|
| `./xn doctor` | Reports which tools and `.env` variables are present. Read-only. | nothing |
| `./xn check` | Runs the offline steps of `.github/workflows/ci.yml`: shell syntax, Python compile, unit tests, C++ tests, secret/binary guards. | `bash git python3 node c++` |
| `./xn build` | Builds the launcher package, `src/xbox-test/app/out/xbtest.msix`, signed with a throwaway local test certificate. | the launcher build toolchain (see [README](../README.md#build)) |
| `./xn local` | `check`, then `build`: everything that needs no console. | both of the above |
| `./xn console` | Builds, bumps the version, signs, installs on the console, launches, runs the launcher self-test, collects logs, prints PASS or FAIL. | the build toolchain, a console, `.env` |

## Check (no console)

```sh
./xn check                 # the offline-checks job
./xn check --list          # the steps and what each job needs
./xn check --job x86-core  # another CI job
./xn check --all           # every CI job
```

`check` does not keep its own list of tests: it reads the `run:` steps of `.github/workflows/ci.yml` and runs
them, so it cannot drift from CI. A new CI step is picked up automatically. The default job needs only a C++
compiler, Python, Node and Git. The other jobs also need the mingw cross compilers and Wine. When a job's tools are
missing its steps are reported `SKIP`, and **a skip is not a pass**: the command exits 3 instead of 0
(`--allow-skip` accepts it). Failed steps print the tail of their log; full logs are kept in the directory named
on the last line.

## Build

```sh
./xn build                       # the version in src/xbox-test/app/AppxManifest.xml
./xn build --version 0.5.300.0   # stamp the package copy; the tracked manifest is left alone
```

`build` first runs `src/tools/check-uwp-build-env.sh` and stops with the list of what is missing. The prerequisites
(cross-build wrapper, `makemsix`, Wine donors) are in the README's Build section; `src/tools/fetch-uwp-crossbuild.sh`
and `src/tools/build-msix-packaging.sh` fetch them from the pins in `deps.pin`. The build is long and memory hungry;
the full log is `out/xn/build.log`.

## Console

### One-time setup

1. Put the console in Dev Mode and enable Device Portal ([src/xbox-test/dp/README.md](../src/xbox-test/dp/README.md)).
2. `cp .env.example .env` and fill it in. `.env` is git-ignored; never commit it, never paste it into a PR or log.

| Variable | Meaning |
|---|---|
| `DP_HOST` | Device Portal host, bare or as a URL (required) |
| `DP_USER`, `DP_PASS` | Device Portal credentials (`DP_PASS` is required and is never printed) |
| `DP_PORT` | Optional, default 11443 |
| `DP_SOCKS` | Optional SOCKS5 proxy, default `localhost:1055`; set it empty to connect directly |
| `DP_LOCK` | Optional console lock path shared by every session that uses this console |
| `UWP_CROSSBUILD` | Optional path of the cross-build wrapper checkout |

These are the names `dp-lib.sh` already reads. Variables already in your environment win over `.env`; `.env` is
read as plain `KEY=VALUE` lines and is never executed. In a git worktree, which does not copy ignored files, `xn`
falls back to the `.env` of the main checkout. `XN_ENV=/path/to/file` selects another file.

### Run

```sh
./xn console --probe     # read-only: reaches the console, checks the credentials, shows the installed and next version
./xn console --dry-run   # validates .env and tools and prints the plan; no network at all
./xn console --yes       # the full run
```

`--yes` is required without a terminal. Without it `xn` explains the risk and asks you to type `yes`.

What a full run does, in order, each step a PASS or FAIL line:

1. Reaches Device Portal and checks the credentials (401 and "unreachable" are told apart).
2. Reads the installed launcher version and picks the next one: one patch above the larger of the installed and the
   manifest version. A same-version install is silently ignored by the console, so this is never skipped. With
   `--msix FILE` it installs that package instead of building one, and refuses it unless it is newer.
3. Builds and signs the package with that version (`PACKAGE_VERSION` in `app/build.sh`).
4. Takes the console lock (`DP_LOCK`) for the rest of the run, so other sessions wait instead of colliding.
   If another session installed a newer version meanwhile it stops and asks you to re-run.
5. Records the `LocalState` file names, stops the running launcher, installs, then waits until the new version is
   actually listed (the install call can report success before the deployment finishes).
6. Launches it and waits for the main loop.
7. Uploads `selftest.txt` and waits for the launcher's own `SELFTEST SUMMARY`
   (see [SELFTEST.md](SELFTEST.md)). The launcher deletes the log when it takes the
   request, so `xn` waits until `selftest.txt` is gone and then judges the fresh log
   with no line offset; an earlier run's summary never satisfies it.
8. Collects `selftest.log`, `selftest_probe.log`, `boot.log` and a screenshot.
9. With `--game APPID`, then runs the acceptance test `src/xbox-test/tools/play-test.sh APPID` against the new package
   (an owned game and a signed-in launcher are needed).

Options: `--modes x86,d3d9` (default `all`), `--timeout SECONDS` (default 900), `--out DIR`, `--version A.B.C.D`.

### Reading the result

`PASS` needs every requested self-test mode to pass. `FAIL`, `BLOCKED` and `SKIP` are all not-pass; there is no
partial credit, and a summary that disagrees with its result lines is a failure. The last line is
`xn console: PASS` or `xn console: FAIL`, with exit status 0 or 1; exit 2 is a setup problem (missing variable,
tool, refused credentials, unreachable console). A self-test pass says the packaged fixtures ran; it is not a claim
that a game is playable ([PLAYABLE.md](PLAYABLE.md)).

Evidence goes to `out/xn/console-<time>/` (git-ignored). `boot.log` can contain account details: review it
before sharing and never commit it.

### Risks

* Installing replaces the launcher on a shared console. Once, an upgrade removed the app's local data: the Steam
  login and installed games had to be redone by the owner. `xn` records the `LocalState` file names before and
  after and warns if `steam_session.bin` disappeared. It never reads or copies that file.
* Check that nobody else is using the console before a run. The lock only orders `xn` and the other scripts.
* The launcher restarts between self-test fixtures; that is expected.

## What is not covered

`./xn check` and the offline tests do not exercise the console. The on-device sections of
`src/xbox-test/smoke-all.sh` (the older uwph test packages) are unchanged and still run separately.
