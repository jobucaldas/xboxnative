# Installing the test kit on the Xbox Series S (for João)

Everything here is off the kit built by `xbox-test/kit/build-kit.sh`. Package
versions change between builds, so read them from `manifest.json` in the kit
directory instead of trusting any number in this file.
`bash xbox-test/kit/check-install-guide.sh` checks that every file, script and
log marker named in backticks in this guide still exists in the repo.

Legend: **[verified]** = used successfully against the console by the scripts
under `xbox-test/dp/` (see `xbox-test/dp/README.md`). **[community]** = taken
from third-party Dev Mode guides, never run by us. **[not verified]** = no
source in this repo or Microsoft docs.

## Before you start

1. Check the kit first (on the PC/box, no console needed):
   `bash xbox-test/kit/verify-kit.sh` and `bash xbox-test/kit/verify-kit-offline.sh`
   must both print `RESULT: PASS`. Kit location: `/workspace/x64-run/kit/latest`
   (symlink to the newest `<YYYYmmdd-HHMMSS>` directory).
2. The console must be in Dev Mode with Device Portal enabled. **[community]**
   Dev Home, Home tab, Remote Access, Remote Access Settings, tick
   "Enable Xbox Device Portal", set a user name and password. The URL shown
   under Remote Access (`https://<console-ip>:11443`) is the Device Portal
   address. Source: <https://xboxoneresearch.github.io/wiki/development/dev-portal-api/set-up-device-portal/>.
3. The apps must be of type Game (5 GB limit). Already confirmed for our
   packages in `docs/xbox/REBUILD.md`. For a newly installed package: **[community]**
   in Dev Home, "Games & Apps", highlight the app, press the Select/View
   button, choose view details, change App to Game.
4. The kit files per package are `<name>.msix` plus its public `<name>.cer`
   (publisher `CN=grok-xbox-dev`). Packages: `uwph`, `wxl`, `w32u`, `vrx`,
   `launcher` (only present if the launcher worktree existed at build time),
   plus the extra `wxl-test.exe`. The kit never contains a private key.

## Install (pick route A or B)

Route A, scripts (recommended, **[verified]**). Needs `DP_PASS` in the
environment and the network path used by `xbox-test/dp/dp-lib.sh` (defaults:
`DP_HOST=192.168.15.96`, `DP_SOCKS=localhost:1055`; override both if you are
on the LAN directly, e.g. `DP_SOCKS=` empty). Never paste the password into a
file or log.

1. For each package run
   `bash xbox-test/dp/install.sh <kit>/<name>.msix <kit>/<name>.cer`
   and wait for `INSTALL OK`. Order does not matter between `uwph`, `wxl`,
   `w32u` and `vrx`. Installing does not launch anything.
2. Do not uninstall `launcher` (`Grok.XboxTest`) to "reinstall": it holds the
   Steam login. Installing `launcher.msix` over the same identity upgrades in
   place (see `xbox-test/kit/README.md`). The uninstall script
   `xbox-test/dp/uninstall.sh` is **not tested**; do not use it.

Route B, browser (**[community]**, not run by us for these packages):

1. Open the Device Portal URL in a browser, accept the certificate warning,
   sign in.
2. In "My games & apps" click Add, pick `<name>.msix`, follow the dialogs and
   press Start/Launch to upload. Whether the dialog accepts our `<name>.cer`
   is **not verified**; if it rejects the signature, use route A, which sends
   the `.cer` in the same upload request as the `.msix`.
3. The path `System -> Apps` shown in `xbox-test/kit/README.md` is **not
   verified**.

## Launch and what success looks like

Each app writes a log into its own `LocalState` folder and also draws on the
TV. Full package name: `<Identity>_<Version>_x64__dk715arakrne0`; launch id:
`<Identity>_dk715arakrne0!App` (identity and version are in `manifest.json`).

1. Optional, **[verified]** partially (HTTP 400 when already signed in, then
   launch works anyway): `bash xbox-test/dp/user-signin.sh`.
2. Launch with script (**[verified]**):
   `bash xbox-test/dp/launch.sh '<Identity>_dk715arakrne0!App' '<Identity>_<Version>_x64__dk715arakrne0'`
   and expect `launch HTTP 200`. Or start it from Games & Apps on the console.
3. Wait for the success line in the log
   (`bash xbox-test/dp/wait-log.sh '<full name>' <logfile> '<pattern>' 60 /tmp/out.log`):

| Package | Log file | Success line (grep) | On the TV |
| --- | --- | --- | --- |
| `uwph` | `k32.log` | `K32 RESULT failures=0` and `D3DT RESULT presented=300 hr=0x00000000` | window cycles colors for 300 frames, ends solid magenta |
| `wxl` | `k32.log` | `WXL RESULT entry=42 failures=0` (first launch shows `WXL FAIL open` until step 4 below) | same window as `uwph` |
| `w32u` | `w32u.log` | `W32U RESULT failures=0` | 180 frames then solid green |
| `vrx` | `vrx.log` | `vrx: all cases done`, and `caseC` line must say OK | window idles on a heartbeat frame |
| `launcher` | `boot.log` | boot reaches the QR or library screen | Steam QR login or your library |

Screen descriptions come from `xbox-test/kit/README.md`; the log markers are
grep-checked in the source by `check-install-guide.sh`.

4. `wxl` guest upload (**[verified]** script, see "Minimal PE mapper" in
   `xbox-test/uwph/host/README.md`):
   `bash xbox-test/dp/put-file.sh '<wxl full name>' '\LocalState' <kit>/wxl-test.exe`,
   stop the app with `bash xbox-test/dp/kill.sh '<wxl full name>'` (confirm with
   `bash xbox-test/dp/ps.sh wxl`; the HTTP status of kill is unreliable), then
   launch again.
5. Screenshot of what the TV shows (**[verified]**):
   `bash xbox-test/dp/screenshot.sh /tmp/shot.png`.

## Collect logs back

1. List what is there: `bash xbox-test/dp/ls-dir.sh '<full name>' '\LocalState'`.
2. Pull a file (resumable): `bash xbox-test/dp/get-file.sh '<full name>' '\LocalState' k32.log /tmp/k32.log`.
3. Send the pulled `.log` files and screenshot back. Do not send `DP_PASS` or
   anything under the launcher's session files in `LocalState`.
4. Crash dumps: `bash xbox-test/dp/logs.sh list '<full name>'` lists them, but
   downloading a dump is not solved (`xbox-test/dp/README.md`): treat as
   **not verified**.
5. Browser File Explorer route in `xbox-test/kit/README.md`
   (File Explorer, LocalAppData) is **not verified**.

## If something goes wrong

- Install fails with 0x800B0100: package is unsigned or the certificate is not
  accepted. Run `bash xbox-test/kit/verify-kit.sh`. **[community]**
- App closes right after launch: see "What makes the app stay open" in
  `docs/xbox/REBUILD.md`.
- Console wedged: `POST /api/control/restart` on Device Portal (not wrapped in
  a script here; restarting is a last resort).
