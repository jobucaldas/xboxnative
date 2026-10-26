# Test kit — on-device Dev Mode sideload bundle

`build-kit.sh` collects every current signed test package (plus the
lane/dl launcher, if that worktree is present) into one directory:

```
/workspace/x64-run/kit/<YYYYmmdd-HHMMSS>/
  uwph.msix        uwph.cer
  wxl.msix         wxl.cer
  w32u.msix        w32u.cer
  vrx.msix         vrx.cer
  launcher.msix    launcher.cer   (only if /workspace/wt/dl exists)
  wxl-test.exe                    (extra: guest PE for the WXL mapper check)
  manifest.json                   (identity, version, sha256 per package)
```

`/workspace/x64-run/kit/latest` symlinks to the most recent run.
`verify-kit.sh` checks the kit (no `.pfx`, hashes match, every `.msix`
signature verifies against its own `.cer`) and exits non-zero on any
problem. Run both before sideloading:

```sh
bash xbox-test/kit/build-kit.sh
bash xbox-test/kit/verify-kit.sh
```

These two commands plus `bash xbox-test/smoke-all.sh --offline` are this
lane's acceptance gate and must all exit 0.

## What's in the kit

| Package | Identity | AppUserModelId (RID) | Logs to | Success line | On screen |
| --- | --- | --- | --- | --- | --- |
| `uwph` | `Joao.XboxSteam.Uwph` 0.1.6.0 | `Joao.XboxSteam.Uwph_dk715arakrne0!App` | `LocalState\k32.log` | `K32 RESULT failures=0` then `D3DT RESULT presented=300 hr=0x00000000` | Window flashes through 300 colored D3D11 clears, ends on a solid magenta frame |
| `wxl` | `Joao.XboxSteam.Wxl` 0.1.1.0 | `Joao.XboxSteam.Wxl_dk715arakrne0!App` | `LocalState\k32.log` | First launch (no guest yet): `WXL FAIL open`. After uploading `wxl-test.exe` and relaunching: `WXL RESULT entry=42 failures=0` | Same k32/D3DT window behavior as `uwph`; this is the isolated-identity build of the same host |
| `w32u` | `Joao.XboxSteam.W32u` 0.1.5.0 | `Joao.XboxSteam.W32u_dk715arakrne0!App` | `LocalState\w32u.log` | `W32U RESULT failures=0` | Window presents 180 frames then holds solid green (RGB-stable, no gamepad needed; it also exercises synthetic XInput conversion internally) |
| `vrx` | `Joao.XboxSteam.Vrx` 0.1.0.0 | `Joao.XboxSteam.Vrx_dk715arakrne0!App` | `LocalState\vrx.log` | `vrx: all cases done` (check each of `caseA`/`caseB`/`caseC` individually — `caseA` and `caseB` are expected to fail/except under AppContainer, `caseC` via `*FromApp` is expected `OK value=42`) | Window opens, briefly renders, then idles on a heartbeat frame |
| `launcher` | `Grok.XboxTest` 0.4.3.0 | `Grok.XboxTest_dk715arakrne0!App` | `LocalState\boot.log` (and its own session files — do not touch those) | boot reaches the QR/library screen | Steam QR login screen, or library view if already signed in |

All five share publisher `CN=grok-xbox-dev` and therefore the same family
suffix `dk715arakrne0`; only the `Name` part of the identity differs. `uwph`
and `wxl` are two manifest variants of the **same binaries** (see
`xbox-test/uwph/host/README.md`) — installing one does not remove the other,
they are separate package families.

## Installing via Device Portal (manual "Add app")

1. Open `https://192.168.15.96:11443` in a browser (or via the Tailscale
   SOCKS proxy), sign in with the Device Portal credentials.
2. Go to **System -> Apps**, click **Add** (the "+" above the app list).
3. Choose **Local Storage**, pick the package's `.msix` from the kit
   directory, add its matching `.cer` as a certificate file in the same
   dialog, then **Add it**. Wait for "Installation of package ... completed."
4. Repeat for each package. Installing does not launch it.
5. To run: find the app under **Apps**, select it, press **Start**. To see
   logs: **File Explorer -> LocalAppData -> `<PackageFamilyName>` ->
   LocalState**, download the `.log` file named in the table above.
   Screenshots: the Device Portal home page has a **Take Screenshot** button
   (same image `xbox-test/dp/screenshot.sh` pulls from `/ext/screenshot`).

Equivalent command-line route (each `dp/*.sh` script takes
`CONSOLE.lock` itself — do not wrap them in another `flock`):

```sh
kit=/workspace/x64-run/kit/latest
xbox-test/dp/install.sh "$kit/uwph.msix" "$kit/uwph.cer"
xbox-test/dp/launch.sh 'Joao.XboxSteam.Uwph_dk715arakrne0!App' 'Joao.XboxSteam.Uwph_0.1.6.0_x64__dk715arakrne0'
xbox-test/dp/wait-log.sh 'Joao.XboxSteam.Uwph_0.1.6.0_x64__dk715arakrne0' k32.log 'D3DT RESULT presented=300' 60 /tmp/uwph.log
```

## Install order and caveats

- **Order does not matter** between `uwph`, `wxl`, `w32u` and `vrx` — they are
  independent package families and do not conflict.
- **`wxl` needs a one-time guest upload.** Its first launch logs
  `WXL FAIL open` (expected — there's nothing in `LocalState` yet). Use
  `xbox-test/dp/put-file.sh <wxl PackageFullName> '\LocalState' <kit>/wxl-test.exe`,
  kill the app, relaunch, then check for `WXL RESULT entry=42 failures=0`. See
  `xbox-test/uwph/host/README.md`'s "Minimal PE mapper" section for the full
  sequence.
- **`launcher` (`Grok.XboxTest`) may already be installed and in active use**
  (QR login state, Steam session). Installing this kit's `launcher.msix` is
  an in-place update of that same package family — if its version already
  matches what's installed, Device Portal will report nothing to do; if it's
  older, this upgrades it. Do **not** uninstall/reinstall it if someone has
  an active signed-in session you don't want to clear; check its current
  version first via **Apps** list or `/api/app/packagemanager/packages`.
- **Do not uninstall any other package while verifying this kit.** This
  lane's acceptance does not require installing anything; the on-device
  steps above are for whoever (João) chooses to sideload and smoke-test.

## No private keys

Every `.cer` in the kit is the **public** self-signed test certificate used
only so Windows/Xbox will accept the matching `.msix`'s signature. The
private `.pfx`/`.key`/`.pem` used to produce each signature is never copied
into the kit; `verify-kit.sh` fails the whole kit if it ever finds a `.pfx`
anywhere under the kit directory.
