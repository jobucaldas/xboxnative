Run from any directory (Python 3.9+ and bash):

```sh
xbox-test/dp/verify-depot.sh --local /workspace/steam-owned/games/megabonk
xbox-test/dp/verify-depot.sh --local /path/to/game --manifest files.json
xbox-test/dp/verify-depot.sh --package PACKAGE_FULL_NAME --remote '\LocalState\spacewar' --manifest files.json
# Downloads into a temporary host directory, removed after verification:
xbox-test/dp/verify-depot.sh --package PACKAGE_FULL_NAME --remote '\LocalState\spacewar' --manifest files.json --sha
```

Console mode only lists and downloads files using ls-dir.sh/get-file.sh. Those helpers
acquire /workspace/x64-run/CONSOLE.lock for each request; do not wrap this script
in another lock on the same file. Set DP_PASS in the environment as usual. Package
updates can change PackageFullName while running; rerun with the current name if
Device Portal says the package does not exist. Remote paths are relative to LocalAppData.

Manifest formats:

* JSON array or `{"files":[...]}` or `{"manifest":{"files":[...]}}`, entries
  `{"filename":"sub/file", "size":123, "sha_content":"40 hex SHA1"}`.
  `name`, `sha1`, and `sha256` are also accepted. Directory flag 64 is skipped.
* Standard sha1sum/sha256sum output (including the dl.js MANIFEST.sha256).
  These lists have no sizes: size_checked=0 explicitly reports that limitation.
* Saved ls-dir.sh output (`- SIZE filename`), for name/size comparisons.
  For nested files use paths relative to the app folder.

Local mode hashes every file with available manifest hashes. Console mode checks
names and available sizes; --sha requires hashes for every file and checks them
by downloading. Steam's all-zero SHA1 convention is accepted only for empty files.
A nonempty file with a zero hash fails verification. Conventional MANIFEST.sha1
and MANIFEST.sha256 sidecars and the selected local manifest are excluded from
extra-file reporting. Other extras fail verification. Paths and hashes are
validated before contacting the console. Output reports each expected file and
present/missing/bad/extra totals, size_checked and hash_checked. Exit status:
0 match, 1 mismatch/missing/extra, 2 invalid input or transport error. Transport
errors never become a misleading missing-files summary. No snapshot consistency
is guaranteed if a downloader is actively modifying the folder.

Offline regression checks: `python3 xbox-test/dp/test-verify-depot.py`.
