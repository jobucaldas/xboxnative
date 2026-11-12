# Scripting the launcher and a running game with cmd.txt

The launcher consumes a one-shot command file, `LocalState\cmd.txt`, on every
frame tick (up to 64 lines per file, each at most 40 characters). A tester
writes it over the Device Portal file channel and the launcher deletes it on
read; every executed line is logged to `LocalState\boot.log` as
`cmd.txt SYNTHETIC command: '<line>'`. The file is never periodic and never
present unless a script put it there.

All commands here are infrastructure-agnostic: any file-transfer channel into
the app's `LocalState` works the same way.

## Launcher UI commands

| Command | Effect |
| --- | --- |
| `open <appid>` | open the game's detail page |
| `press` / `a`, `back` / `b`, `up`, `down`, `left`, `right` | one pad press on the launcher UI |
| `lb`, `rb`, `x`, `y`, `l3` | the remaining pad buttons the UI maps |
| `sort`, `filters`, `search`, `profile`, `menu` | open the named UI surface |
| `type <text>` | replace the on-screen keyboard's query (keyboard must be open) |
| `sleep <seconds>` | pause the command queue (max 600) |
| `stacks` | dump guest thread stacks to `launch_probe.log` |
| `gtab <0-2>` | switch the in-game guide overlay's tab |

## In-game commands (launcher overlay)

Accepted only while a game guest is running:

| Command | Effect |
| --- | --- |
| `gmenu` | toggle the launcher's in-game guide overlay |
| `gquit` | ask the game's windows to close (WM_CLOSE) |
| `gforce` | force quit: sync saves and restart the launcher |
| `gfps` | toggle the overlay FPS counter |

## In-game pad injection: `gpad` and `gpadseq`

These drive the *game's own* controller input. The pressed buttons are ORed
into the pad state the launcher already publishes to the guest through its
emulated XInput (the same path a physical pad's reading takes), so an
XInput/RawInput game sees them exactly like a real controller. Precedence:

- Injected buttons are **ORed** with the physical pad's reading: an injected
  press can never release a button the player is holding, and a physical
  press can never release an injected one. Either source releasing its own
  buttons is visible to the game.
- The launcher's in-game overlay (guide menu) always wins: while it owns
  input the game sees an empty pad, automation included.

Safety: `gpad`/`gpadseq` are accepted **only** from the cmd.txt file channel
and **only** while a game guest is on screen. The game itself and the network
cannot reach this code path, and an armed program is cleared as soon as the
guest ends, so nothing outlives the session.

### `gpad <buttons>[ hold_ms]`

Press the listed buttons, hold for `hold_ms` milliseconds (default 120, max
60000), then release. Button names, comma-separated, case-insensitive:

`A B X Y LB RB LT RT DPAD_UP DPAD_DOWN DPAD_LEFT DPAD_RIGHT` (short forms
`UP DOWN LEFT RIGHT`), `START BACK LSTICK RSTICK` (short forms `LS RS`).

`LT`/`RT` are the analog triggers: they are delivered as the trigger bytes
(255) rather than buttons, matching a physical trigger's full pull.

Examples:

```
gpad DPAD_DOWN
gpad A 250
gpad LT,RT 500
```

An unknown button name rejects the whole command (logged as
`cmd.txt: gpad rejected: unknown or empty button list`) — a typo never
silently presses nothing.

### `gpadseq <name>`

Arm a multi-step program from `LocalState\gpadseq\<name>.txt` (uploaded the
same way as cmd.txt). One step per line, same grammar as `gpad` plus an
optional second number, the gap in milliseconds after the release:

```
# Slime Rancher repro: open Options, change resolution, back out
DPAD_UP 150
A 200 100
DPAD_DOWN 100 50
DPAD_DOWN 100
```

Blank lines and lines starting with `#` are comments. At most 64 steps, each
at most 40 characters (the cmd.txt caps). The launcher logs
`cmd.txt: gpadseq '<name>' armed, N step(s)` on success.

## Watching for the result

`boot.log` carries every executed command line and the gpad arm/release
lifecycle (`cmd.txt: gpad armed (1 step(s))`). The launcher restarts on guest
exit, so scripts should poll the log the way `src/xbox-test/dp/wait-log.sh`
does rather than reading it once.

## Ready-to-run helper

`src/xbox-test/dp/game-pad.sh` writes a `gpad` command (or a `gpadseq`
program) into cmd.txt over the Device Portal file channel and waits for the
ack line in boot.log. See that script's usage comment.
