The app mapper originated in `lane/tlm` commit `1e6b08d6`
(`src/xbox-test/uwph/host/`) and has subsequently diverged. It is not an
unmodified copy. Mapping-only loads do not directly run that image's TLS
callbacks or entry point; import resolvers may initialize guest dependencies.
Executable callers prepare TLS/unwind state before guest entry.

`wxl_mapper.h` follows Wine's image mapping, relocation and import-fixup sequence
without copying Wine code. The bounded, explicit MSVC x64 cookie fixture mode
uses the Wine 10.0 loader sequence pinned in `deps.pin` and SDK load-config
structure definitions. See [the fixture contract and tests](../tests/MSVC-COOKIE.md).
It does not enable launcher Play; default load-config rejection and the exact
zero-bodied `inert_wine` policy remain distinct.
