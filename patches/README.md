# patches/

Patch files applied by `src/tools/fetch-dep.sh` on top of a pinned dependency, in `patches/<dep>/*.patch`.

`wine-src/0001-pe-rodata.patch` is applied by `src/tools/build-wine-donors.sh` to
the pinned Wine 10 source before building diagnostic donors. It emits PE constant
data into a read-only `.rdata` section instead of GNU MinGW's writable orphan
`.rodata`. It does not strip load configurations, change their fields or disable
security features. The build cache includes the source and recipe/patch digests;
`SOURCE.txt` and the distributed patch identify the modified corresponding source.
The patch retains Wine's LGPL-2.1-or-later licence; see `NOTICE`.
