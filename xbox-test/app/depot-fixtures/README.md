# Synthetic depot hash vectors

Preparation only: these local `abc`/`abd` byte vectors model `DlFile.path`,
`size`, and `sha` from `xbox-test/app/steamdl.h:224-225`. The expected SHA1
for `abc` is `a9993e364706816aba3e25717850c26c9cd0d89d`; `abd` intentionally
retains that digest to model byte mutation. No owned depot, Steam request,
game content, downloader execution, or completion-marker behavior was tested.

The source currently hashes nonempty file bytes and compares the resulting
digest with `f.sha` at `steamdl.h:467-480`. The missing/empty-hash vectors
describe desired fail-closed test outcomes, not verified current behavior.
`steamdl.h:481-484` only writes the marker if opening succeeds but sets state
to done regardless; this fixture prep does not change that implementation.

Run `python3 xbox-test/app/depot-fixtures/validate-vectors.py` to validate the
JSON shape, case set, encoded bytes and declared digest expectations. This
checks fixture data only; it does not run the production downloader/verifier.
