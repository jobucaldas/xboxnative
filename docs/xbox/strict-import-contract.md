# Strict import resolver contract (review proposal)

This is a proposal for the next implementation unit, not a resolver implementation or a passing test. Current mapper ownership remains with k32.

## Observed behavior

- `xbox-test/uwph/host/wxl_mapper.h:26` defines `Resolver` as `uintptr_t (*)(const char*,const char*, WORD)`: null name denotes ordinal; zero means unresolved. It carries no provenance/callability classification.
- `wxl_mapper.h:162-180` decodes named versus ordinal imports, calls the resolver, and fails closed with `WXL FAIL unresolved builtin` on zero. Delay imports similarly fail with `WXL FAIL unresolved delay import` at `:193-205`.
- `xbox-test/uwph/host/mlp_probe.h:57-121` resolves system exports through restricted module lookup, UWP assets/builtins, named route tables, then explicitly categorized fallback providers. It logs `MLP ROUTE ... provider=... abi=named-x64`, increments stub and resolved counts separately, and logs `MLP MISSING <dll>!<symbol> route=<kind>` before returning zero. However the raw nonzero address contract itself cannot prevent callers using a probe stub as guest code.
- `xbox-test/uwph/host/mlp_probe.h:109-110` admits `entry_stubs.dll` results as `named-stub`; `xbox-test/uwph/host/STUBS.md:3-6,43` says these are nonexecuting probe routes. `mlp_probe.h:126-138` says probe-only/no entry. `xbox-test/app/launch.h:66-84,119-120,140-144` explicitly records no entry, `gameCodeExecuted=false`, and that missing imports may be stubbed. These addresses cannot be execution targets.
- The k32 worktree diff (`wt/k32/xbox-test/uwph/host/wxl_mapper.h`) adds `Image::close()` and calls it after `run`; it does not change the resolver signature. This was inspected read-only.

## Required contract

1. A named request is `(dll, exact case-insensitive DLL identity, non-null exact symbol)`; ordinal is `(dll, null symbol, nonzero ordinal)`. Preserve the distinction in diagnostics as `DLL!Name` versus `DLL!#N`.
2. A successful resolution must provide a nonzero address and a host-supplied `callable` classification whose target is a real test-host function with the declared x64 ABI. Resolver status must not treat a `named-stub`, empty service, or probe placeholder as callable.
3. Missing name/ordinal, zero ordinal, provider-load failure, or a noncallable route fails closed. Emit deterministic `IMPORT FAIL dll!symbol reason=<missing|provider-load|probe-only|invalid-request>`; do not patch the IAT with a substitute address.
4. The image is not eligible for entry, DllMain, or TLS invocation unless every required import resolves as callable. On any unresolved request the load result is failure; the fixture must observe its entry marker remains absent. This is a test contract; current probe mode intentionally never enters images.

## Bounded fixture proposal

The implemented fixture set uses no-CRT DLLs with explicit DllMain, named and ordinal imports, host callbacks, and missing name/ordinal variants. A fixture-only resolver maps only these exact imports to real host functions. The positive attach path calls both imports and records a marker; negative attach paths place a marker call before the missing import, and the harness requires the exact resolver failure and zero marker calls. Never use `entry-stubs.dll` as a guest execution provider.

Implemented fixture build and runtime command (off-device only; Wine 11 desktop API aliases):

```sh
bash xbox-test/uwph/host/tests/run-strict-imports.sh --run
```

The command builds no-CRT DLLs with explicit DllMain and executes them through wxl::load under Wine. It validates import binding and fail-before-entry for synthetic test-host functions only; it does not validate probe provenance, production resolver integration, UWP FromApp semantics or Xbox behavior.

## Smallest next implementation unit

Next integration unit: agree with mapper owner on a resolver result type separating `callable` from `probe_only`; current `Resolver` returns only address-or-zero, so this test does not prove production probe stubs are excluded from execution. This Wine fixture pass also does not establish UWP FromApp semantics or Xbox acceptance.
