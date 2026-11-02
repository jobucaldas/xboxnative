# Wine PE donor experiment

The launcher builds five diagnostic donor DLLs (`avrt`, `hid`, `setupapi`,
`shcore`, `version`) from the `wine-src` source archive in `deps.pin`, using
`tools/build-wine-donors.sh`. The tracked `patches/wine-src/0001-pe-rodata.patch`
corrects PE constant-data section flags for GNU MinGW builds; it does not alter
load-config fields or remove security requirements. No DLLs are committed or copied from a system Wine
installation. Wine is LGPL-2.1-or-later; retain `LICENSE`, `COPYING.LIB`, and the
corresponding source URL when distributing the built DLLs.

The source builder writes the DLLs, `SHA256SUMS` and `SOURCE.txt` into ignored
`out/wine-donors/`. The app build verifies that provenance against the selected
Wine source pin, the selected source patch and every DLL digest. Inspection also
rejects incompatible load configurations before packaging. `inspect.py` then generates the
complete direct/delay import table in the app's ignored object directory:

```sh
bash tools/build-wine-donors.sh
python3 -I xbox-test/app/donors/inspect.py \
  --output xbox-test/app/out/obj/donor_imports.generated.h
python3 -m unittest discover -s xbox-test/app/donors -p 'test_*.py'
```

Inspection requires Python's `pefile` package. `app/build.sh` performs the build
and generation automatically and packages the DLLs with their licence and source
metadata. Generated tables and binaries must not be committed.

The strict probe resolves every required dependency before initialization. It
never uses MLP failure placeholders. Mapping alone is insufficient: each donor
must initialize through the W^X/unwind mapper and pass its representative
behavior check before its exports are offered to the game-import probe. `avrt`
is checked only for the documented invalid-task failure; a scheduling backend
is not claimed. Game entry points remain uncalled by this diagnostic path.

Only the donor loader opts into Wine 10's inert x64 load-configuration record:
exactly 320 file-backed read-only bytes, with the size field set and every other
byte zero. Security cookies, CFG/XFG, dynamic relocations and unknown extensions
remain unsupported and rejected before allocation or entry. The general mapper
still rejects load configurations by default; no directory is stripped or
patched out of a donor. Test both this opt-in and rejection boundaries with
`bash xbox-test/uwph/host/tests/run-load-config.sh`.

Validate a fresh console log with:

```sh
python3 xbox-test/app/donors/check.py /path/to/private/launch_probe.log
```

The checker requires all five executed behavior checks, exactly 22 donor IAT
routes and a historical STUB/MISSING surface of at most 40. These gates are not
relaxed for a rebuilt donor. Old evidence for Debian Wine binaries does not
certify a current source-built package. Keep raw logs, accounts and captures
private; record exact source/package identity and the actual result in the PR.
Passing this donor diagnostic is not launcher/game-playability acceptance.
