# Console self-test

The launcher can run its own graphics and 32-bit paths on the console without a game:
it starts small fixture programs through the same Play runtime a launch uses and writes
one result line per fixture. This exists because the installed package ships the launcher
and the Play runtime, not the off-device test runner
(`src/xbox-test/app/tests/play-runner.cpp`) or its fixtures, so those paths otherwise had
no way to be exercised on an Xbox.

## Asking for a run

Write a one-shot request file into the launcher's local data folder:

```
selftest.txt
```

One mode per line; blank lines and `#` comments are ignored, names are case-insensitive,
duplicates collapse, and `all` expands to every mode. The file is consumed (deleted) when
the launcher reads it, which it only does while no game is launching or running; a request
that arrives during a launch waits on disk until the launcher is idle. A mode the build
does not carry is reported as `SKIP`, never silently dropped.

| Mode | Fixture | What it exercises |
|---|---|---|
| `x86` | `diag\fixture-crt.exe` | A 32-bit mingw-CRT program through the PE32 interpreter |
| `vulkan-dzn` | `diag\play-vulkan-dzn.exe` | Vulkan through the packaged Mesa dzn ICD and Khronos loader |
| `d3d9` | `diag\play-d3d9.exe` | The D3D9 shim over D3D11 |
| `d3d10` | `diag\play-d3d10.exe` | The D3D10 shim over D3D11 |
| `opengl` | `diag\play-opengl.exe` | Mesa's d3d12 WGL driver: pixel format, context, clear, SwapBuffers, glReadPixels |

The fixtures are built from this repository's sources when the package is built
(`src/xbox-test/app/tests/build-diag-fixtures.sh`) and packed under `diag\` beside the
launcher; each is a few KB. Build with `PLAY_SELFTEST=0` to leave them out.

## Reading the result

The launcher appends to `selftest.log` in the same folder:

```
SELFTEST BEGIN request=4 modes
SELFTEST RESULT x86 PASS
SELFTEST RESULT vulkan-dzn BLOCKED device-creation result=-9
SELFTEST RESULT d3d9 PASS
SELFTEST RESULT d3d10 FAIL exit-17
SELFTEST SUMMARY pass=2 fail=1 blocked=1 skip=0
```

`selftest_probe.log` holds the evidence behind each line: the mapping summary, the
fixture's own output and Play's `PRUN` lines. Fetch both files from the app's local data
folder after the run.

The verdict comes from the fixture's exit code (42 is success) and from whether Play
actually ran guest code and the guest exited. It is not a visual or playability claim:

| Verdict | Meaning |
|---|---|
| `PASS` | The fixture ran and exited 42 |
| `PASS frame-presented` | `vulkan-dzn` ran and presented a frame |
| `BLOCKED device-creation` | `vulkan-dzn` ran, but the host's D3D12 refused the dzn device |
| `BLOCKED pixel-readback` | `opengl` ran to a context, but the cleared frame did not read back as drawn |
| `FAIL exit-N` | The fixture ran and exited N |
| `FAIL no-guest-code` | The image mapped but no guest code ran |
| `FAIL did-not-exit` | Guest code ran and the fixture never exited |
| `FAIL donor-gate` / `FAIL u32c-test` | A runtime prerequisite a launch also needs is missing |
| `FAIL restart-limit` / `FAIL stale-request` | The run could not continue across launcher restarts |
| `FAIL empty-request` | `selftest.txt` named no mode |
| `SKIP unknown-mode` / `SKIP not-packaged` | The request named a mode this build cannot run |

## How a run proceeds

Play keeps one 64-bit guest in the launcher process and cannot unload it, so the run is a
queue: the pending modes go to `selftest_queue.txt`, the launcher runs one 64-bit fixture,
and restarts itself (`CoreApplication::RequestRestartAsync`) for the next one. The log is
appended across those restarts, so it is the whole request's record. The 32-bit fixture
runs in the interpreter and needs no restart, so it comes first. A queue left behind by a
previous process resumes on the next start while it is fresh (30 minutes); after one
restart per 64-bit fixture (plus one spare, so a fixture that faults before game code can
be retried) without progress the remaining modes are reported `FAIL restart-limit`.

While a fixture runs, Play belongs to the self-test: starting a game from the grid is
refused, and the game's page says so. Once the run ends (or the launcher restarts for
the next fixture) Play is free again.

The self-test never touches Steam, cloud saves or presence: a fixture is not a game and
must not reach the player's account.

## Forcing Vulkan for a Unity game

Unity titles are launched with `-force-d3d11` so they use the guest D3D11 route. To run one
on the Vulkan path instead, opt in per game or for the whole launcher:

- `vulkan_<appid>.txt` in the local data folder — one game;
- `vulkan.txt` — every Unity title;
- the `PLAY_FORCE_VULKAN` environment variable — the whole launcher process.

The file holds `1`, `on`, `true` or `yes` (a line may also be written `vulkan=1`). Anything
else keeps D3D11, which is the default. The launch log records the choice as
`PLAY_FORCE_VULKAN` on the target's reason line.

## Off-device

The planning logic — fixture list, request parsing, the restart queue and exit-code
verdicts (`src/xbox-test/app/selftest_plan.h`) — is console-free and covered by
`src/xbox-test/app/tests/selftest-plan.sh`. The fixture build is covered by the
`diag-fixtures` CI job. Running the fixtures themselves needs a console.
