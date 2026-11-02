# Strict import resolver contract and fixture scope

## Implemented host-mapper boundary

The host mapper in `xbox-test/uwph/host/wxl_mapper.h` separates two resolver
interfaces:

- `Resolver` returns an address for mapped, nonexecuting probes only.
- `ExecutionResolver` returns a `strict_policy::Decision` for executable loads.
  `may_call` accepts only an explicit `callable` classification with a nonzero
  address. Missing, probe-only, unknown classifications and callable-with-zero
  results fail closed before TLS callbacks or DllMain.

The built-in execution exports remain an explicit host whitelist. A named stub,
empty service, exported address or known x64 signature does not by itself make a
provider callable. Production resolver owners must supply genuine behavior and
must not relabel probe placeholders as executable implementations.

Ordinary imports and supported RVA-based delay imports pass through the same
execution-policy check. Delay IAT binding is eager, before guest initialization.
Ordinal zero is rejected before either resolver is called, including probe mode.
Named requests preserve exact symbols; DLL identities are case-insensitive.
Diagnostics preserve `DLL!Name` versus `DLL!#N`:

- Ordinal zero: `IMPORT FAIL dll!#0 reason=invalid-request`.
- An execution request without an approved target:
  `IMPORT FAIL dll!symbol reason=noncallable`.
- The fixture resolver additionally records its exact missing request as
  `IMPORT FAIL dll!symbol reason=missing`.

This is not a complete provider-load diagnostic taxonomy. It does not certify
all production providers, the app's separate mapper copy, arbitrary guest
execution or gameplay. `mlp::run` and the launcher's catchup-base Play path still
use mapped probes; the latter reports `gameCodeExecuted=false`. A successful
probe never authorizes later entry.

## Bounded synthetic fixtures

`host/tests/strict-imports/` contains source-only no-CRT DLLs with explicit
DllMain entrypoints and a volatile entry anchor to force relocations. The
separately built `strict-provider.dll` is inspected for its export table; the
harness does **not** execute that provider. Its classified resolver supplies
real test-host functions instead. No donor or game binary is an input.

The runner retains the merged fixture checks for named `fixture_named` and
ordinal-only #7 imports, missing `absent_name` and #9 imports, absence of those
negative exports from the provider, and nonempty guest relocation directories.
Directory inspection is not a runtime relocation test; successful executable
mapping also requires exercised DIR64 relocations.

The harness requires:

- Positive ordinary imports: exactly one actual call to each named and ordinal
  host function, one DllMain marker with value 33, one binding of each import,
  and no resolver miss or mapper rejection.
- Each missing ordinary import: exactly the expected missing request, one
  resolver miss, one mapper policy rejection, failed load and no host-function
  or entry-marker call. The negative DllMain bodies place their marker before
  the missing function call, so entering them cannot count as rejection.
- Nonzero probe-only targets, unknown classifications and callable-with-zero
  targets: failed load before entry.
- A separate no-CRT TLS fixture with a constructed RVA delay directory: named
  and ordinal bindings succeed before one TLS callback and one DllMain attach;
  probe-only, unknown, zero and missing delay decisions fail before both.
  Binding these delay imports is not proof that the guest invokes them.
- Ordinary and delay ordinal-zero fixtures: rejection before resolver dispatch,
  TLS callbacks and DllMain, with cleanup, in execution and mapped-probe modes.

Never use `entry-stubs.dll` as a guest execution provider. These controlled
fixtures validate the classification boundary, not the provenance or behavior
of every future resolver implementation.

## Reproducing off-device checks

Use LLVM-MinGW or MinGW GCC plus LLVM tools on PATH, Wine for runtime execution,
and Node.js for the synthetic delay/ordinal-zero fixture generator.
`LLVM_MINGW_ROOT` optionally prepends a toolchain's `bin` directory. Individual
executables can be selected with `STRICT_CC`, `STRICT_CXX`, `STRICT_OBJDUMP`,
`STRICT_DLLTOOL`, `STRICT_LINKER` and `WINE_BIN`. Build-only mode needs neither
Wine nor Node.js:

```sh
bash xbox-test/uwph/strict-policy/tests/run-policy-test.sh
bash xbox-test/uwph/host/tests/run-strict-imports.sh --build-only
WINEPREFIX=/absolute/path/to/dedicated-win64-prefix \
  bash xbox-test/uwph/host/tests/run-strict-imports.sh --run
bash xbox-test/uwph/host/tests/run-lifecycle.sh
```

The runner falls back from MinGW Clang to MinGW GCC and uses LLVM's objdump table
format. `WINE_BIN` defaults to `wine`. A supplied `WINEPREFIX` is respected;
otherwise strict-import execution uses ignored `host/out-strict-imports/wine-prefix`.
The desktop harness aliases `VirtualAllocFromApp`/`VirtualProtectFromApp` to
`VirtualAlloc`/`VirtualProtect`; **no UWP FromApp behavior is exercised**.

## Integration and acceptance boundaries

The lifecycle baseline remains dependent on the separately owned PR #3. Merging
current fixture/policy work into this branch does not resolve that ownership or
replace its review and acceptance. Do not weaken either set of regressions:
import rejection does not establish resource cleanup, and a classification unit
test alone does not establish production enforcement.

`bash tools/check-uwp-build-env.sh` checks tool availability, not SDK/CRT usability,
package validity or host execution. Even `RESULT TOOLS PRESENT ONLY` is not
acceptance. On-device changes still require matching source-built host/TLS
packages and the full `xbox-test/smoke-all.sh` console run. Launcher acceptance is
`xbox-test/tools/play-test.sh <appid>`; report actual guest execution separately
from a probe or launch attempt. Historical lane/other-branch console results
cannot certify this source. Use the shared lock and bump package versions before
reinstalling, as required by `AGENTS.md`.

Record exact source/package identities, current review status and evidence
summaries in the PR, not machine-specific checkpoints in this guide.
