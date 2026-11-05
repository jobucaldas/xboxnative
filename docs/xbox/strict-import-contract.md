# Strict import resolver contract and fixture scope

PR #6 provides a synthetic off-device fixture, not production provenance enforcement.
The contract below remains an integration requirement. Mapper ownership remains with k32.

## Observed behavior on this branch

- `src/xbox-test/uwph/host/wxl_mapper.h` defines `Resolver` as
  `uintptr_t (*)(const char*, const char*, WORD)`: null name denotes an ordinal;
  zero means unresolved. A nonzero address carries no callability classification.
- `wxl::load` decodes ordinary name/ordinal imports and rejects a zero resolver
  result with `WXL FAIL unresolved builtin`. RVA delay imports are fixed only in
  mapped probe mode and reject zero with `WXL FAIL unresolved delay import`.
  Execution mode rejects the delay-import runtime directory on this branch.
- `src/xbox-test/uwph/host/mlp_probe.h` resolves restricted system exports, mapped
  assets/builtins and named route tables, then categorized fallback providers.
  Its `MLP ROUTE ... provider=... abi=named-x64` diagnostics and separate stub
  counts are observations, not execution authorization. The raw nonzero result
  cannot prevent an execution caller from accepting a probe target.
- `mlp::resolve` admits `entry-stubs.dll` exports as `named-stub` and HID fallback
  exports as `named-stub-hid`. `host/STUBS.md` labels these probe routes, not
  implementations of the required services. An exported address or known x64
  signature does not make a placeholder safe or semantically valid for execution.
- `mlp::run` uses mapped mode. `wxl::load` returns from that mode without
  initializing TLS/unwind or calling entry/DllMain. `src/xbox-test/app/launch.h`
  records probe-only behavior and `gameCodeExecuted=false`. Neither probe
  completion nor the fixture pass establishes arbitrary guest/Megabonk execution.

## Required execution contract (not implemented by this PR)

1. A named request is `(DLL identity, non-null exact symbol)`; DLL identity is
   case-insensitive. An ordinal request is `(DLL identity, null symbol, nonzero
   ordinal)`. Preserve `DLL!Name` versus `DLL!#N` in diagnostics.
2. A successful execution resolution supplies a nonzero address and an explicit
   host-supplied `callable` classification for an approved implementation with
   the declared x64 ABI. Missing, probe-only, unknown classifications and
   callable-with-zero results must fail closed. A named stub or empty service
   must not be relabeled callable merely because it exports the right symbol.
3. Missing name/ordinal, ordinal zero, provider-load failure and noncallable
   routes must not receive substitute IAT targets. The proposed diagnostic is
   `IMPORT FAIL dll!symbol reason=<missing|provider-load|probe-only|invalid-request>`.
   This branch's fixture emits only `reason=missing`; it does not implement the
   complete proposed diagnostic taxonomy or ordinal-zero enforcement.
4. Every required import must be callable before entry, DllMain or TLS callbacks.
   Apply the policy to ordinary and supported delay imports. Mapping-only probe
   results must remain separate from execution and must not authorize later entry.

## Implemented bounded fixture

`host/tests/strict-imports/` contains source-only synthetic fixtures. The three
mapped guest DLLs use no CRT and explicit DllMain entrypoints, with a volatile
entry anchor to force relocations. The separately built `strict-provider.dll`
is inspected for its export table; the harness does **not** load or execute that
provider. Instead, its fixture-only resolver supplies real test-host functions.
No donor or game binary is an input.

The runner checks named `fixture_named` and ordinal-only #7 PE tables, missing
`absent_name` and #9 imports, absence of those negative exports from the provider,
and nonempty guest relocation directories. Directory inspection alone is not a
runtime relocation test; the mapper additionally requires exercised DIR64
relocations on the successful execution path.

The harness requires:

- Positive: one call each to the named and ordinal host functions, one DllMain
  marker with value 33, successful load, no resolver misses or mapper rejection.
- Each negative: exactly the expected missing request, exactly one resolver miss
  and `WXL FAIL unresolved builtin`, failed load, and zero host-function/entry
  marker calls. The negative DllMain bodies put their marker before the missing
  function call, so entering them cannot be counted as successful rejection.

These are ordinary-import and DllMain tests. The fixture has no TLS callbacks;
zero marker calls do not prove the broader TLS or delay-import execution gates.
The word `callable` in fixture helper names is not a production classification.
Never use `entry-stubs.dll` as a guest execution provider.

## Reproducing off-device checks

Use LLVM-MinGW or MinGW GCC plus LLVM tools on PATH, and Wine for runtime
execution. `LLVM_MINGW_ROOT` optionally prepends a toolchain's `bin` directory.
The runner falls back from MinGW Clang to MinGW GCC and uses `llvm-objdump`
(the table assertions require LLVM's output format). Individual executables can
be selected with `STRICT_CC`, `STRICT_CXX`, `STRICT_OBJDUMP`, `STRICT_DLLTOOL`,
and `STRICT_LINKER`. Build-only mode does not need Wine or a prefix:

```sh
LLVM_MINGW_ROOT=/absolute/path/to/llvm-mingw \
  bash src/xbox-test/uwph/host/tests/run-strict-imports.sh --build-only

LLVM_MINGW_ROOT=/absolute/path/to/llvm-mingw \
WINE_BIN=/absolute/path/to/wine \
WINEPREFIX=/absolute/path/to/dedicated-win64-prefix \
  bash src/xbox-test/uwph/host/tests/run-strict-imports.sh --run
```

`WINE_BIN` defaults to `wine` on PATH. `WINEPREFIX` is respected when supplied;
otherwise the runner uses ignored `host/out-strict-imports/wine-prefix`, not an
individual worker's shared prefix. Outputs stay in ignored `out-strict-imports`.
The desktop harness aliases `VirtualAllocFromApp`/`VirtualProtectFromApp` to
`VirtualAlloc`/`VirtualProtect`; **no UWP FromApp behavior is exercised**.

## Integration and acceptance boundaries

This fixture changes no production mapper, probe, launcher, or console gate.
When integrating it with lifecycle cleanup or execution-policy work, retain both
sets of regressions: import rejection does not establish resource cleanup, and
an isolated classification unit test does not establish production enforcement.
Execution-policy integration must additionally cover delay imports, ordinal zero,
noncallable providers, TLS initialization and entry gating. Do not substitute the
fixture's test-host functions for production providers.

`bash src/tools/check-uwp-build-env.sh` checks tool availability, not SDK/CRT usability,
package validity or host execution. Even `RESULT TOOLS PRESENT ONLY` is not
acceptance. Changes to on-device code still require a matching source-built
host/TLS package and the full `src/xbox-test/smoke-all.sh` console run. Launcher
acceptance is `src/xbox-test/tools/play-test.sh <appid>`; report actual guest execution
separately from a probe or launch attempt. Desktop Wine's FromApp aliases and
historical lane results cannot replace those checks. Use the shared console lock
and advance package versions before reinstalling, as required by `AGENTS.md`.

Record tested source/package identities, current review status and evidence
summaries in the PR rather than keeping machine-specific checkpoints here.
