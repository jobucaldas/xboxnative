# X86 stage 3: guest DLL loading, the VC runtime for PE32 games, and the Proteus import gap

Date: 2026-11-12. Slice: x86 stage 3 of research/X86-32BIT-PLAN.md — guest DLL
loading (LoadLibrary/GetProcAddress/DllMain) and a from-source i686 build of the
VC runtime pieces PE32 games import. Console status: unverified (off-device
work; the console check to run is at the bottom).

## What existed before this slice

| Piece | Evidence | Status before |
|---|---|---|
| x86 interpreter core (ALU, x87, SSE/SSE2, thunks, guest callbacks) | src/xbox-test/app/host/x86/x86_cpu.h, tests x86-core.sh CI | done |
| PE32 loader: map at ImageBase, relocations, IAT binding to host thunks | src/xbox-test/app/host/x86/x86_pe.h | done (exe only; no DLL loading) |
| Guest OS surface: kernel32 + msvcrt allowlist with checked marshallers | src/xbox-test/app/host/x86/x86_guest.h | done — LoadLibraryA/W and GetProcAddress were explicit faults |
| mingw-CRT PE32 fixture runs in the interpreter | src/xbox-test/app/tests/x86-guest.sh CI | done |
| 64-bit VC runtime routes (ucrtbase/api-ms/msvcp140/vcruntime140) for native guests | src/xbox-test/app/host/play_runtime.h, play-msvcp/ play-vcrt tests | done for x64, not for PE32 |
| Wine donors from source (x86_64) | src/tools/build-wine-donors.sh | done for x64 only |

## What this slice adds

| Feature/API | Test that proves it | Status |
|---|---|---|
| `LoadLibraryA`/`LoadLibraryW` of a guest PE32 DLL: map at ImageBase, relocations, imports bound recursively, DllMain(DLL_PROCESS_ATTACH) runs | x86-dll.sh: fixture loads an owned DLL, DllMain side effect observed, exit 42 | PASS (off-device) |
| `GetProcAddress` against a guest module's export table (name and ordinal), including a host-registered export | x86-dll.sh: fixture resolves its own DLL's export by name and ordinal and calls it | PASS |
| `GetModuleHandleA/W` of a loaded DLL; `FreeLibrary` refcount (a real unload is not implemented and says so) | x86-dll.sh | PASS |
| DllMain(DLL_THREAD_ATTACH/DETACH) on the load-time thread only (no guest threads in this surface yet) | x86-dll.sh asserts the attach reason value | PASS |
| Import resolution between guest PE32 DLLs (DLL imports another guest DLL) | x86-dll.sh: chain exe -> dll-a -> dll-b, all guest PEs | PASS |
| From-source i686 VC runtime: ucrtbase, vcruntime140, msvcp140, concrt140 built from pinned Wine 10.0 source | src/tools/build-wine-runtime-i386.sh; deps.pin entry; --dry-run in CI pins check | PASS (built off-device) |
| An i686 C++ fixture using iostream/std::string/exceptions/threads runs in the emulator | x86-cxxrt.sh: 141/141 imports bound, exit 42, no unresolved, no fault; identical stdout and exit code under Wine | PASS (off-device) |
| Proteus.exe (Mega Man Legacy Collection) import gaps | pefile listing only (this file, below); no game files committed | documented |

## Honest gaps (still open after this slice)

- The committed C++ fixture links the mingw static C++ runtime (imports only
  kernel32/msvcrt/ntdll), so the interpreter run exercises the full C++
  stdlib path through the allowlisted surface. **Wine's i386
  ucrtbase/msvcp140** built from source were proven to load as guest DLLs
  (LoadLibraryA + GetProcAddress on `_initterm`, `_errno` and the forwarded
  `_o__errno` succeed, exit 42; probe not committed as a test) but a fixture
  linked against them is not committed: Wine's ucrtbase startup needs more
  kernel32 than the surface serves today.
- Guest DLL **unloading** (refcount to zero → unmap) is not implemented: the
  loader tracks refcounts and DllMain(DLL_PROCESS_DETACH) runs only at process
  exit, matching what a game's startup path needs.
- api-ms-win-crt-* import names resolve through a fixed table to ucrtbase
  (Wine's own apisetschema mapping); a real API-set schema is not parsed.
- No console evidence: everything here is off-device. The console check is at
  the bottom.

## Proteus.exe (Mega Man Legacy Collection) import gaps — pefile listing only

Source: the owned local install (never committed). Machine 0x14c (i386),
subsystem 2 (GUI). 173 imports in 19 DLLs.

| DLL | imports | Guest surface today | Gap |
|---|---|---|---|
| KERNEL32.dll | 31 | 17 on the allowlist | CreateDirectoryW, GetUserGeoID, FindClose, FindFirstFileA, FindNextFileA, IsDebuggerPresent, SetEvent, ResetEvent, WaitForSingleObjectEx, CreateEventW, GetModuleHandleW, GetProcAddress, InitializeSListHead (14) |
| USER32.dll | 23 | 0 | all 23 (window class/message box surface: RegisterClassExA, CreateWindowExA, PeekMessageA, DispatchMessageA, ...) |
| MSVCP140.dll | 17 | 0 (was: no PE32 route) | resolved by the Wine i386 msvcp140 built here (all 17 exports present, verified) |
| VCRUNTIME140.dll | 14 | 0 | resolved by the Wine i386 vcruntime140 (all 14 present, verified) |
| CONCRT140.dll | 4 | 0 | resolved by the Wine i386 concrt140 (all 4 present, verified) |
| api-ms-win-crt-runtime-l1-1-0.dll | 23 | 0 | resolve to ucrtbase per the apiset table; all 23 present in Wine i386 ucrtbase (verified) |
| api-ms-win-crt-stdio-l1-1-0.dll | 17 | 0 | same; all present (verified) |
| api-ms-win-crt-math-l1-1-0.dll | 5 | 0 | same; all present (verified) |
| api-ms-win-crt-utility-l1-1-0.dll | 1 | 0 | same; present (verified) |
| api-ms-win-crt-string-l1-1-0.dll | 7 | 0 | same; all present (verified) |
| api-ms-win-crt-heap-l1-1-0.dll | 7 | 0 | same; all present (verified) |
| api-ms-win-crt-time-l1-1-0.dll | 1 | 0 | same; present (verified) |
| api-ms-win-crt-locale-l1-1-0.dll | 1 | 0 | same; present (verified) |
| steam_api.dll | 12 | 0 | needs the 32-bit steam_api shim (plan §3.6; separate slice) |
| d3d11.dll | 1 | 0 | D3D11CreateDeviceAndSwapChain — the 64-bit D3D11 layer exists; needs a 32-bit COM boundary (separate slice) |
| VERSION.dll | 3 | 0 | GetFileVersionInfoSizeA/GetFileVersionInfoA/VerQueryValueA (donor candidate) |
| XINPUT1_3.dll | 2 (ordinals 2, 5) | 0 | ordinal-only imports: XInputGetState (2), XInputSetState (5)... actually #2=XInputGetState, #5=XInputGetDSoundAudioDeviceGuids per SDK headers; needs the xinput donor at i386 |
| SHELL32.dll | 1 | 0 | SHGetKnownFolderPath |
| ole32.dll | 3 | 0 (Wine i386 ole32 has all 3) | CoInitializeEx, CoCreateInstance, CoTaskMemFree |

Summary: of 173 imports, the runtime-DLL slice (UCRT api-ms sets 62, MSVCP140
17, VCRUNTIME140 14, CONCRT140 4 = 97) is addressable by the from-source i686
runtime built in this slice; the remaining 76 split into kernel32 (14 missing
from the allowlist), user32 (23), ole32 (3, Wine has them), version (3),
shell32 (1), xinput1_3 (2, ordinal), steam_api (12), d3d11 (1).

## Console check to run later (coordinator)

1. `src/xbox-test/tools/play-test.sh <mmlc-appid>` with the runtime DLLs staged
   beside the game: expect `PRUN PE32 engine=x86-interpreter`, then the
   unresolved list to show only steam_api/d3d11/user32/xinput gaps — not the
   api-ms/MSVCP140/VCRUNTIME140/CONCRT140 names.
2. The x86-dll/x86-cxxrt fixtures do not have a console entry point yet (see
   the console-verification-gap memory); adding one is a follow-up.
