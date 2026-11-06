# VC++ runtime route (vcruntime140, vcruntime140_1)

Games built against the Visual C++ redistributable import `vcruntime140.dll`
(and, for x64 C++ built with VS2019 or later, `vcruntime140_1.dll`). Desktop
Windows gets them from the redistributable; the console has neither, so
`LoadLibrary` fails with error 126 unless the game ships its own copy.

`route.h` answers these imports without the redistributable:

- **Forwarded names** resolve to the console's own `ucrtbase.dll`. It is an OS
  component and exports the same exception-handling, RTTI and string
  functions. The list is Wine 10's `vcruntime140.spec` / `vcruntime140_1.spec`
  forward set, so one runtime and one per-thread exception state serve every
  module.
- **Owned helpers** cover what vcruntime implements itself:
  - the telemetry hooks (no-ops);
  - `__uncaught_exceptions`, using ucrtbase's `__processing_throw`;
  - `__vcrt_GetModuleHandleW`, `__vcrt_GetModuleFileNameW`,
    `__vcrt_LoadLibraryExW` and `__vcrt_InitializeCriticalSectionEx`, through
    the host's guest-aware kernel32;
  - `__report_gsfailure`, which raises `STATUS_STACK_BUFFER_OVERRUN` so the
    crash report sees it;
  - `unexpected`.
- **x64 `_CxxThrowException`** is owned too. ucrtbase's own version finds the
  throwing image with ntdll's `RtlPcToFileHeader`, which knows only OS-loaded
  modules. For a host-mapped game image that lookup returns 0, and the catch
  handler then faults on image-relative type RVAs. The owned version builds the
  same MSVC record (magic, object, ThrowInfo, image base) using the host's
  guest-aware `RtlPcToFileHeader`. WinRT throws stay with ucrtbase.
- Names that are stubs in Wine but may exist in a native ucrtbase (`__TypeMatch`,
  `_NLG_*` and others) are offered only when the running ucrtbase exports them.
  Any other name, and every ordinal import, stays unresolved. No placeholder is
  ever returned.

A game's own `vcruntime140.dll` in its directory is mapped instead, as on
Windows. A dynamic `LoadLibrary("vcruntime140.dll")` is not routed yet.
`msvcp140.dll` (the C++ standard library) is separate work.

Tests (off-device, Wine):

- `bash xbox-test/uwph/vcruntime/test.sh` checks the route contract: keys,
  forwarded and owned names, the native-only fallback, and the throw record's
  image base, magic and flags.
- `app/tests/play-runtime.sh` mode `native-vcrt` builds an owned `/MD` C++ DLL
  that imports both DLLs. It verifies through launcher Play that every IAT slot
  is bound to ucrtbase or an owned helper, and that throw/catch, rethrow,
  unwinding destructors, `__uncaught_exceptions`, `typeid`, `dynamic_cast` and
  `std::bad_cast` all work.

Under Wine, ucrtbase is Wine's. The console's ucrtbase export set still needs
checking on the console (see the `VCRT ROUTE ... found=` log lines).
