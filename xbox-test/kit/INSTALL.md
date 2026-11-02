# Installing the test kit in Xbox Dev Mode

Use only your own console with Dev Mode and Device Portal enabled. Kit versions
change: read identity/version from `manifest.json`, not a copied example.
`bash xbox-test/kit/check-install-guide.sh` checks guide paths and log markers;
it does not prove that a package works on the console.

## Prepare and install

1. Build with `bash xbox-test/kit/build-kit.sh <output-root>`. Before sideloading,
   `bash xbox-test/kit/verify-kit.sh <kit>` and
   `bash xbox-test/kit/verify-kit-offline.sh <kit>` must both print `RESULT: PASS`.
   `<kit>` is a completed build directory, or the output root's `latest` symlink.
2. Configure Device Portal address, authentication and network access using
   `xbox-test/dp/README.md`. Supply `DP_PASS` through your local secret setup;
   never put it in a command transcript, committed file or log. Override
   `DP_HOST`/`DP_SOCKS` for your network rather than using another developer's
   address. Device Portal setup reference:
   <https://xboxoneresearch.github.io/wiki/development/dev-portal-api/set-up-device-portal/>.
3. Each package has `<name>.msix` and matching public `<name>.cer`. Names:
   `uwph`, `wxl`, `hid`, `mlp`, `tls`, `swc`, `w32u`, `vrx`, and `launcher`
   unless explicitly omitted at build time. Install each with
   `bash xbox-test/dp/install.sh <kit>/<name>.msix <kit>/<name>.cer`, waiting for
   `INSTALL OK`. The separate test identities may be installed in any order.
   Installing does not launch them.
4. **Do not uninstall `launcher` (`Grok.XboxTest`) to update it:** it holds Steam
   login state. Bump the package version and install over the same identity.
   Follow `docs/xbox/REBUILD.md` for package setup. In Dev Home, set the app type
   to Game when required for the larger memory limit; the UI sequence varies
   with console software. Do not use `xbox-test/dp/uninstall.sh` for this guide.
5. Read the package's identity/version from `manifest.json`. For the default
   manifests, full name is `<Identity>_<Version>_x64__dk715arakrne0` and launch ID
   is `<Identity>_dk715arakrne0!App`. With alternate manifests, use the actual
   architecture, publisher-family suffix and application ID.

Browser installation is an alternative, but menu names and certificate upload
behavior vary. If the browser flow rejects the self-signed package, use the
script route, which uploads the matching public certificate with the package.

## Upload guests and launch

1. Upload `wxl-test.exe` to the WXL package:
   `bash xbox-test/dp/put-file.sh '<wxl full name>' '\LocalState' <kit>/wxl-test.exe`.
   Without it, `WXL FAIL open` is expected but is **not** a successful WXL test.
2. Similarly upload `swc-test.exe` to the SWC package using
   `bash xbox-test/dp/put-file.sh '<swc full name>' '\LocalState' <kit>/swc-test.exe`.
   The source sequence is in `xbox-test/uwph/host/run-swc.sh`.
3. For TLS, upload all three guests to the TLS package's `LocalState`:
   `tls-test.exe`, `tls-test.dll`, `lifecycle.dll`. Use
   `bash xbox-test/dp/put-file.sh '<tls full name>' '\LocalState' <kit>/tls-test.exe`
   and repeat with each DLL. The native TLS carrier DLLs are already packaged;
   they are not substitutes for these uploads.
4. MLP requires `Megabonk.exe`, `UnityPlayer.dll`, `baselib.dll` from your own
   legitimate Steam install in that package's `LocalState`. They are not in the
   kit. See `xbox-test/uwph/host/run-mlp.sh`; this is a mapping probe, not game
   execution or a substitute for launcher acceptance.
5. If running, stop the app with `bash xbox-test/dp/kill.sh '<full name>'` and
   confirm with `bash xbox-test/dp/ps.sh`. Kill's HTTP status alone is unreliable.
   Launch using
   `bash xbox-test/dp/launch.sh '<Identity>_dk715arakrne0!App' '<full name>'`.
   Optional `bash xbox-test/dp/user-signin.sh` can report HTTP 400 when already
   signed in; check the actual launch result rather than treating that as proof.
6. Wait for the required log lines with
   `bash xbox-test/dp/wait-log.sh '<full name>' <logfile> '<pattern>' 60 /tmp/out.log`.

| Package | Log file | Required markers | Expected display |
| --- | --- | --- | --- |
| `uwph` | `k32.log` | `K32 RESULT failures=0` and `D3DT RESULT presented=300 hr=0x00000000` | colored frames ending magenta |
| `wxl` | `k32.log` | `WXL RESULT entry=42 failures=0` | host window |
| `hid` | `k32.log` | `HID RESULT failures=0` and `HID-STUB-OK symbols=20 checks=23` | host window |
| `mlp` | `k32.log` | `MLP-PROBE-DONE` | host window; no game execution |
| `tls` | `k32.log` | `WXL TLS RESULT failures=0` and `LIFECYCLE RESULT failures=0` | host window |
| `swc` | `k32.log` | `SWC RESULT entry=42 failures=0` | 300 frames ending green |
| `w32u` | `w32u.log` | `W32U RESULT failures=0` | 180 frames ending green |
| `vrx` | `vrx.log` | `vrx: all cases done`; inspect `caseC` for `OK value=42` | heartbeat frame; A/B intentionally fail or except |
| `launcher` | `boot.log` | QR/library screen reached | login/library; not game playability |

## Collect and review evidence

1. List files with `bash xbox-test/dp/ls-dir.sh '<full name>' '\LocalState'`.
2. Download a log with
   `bash xbox-test/dp/get-file.sh '<full name>' '\LocalState' k32.log /tmp/k32.log`.
3. Capture the display with `bash xbox-test/dp/screenshot.sh /tmp/shot.png`.
4. Review logs and images before sharing: remove account details and never share
   credentials, launcher session files or game binaries. Crash dumps may contain
   sensitive data; `xbox-test/dp/logs.sh` can list them, but download support is
   not established by this guide.
5. Record which package/guest actually ran. Off-device kit checks, mapping probe
   completion and a non-black launcher do not replace full console smoke or
   `xbox-test/tools/play-test.sh <appid>` through the launcher.

For an unsigned/untrusted-package error, re-run both kit verification scripts
before attempting another install. If an app exits after launch, see
`docs/xbox/REBUILD.md`; do not loosen test assertions or count a missing guest as
success. Console access is shared: the Device Portal scripts acquire the lock.
