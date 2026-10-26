# patches/

Patch files applied by `tools/fetch-dep.sh` on top of a pinned dependency, in `patches/<dep>/*.patch`.

Currently empty: the original fork (`jobucaldas/Boxedwine-xbox-64`, branch `x64-spike`) changes exactly one
upstream file, `.gitignore` (adds `/xbox-test/uwph/host/out-*/`), which is not needed here. Everything else in
the fork is new files (`xbox-test/`, `tools/xbox/`, `docs/xbox/`, `research/`), which live in this repo.
