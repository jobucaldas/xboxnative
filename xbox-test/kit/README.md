# Test kit — Dev Mode sideload bundle

`build-kit.sh` builds the signed test packages and launcher into a unique
subdirectory of `out/kit/`. Each package has a matching public certificate;
private signing keys are never copied into the kit.

## Build and verify

Use the toolchains described in `xbox-test/uwph/host/README.md` and
`docs/xbox/REBUILD.md`: Python 3, OpenSSL, MinGW, LLVM, the UWP cross-build
SDK/CRT, `makemsix` and `osslsigncode`. Put tools on `PATH` and set
`UWP_CROSSBUILD` to the cross-build checkout. Individual builders may need the
additional setup documented in their directories.

```sh
bash xbox-test/kit/build-kit.sh /path/to/output/kit
bash xbox-test/kit/verify-kit.sh /path/to/output/kit/latest
bash xbox-test/kit/verify-kit-offline.sh /path/to/output/kit/latest
bash xbox-test/kit/check-install-guide.sh
python3 -m unittest discover -s xbox-test/kit -p 'test_*.py'
```

The build runs all three kit/guide verification scripts before updating the
`latest` symlink. Failures leave that symlink unchanged. Signature/hash checks,
manifest identity and publisher checks, executable presence and block-map checks
are off-device checks, **not console acceptance**. Full `xbox-test/smoke-all.sh`
and `xbox-test/tools/play-test.sh <appid>` through the launcher still apply to
on-device changes; an offline smoke run cannot replace them.

Configuration (all paths may be absolute):

- Output: first argument, then `KIT_OUT_ROOT`, then this checkout's `out/kit`.
- `KIT_SOURCE_ROOT`: source checkout for working directory and Git provenance;
  defaults to the checkout containing this script.
- `KIT_BUILD_ROOT`: root containing the `xbox-test/.../build.sh` builders;
  defaults to `KIT_SOURCE_ROOT`. This can select local build adapters. Adapters
  and alternate sources must be disclosed with build evidence; the recorded
  source commit alone does not certify them.
- `LAUNCHER_SOURCE_ROOT`: launcher checkout, defaulting to `KIT_BUILD_ROOT`.
  Set it to an empty string to intentionally omit the launcher. A nonempty
  missing path or a failed builder is an error, not a successful omission.
- `PATH`, `UWP_CROSSBUILD` and other individual-builder settings are inherited.

The explicit package list avoids silently shipping an incomplete bundle after a
failed auto-discovered builder. To add a package, add its required build and
`add_package` call, then update the guide and tests.

## Contents

Each of `uwph`, `wxl`, `hid`, `mlp`, `tls`, `swc`, `w32u`, `vrx` and, unless
explicitly omitted, `launcher` has `<name>.msix` and `<name>.cer`.

Additional files:

- `wxl-test.exe` and `swc-test.exe`: guests uploaded to the corresponding
  package's `LocalState` before its test can succeed.
- `tls-test.exe`, `tls-test.dll`, `lifecycle.dll`: upload all three to the TLS
  package's `LocalState`. Its native carrier DLLs stay inside the MSIX.
- `manifest.json`: identity and version read from the **built MSIX**, SHA-256
  for each package/guest, and source commit/branch.
- `VERSIONS.md`: generated version table belonging to this artifact, not a
  tracked snapshot of a developer's last build.

Regenerate or check a kit's version document without changing source files:

```sh
bash xbox-test/kit/sync-readme-versions.sh /path/to/kit/manifest.json
bash xbox-test/kit/sync-readme-versions.sh --check /path/to/kit/manifest.json
# Optional second argument selects another output document.
```

### Packages and expected results

| Package | Identity | LocalState log | Required result |
| --- | --- | --- | --- |
| `uwph` | `Joao.XboxSteam.Uwph` | `k32.log` | `K32 RESULT failures=0` and `D3DT RESULT presented=300 hr=0x00000000`; colored clears ending magenta |
| `wxl` | `Joao.XboxSteam.Wxl` | `k32.log` | After guest upload: `WXL RESULT entry=42 failures=0`; `WXL FAIL open` before upload is not a pass |
| `hid` | `Joao.XboxSteam.Hid` | `k32.log` | `HID-STUB-OK symbols=20 checks=23` and `HID RESULT failures=0` |
| `mlp` | `Joao.XboxSteam.Mlp` | `k32.log` | `MLP-PROBE-DONE` after uploading your own Megabonk PEs; mapping/import classification only, no game execution |
| `tls` | `Joao.XboxSteam.Tls` | `k32.log` | After guest uploads: `WXL TLS RESULT failures=0` and `LIFECYCLE RESULT failures=0` |
| `swc` | `Joao.XboxSteam.Swc` | `k32.log` | After guest upload: `SWC RESULT entry=42 failures=0`; 300 presents ending green |
| `w32u` | `Joao.XboxSteam.W32u` | `w32u.log` | `W32U RESULT failures=0`; 180 frames ending green |
| `vrx` | `Joao.XboxSteam.Vrx` | `vrx.log` | `vrx: all cases done`; inspect each case: A/B deliberately fail or except under AppContainer, C must report `OK value=42` |
| `launcher` | `Grok.XboxTest` | `boot.log` | QR/library screen; this alone is not evidence of playable games |

For the default manifests, publisher is `CN=grok-xbox-dev`, package-family
suffix is `dk715arakrne0`, application ID is `App`, and architecture is `x64`.
Full package name is `<Identity>_<Version>_x64__dk715arakrne0`; launch ID is
`<Identity>_dk715arakrne0!App`. Read versions from the kit, not this document.
Alternate manifests/publishers require their actual package identifiers.

## Install and collect evidence

Follow [INSTALL.md](INSTALL.md) for Device Portal commands, guest uploads and
log collection. Configure the console address, credentials and network path for
your environment using `xbox-test/dp/README.md`. Device Portal scripts take the
console lock themselves; do not wrap them in another `flock`.

Packages have separate identities and can coexist. **Do not uninstall the
launcher to update it**: that can erase Steam login state. Install over the same
identity with a newer version. Inspect the installed version before reinstalling
and follow the repository's version-bump requirement.

Before sharing evidence, remove account details and review logs/screenshots.
Never include credentials, session files, private signing keys or game binaries.
