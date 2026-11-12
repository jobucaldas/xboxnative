<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# x86 implicit TLS coverage matrix

Stage-3 slice: PE32 implicit TLS (data directory 9) in the x86 interpreter.
Loader change: `src/xbox-test/app/host/x86/x86_pe.h` (`Image::Tls`, `init_tls`,
hooked into `run_entry`). Test: `src/xbox-test/app/tests/x86-tls.sh` with the
fixture in `src/xbox-test/app/tests/x86-tls/`.

| # | Feature / API | Test | Status |
|---|---------------|------|--------|
| 1 | PE32 TLS directory parse (malformed = error, not silent skip) | x86-tls.sh (patched dir9 runs; a short/inverted directory throws in `load_pe32`) | interpreter PASS; Wine PASS (exit 42, checks=11 failed=0) |
| 2 | TLS block = fresh allocation: raw data copied + SizeOfZeroFill zeroed; not aliased with the image template | fixture-tls.c block checks (write block, template unchanged) | interpreter PASS; Wine PASS |
| 3 | TEB32.ThreadLocalStoragePointer (fs:[0x2C]) -> one-slot array; slot 0 = block | fixture fs_dword(0x2C) chain | interpreter PASS; Wine PASS |
| 4 | AddressOfIndex variable written with slot 0 | fixture `tls_index == 0` | interpreter PASS; Wine PASS |
| 5 | TLS callbacks run with (image base, DLL_PROCESS_ATTACH, NULL) before entry | fixture callback records calls/reason; negative control (dir9 zeroed -> fault) | interpreter PASS; Wine PASS |
| 6 | Negative control: without directory 9 the fixture faults (proves the path is exercised) | x86-tls.sh negative step | PASS (guest faults; Wine would fault identically) |
| 7 | Cross-check: same patched exe under a real loader | x86-tls.sh --wine (stdout + exit identical) | PASS (wine-11.0 wow64, off-device) |

## Honest gaps (still open)

- **`__thread` (emutls) is not supported.** The mingw i686 gcc on this machine
  lowers `__thread` to `__emutls_get_address` unconditionally (no
  `-mno-emutls`); a real game compiled with emutls needs that runtime, which
  the interpreter does not provide. The fixture therefore hand-writes the
  `.tls` section and IMAGE_TLS_DIRECTORY32. MSVC `/GT`-style implicit TLS
  (the directory path this slice implements) is covered; emutls is a separate
  mechanism and unimplemented.
- **Single-thread only.** The interpreter has one guest thread, so
  per-thread block allocation on `CreateThread` (slot N for a new thread,
  DLL_THREAD_ATTACH callbacks) is unimplemented and untested.
- **DllMain-unload and thread-detach reasons** (DLL_THREAD_DETACH,
  DLL_PROCESS_DETACH) are not delivered; there is no guest exit path for
  callbacks yet.
- **Console check (unverified)**: on-device, run a TLS-callback-exercising
  PE32 through the launcher's play path and confirm the game reaches its
  first frame with TLS initialized (coordinator runs it; console was
  off-limits this week).
