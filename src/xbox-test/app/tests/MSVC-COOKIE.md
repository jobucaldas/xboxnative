# Explicit MSVC x64 cookie fixture mode

`wxl::load(..., mapped, wxl::LoadConfig::msvc_x64_cookie)` is an explicit,
experimental **owned-fixture-only** mode in the app mapper. It is not enabled
by `wxl::run`, launcher Play, or the UWPH mapper. The default remains `reject`;
`inert_wine` still requires an exactly zero-bodied 0x140 record and rejects
**every** loader write into that record.

## Accepted contract

The new mode accepts only AMD64, no `GUARD_CF`, a read-only/non-executable,
raw-backed and virtually backed 0x140 load-config record. Every byte except
`Size` and these eight pointer fields must be zero:

- `0x58`: a naturally aligned, readable/writable, non-executable, raw-backed
  default x64 security cookie (`0x2b992ddfa232`).
- `0x70`, `0x78`, `0x118`, `0x120`, `0x128`, `0x138`: separate readable,
  non-writable, non-executable slots pointing to raw-backed executable code.
- `0x130`: a separate read-only/non-executable, zero-valued inactive CastGuard
  slot.

The config pointers and the six nonzero slot values each require exactly one
DIR64 relocation. Missing, incorrect, duplicate, overlapping or straddling
relocations are rejected. Inactive slots are verified, **never initialized or
rewritten** beyond their declared ordinary image relocation. There is no CFG,
XFG, CastGuard, memcpy-dispatch or security-policy implementation here.

The pure validator builds a FILE-derived relocation/import/TLS plan before
allocation or loader mutation. It requires both raw and virtual bounds for
loader-owned spans, rejects raw aliases/overlapping sections/holes in those
spans, and rejects cookie aliases with the record,
slots, ordinary/delay IAT writes (including terminators), TLS directory/index/
template/callback array, and every relocation destination. Imports are bound
from copied names and destinations, not mapped mutable metadata. The mode is
bounded to 96 sections, 256 descriptors per import directory, 4096 total imports,
a 1 MiB relocation directory, 65536 DIR64 relocations, and 255 TLS callbacks.
Unsupported CLR and non-RVA/auxiliary delay-import layouts are rejected.

After relocations, pointers are verified against the file plan. Before the
**first import resolver call** (which may itself execute a guest dependency),
`BCryptGenRandom(BCRYPT_USE_SYSTEM_PREFERRED_RNG)` supplies a 48-bit cookie.
At most eight attempts are allowed; zero, the default, RNG failure or exhaustion
fail closed, with no clock/PID/address/CRT fallback. The loader never guesses
or writes the CRT complement location. The real CRT sets its complement when
it sees that the loader has already replaced the default cookie.

Separate `prepareTls` calls use retained file-derived bounds/addresses and
reject mutated headers, config, guard pointers or TLS directory bytes. They
cannot acquire new authority from a forged writable section or copied TLS
index. Existing W^X protections and unwind registration remain required.

**Not supported:** records with `GuardFlags=0x100`, nonzero VolatileMetadata,
CFG tables or any other nonzero/unknown requirements. In particular, this mode
does not enable the diagnosed owned game records. It also supplies no genuine
Steam client/process/IPC functionality and makes no gameplay claim.

## Semantics and tests

The structural offsets follow `IMAGE_LOAD_CONFIG_DIRECTORY64` in the Windows
SDK and pinned Wine 10.0 `include/winnt.h`. The loader sequence follows pinned
Wine 10.0 `dlls/ntdll/loader.c`: `perform_relocations` (2219),
`update_load_config` (2233), then import fixup (2235 onward);
`set_security_cookie` (2067–2090) and `update_load_config` (2097–2120) establish
the loader cookie contract. The entropy source here is intentionally stronger
and bounded, rather than copying Wine's random implementation.

Run from the app directory with the existing cross-build toolchain and Wine:

```sh
bash tests/msvc-cookie.sh
bash tests/load-config.sh
```

The first script builds a pure native C++ assertion test, an owned MSVC-ABI
static-CRT DLL and an owned native TLS carrier, then includes the **actual app
mapper** in its Wine test. The fixture observes the nondefault cookie at import
resolution, TLS, and pre-CRT entry; references the genuine CRT cookie/complement
symbols directly; checks the complement after CRT initialization; and exercises
static-CRT throw/catch with registered unwind metadata. Its test-only resolver
recognizes only that owned image for `RtlPcToFileHeader` and the fixture's finite
kernel32/observer import surface. That resolver is not used by Play.

File mutation tests assert rejection before allocation/RNG/resolver/callbacks;
RNG and missing-import failures assert cleanup with no guest callbacks. Later
TLS preparation is tested with both intact and mutated mapped metadata.
These are **offline tests only**, not substitutes for the full console smoke
run or launcher acceptance test. No generated DLLs, raw captures or private
validation tooling belong in the repository.
