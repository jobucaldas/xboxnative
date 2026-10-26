# UWPH kernel host foundation

`./build.sh` produces a signed `out/k32.msix` with identity
`Joao.XboxSteam.Uwph` version `0.1.23.0` (publisher `CN=grok-xbox-dev`). Requires the local
uwp-crossbuild toolchain, makemsix, openssl and osslsigncode. Outputs and test
certificates are ignored. No external runtime dependency is required.

Use `./build.sh out/hid-variant hid` for the preserved `Joao.XboxSteam.Hid`
identity (0.1.1.0), or `./build.sh out/wxl-variant wxl` for
`Joao.XboxSteam.Wxl` (0.1.1.0). All variants contain the same tests.

The UWP executable loads five packaged x64 PE DLLs:

- `uwph-host.dll`: exports the dispatch-pointer and handle data symbols used
  by the spike. Implements 37 primitive opcodes using Windows APIs compiled
  under `WINAPI_FAMILY=APP`.
- `kernel-test.dll`: receives those two values and makes all tested calls
  through that pointer. It checks file contents and errors, thread completion,
  synchronization, TLS, clocks, and RW allocation -> RX protection -> code
  execution returning 42. Each assertion is logged to `LocalState/k32.log`.

- `d3dt-test.dll`: directly imports system `D3D11CreateDevice`, obtains the
  CoreWindow through `uwph_get_swapchain_target`, creates a native DXGI swapchain
  and presents 300 changing-color clears. Logs fps every 60 frames and requires
  exact S_OK for all presents. The final magenta frame remains visible.

- `uwph-hid.dll` and `hid-test.dll`: the empty legacy device backend and separate
  mingw caller; see [HID backend](../hid/README.md). Logs every export call and
  requires `HID-STUB-OK symbols=20 checks=23` with `HID RESULT failures=0`.

A complete run logs `K32 RESULT failures=0` and
`D3DT RESULT presented=300 hr=0x00000000`. Use `xbox-test/dp/install.sh`, `launch.sh`,
then `wait-log.sh` to collect results. Those scripts take CONSOLE.lock themselves.
Do not wrap them in another flock. Do not install over Grok.XboxTest.

## ABI and limits

`backend.h` defines **our private ABI v1**, not Wine's existing opcode table.
Calls are synchronous in-process; arguments and buffers must be valid for the
call. The DLL uses Windows x64 calling conventions, with no GS-stack switch,
ELF code, AF_UNIX socket or wineserver. This is trusted host glue, not a boundary
for untrusted pointers. The caller owns handles, memory and TLS slots.

The dispatcher returns NTSTATUS-shaped errors for invalid handle/opcode/args;
valid calls report native result and Win32 error in `Call`. RWX is refused;
write to RW memory then protect RX and flush the instruction cache. Process
creation returns zero and ERROR_ACCESS_DENIED without starting anything.
CreateFile uses CreateFile2 (the UWP equivalent of synchronous CreateFileW)
with normal attributes. The file operations remain subject to AppContainer
access rights; the test uses only its own LocalState directory.

**This does not load or run Wine's prebuilt kernel32/ntdll.** They have existing
Nt syscall, wineserver, loader, TLS and exception dependencies beyond the unix
call pair proved in the dispatch spike. Matching symbol names alone does not
satisfy those dependencies. The Wine adapter must translate its exact argument
structures/opcodes and NTSTATUS semantics to these primitives; the real Wine
loader and in-process wineserver remain integration work. The test PE is our
purpose-built DLL, not an unmodified third-party executable.

`uwph_wine10_unix_dispatcher` is a second exported entry point, taking handle
10 and the **actual Wine 10.0 ntdll opcode numbering**. It serves opcode 7
(`system_time_precise`, 100ns FILETIME ticks) and opcode 2 (`wine_dbg_write`,
returns byte count) through native APIs. Unsupported loader/unwind/server/fd/
spawn operations return STATUS_NOT_IMPLEMENTED. The declarations and ordering
were checked against `/workspace/ref/wine-10.0-src/wine-10.0/`:
`dlls/ntdll/unixlib.h`, `unix/loader.c:unix_call_funcs`,
`unix/sync.c:system_time_precise`, `unix/debug.c:unixcall_wine_dbg_write`.
This dispatcher uses Windows ABI throughout and replaces Wine's dispatcher;
it cannot be installed as a function-table entry behind Wine's original sysv
trampoline. Native Nt memory/thread/file syscalls are **not** members of this
eight-opcode table and still need a Wine-specific syscall bridge.

Example deployment for the current HID manifest version 0.1.0.0, from the repository root:

```sh
xbox-test/uwph/host/build.sh
xbox-test/dp/install.sh xbox-test/uwph/host/out/k32.msix xbox-test/uwph/host/out/test.cer
xbox-test/dp/launch.sh 'Joao.XboxSteam.Hid_dk715arakrne0!App' 'Joao.XboxSteam.Hid_0.1.0.0_x64__dk715arakrne0'
xbox-test/dp/wait-log.sh 'Joao.XboxSteam.Hid_0.1.0.0_x64__dk715arakrne0' k32.log 'K32 RESULT failures=0' 60 /workspace/x64-run/runs/k32/k32-final.log
```

The publisher determines the family suffix; query Device Portal's
`/api/app/packagemanager/packages` if using a different signing identity.

## In-process object server (lane/ws)

Version 0.1.4.0 adds `server.h`: an SRW-locked process-local handle table with
opaque, monotonically increasing IDs and typed event, mutex, semaphore, thread,
and file objects. A table entry owns a shared reference to a native AppContainer
kernel handle. Duplicate creates another table entry for the same object; close
removes one entry, and the last reference closes the native handle. An operation
pins its object before releasing the table lock, so a concurrent close cannot
invalidate a native call already in progress. Thread completion, mutex ownership,
and semaphore counts are maintained by the native kernel object. No helper
process, socket, IPC listener, or extra server executable is created.

All file-handle, thread, and synchronization opcodes now use this table.
`Op::Event` provides unnamed CreateEventW semantics through CreateEventExW:
`a[0]` contains CREATE_EVENT_MANUAL_RESET / CREATE_EVENT_INITIAL_SET flags.
SetEvent, ResetEvent, WaitForSingleObject and CloseHandle use the returned
opaque ID. Set/reset/release and file calls reject an incorrect object type.
Appends to ABI v1 preserve previous opcode numbers:

| Opcode | Arguments | Result |
| --- | --- | --- |
| Duplicate | a[0] source ID | new independent ID, or zero plus Win32 error |
| WaitMultiple | a[0] pointer to uintptr_t IDs, a[1] count, a[2] wait-all, a[3] milliseconds | native WAIT_* result |

Duplicate is the same-process, same-access, non-inheritable subset of
DuplicateHandle. There is no cross-process transfer, security descriptor,
named-object namespace, access-right reduction, or DUPLICATE_CLOSE_SOURCE flag.
WaitMultiple pins the whole set under one table lock and delegates atomic
wait-all/any, timeout and abandoned mutex behavior to WaitForMultipleObjectsEx.
Zero/oversize counts and repeated underlying objects are rejected. Handles
must never be passed directly to a native Windows API. This private ABI accepts
trusted in-process pointers; it is not a memory isolation boundary.

**Actual Xbox Series S evidence:** `ws-console-evidence.log`, package
`Joao.XboxSteam.Uwph_0.1.4.0_x64__dk715arakrne0`, PID 6776, 71 checks,
zero failures. Separate test PE's main thread and native worker synchronize via
an event duplicate whose original has been closed, then wait for completion
and thread termination together. It also checks stale/double-close rejection,
auto-reset consumption, wait-any index, wrong type, and bad wait count. All
52 inherited checks still pass. The off-device build and all three PE import
audits pass. Original logs, build output, audits and hashes are in
`/workspace/x64-run/runs/ws/`.

This implements the host's in-process object server, **not Wine's server request
protocol or unmodified kernel32 imports**. Wine10 server-call opcode 3 still
returns STATUS_NOT_IMPLEMENTED; integrating Wine's NT syscall/request adapter,
loader, TEB/APC behavior and broader object namespace remains future work.

## CoreWindow bridge

`uwph_set_swapchain_target(IUnknown*)` and
`uwph_get_swapchain_target(IUnknown**)` are private trusted in-process calls,
used only on the CoreWindow thread. The setter retains the target; the getter
returns an AddRef'd reference owned by the caller. Null getter output is rejected;
no configured window returns E_UNEXPECTED. This is a CoreWindow proof, with no
HWND translation or Wine D3D interface implementation. All graphics calls occur
in the separate PE, and its device/swapchain remain alive until app closure.
## Minimal PE mapper (lane/wxl)

This lane uses isolated identity `Joao.XboxSteam.Wxl`, version `0.1.0.0`, with
family `Joao.XboxSteam.Wxl_dk715arakrne0`. The publisher is unchanged. Earlier
Uwph package examples above refer to the inherited kernel/server foundation.

`wxl_mapper.h` implements a deliberately small loader, following Wine 10's
`dlls/ntdll/unix/virtual.c` (`map_image_into_view`,
`process_relocation_block`) and `dlls/ntdll/loader.c` (`import_dll`) sequence:
validate PE32+/AMD64 headers and nonoverlapping page-aligned sections, allocate
zero-filled RW memory, copy headers/raw sections, apply DIR64 base relocations,
resolve named import thunks through the explicit uwph builtin table, then
protect headers R, code RX, writable data RW, other readable sections R, gaps
NOACCESS. Every mapper VirtualProtectFromApp call logs RVA, size, old/new numeric
protection, result and error. It flushes the instruction cache before calling the
entry and frees the image after return. The initial RW allocation is logged too.
It never asks the operating system for executable writable memory.

`wxl_test.c` is separately compiled with MinGW and linked by lld-link without a
CRT, using only GetCurrentProcessId, GetTickCount and OutputDebugStringA imports.
The first two builtin entries use native UWP-compatible functions; the debug
entry routes to the host LocalState logger. A relocated volatile data pointer is
used by the entry to emit `WXL-OK <n>`; the entry returns 42. The test requires a
nonzero relocation delta and at least one applied relocation. The host retains
all 71 existing checks. Their `PASS RWX allocation refused` line tests the uwph
rejection guard; that deliberate ABI request never reaches VirtualAllocFromApp.
For actual mapper requests, `grep -E 'new=0x(40|80)( |$)'` must find nothing.

Reproduce from the repository root (each DP helper takes CONSOLE.lock itself):

```sh
xbox-test/uwph/host/build.sh
xbox-test/dp/install.sh xbox-test/uwph/host/out/k32.msix xbox-test/uwph/host/out/test.cer
pkg=Joao.XboxSteam.Wxl_0.1.0.0_x64__dk715arakrne0
app='Joao.XboxSteam.Wxl_dk715arakrne0!App'
xbox-test/dp/launch.sh "$app" "$pkg"
# First launch creates LocalState; without the file it logs WXL FAIL open.
xbox-test/dp/put-file.sh "$pkg" '\LocalState' xbox-test/uwph/host/out/layout/wxl-test.exe
xbox-test/dp/kill.sh "$pkg"
xbox-test/dp/launch.sh "$app" "$pkg"
xbox-test/dp/wait-log.sh "$pkg" k32.log 'WXL RESULT entry=42 failures=0' 60 /tmp/wxl-console.log
```

Actual Xbox Series S acceptance: `wxl-console-evidence.log`, PID 4788,
`WXL-OK 4791`, one relocation, three imports, six successful protection calls
(RW -> NOACCESS for the image, then header/section final permissions), zero
executable writable protection requests, entry=42, all 71 checks pass. D3D11
FL11_0, CoreWindow swap chain and first-frame heartbeat also succeed.

This is a trusted, purpose-built PE execution model, not the full Wine loader
or a parser security boundary. It rejects TLS, exception/unwind metadata, delay
imports, load-config and CLR directories; it supports only DIR64/ABSOLUTE
relocations and the three named kernel32 imports. There is no DllMain, loader
registration, ordinal/forwarder handling, unwind registration or general EXE
process startup/TEB environment. No Wine, Steam or game execution is claimed.
All execution evidence above is from the console, not off-device Wine.

## Merged host (lane/wxm)

The default build now packages Joao.XboxSteam.Uwph 0.1.6.0. It runs all 71
kernel/ws checks, the LocalState mapper, then the separate D3DT PE for 300
frames. Run `build.sh out/wxl-variant wxl` to build the same host under the preserved
Joao.XboxSteam.Wxl identity (0.1.1.0); its manifest is AppxManifest.Wxl.xml.
The Wxl 0.1.0.0 reproduction/evidence above is historical. Neither identity is
Grok.XboxTest. Upload wxl-test.exe to the selected package's LocalState before
acceptance. The repository smoke-all.sh uploads and verifies this guest, clears
stale logs, and checks WXL, k32/ws and D3DT under a single console lock.

### Mapped HWND swap-chain probe (Swc)

Build `bash xbox-test/uwph/host/build.sh out/swc swc`, then run
`SWC_INSTALL=1 bash xbox-test/uwph/host/run-swc.sh`. Subsequent fresh runs omit
`SWC_INSTALL`. This installs only `Joao.XboxSteam.Swc`, round-trips the guest
through LocalState, resets and verifies its log, and validates a fresh activation
before taking `/workspace/x64-run/runs/swc/swc.png`. Helpers share CONSOLE.lock.

The CRT-free `swc-test.exe` runs through the WXL mapper, creates its opaque HWND
through the existing w32u display driver, imports native D3D11, and imports a
routed DXGI factory. `swc_dxgi.h` implements the IDXGIFactory/1/2 ABI and translates
both HWND swap-chain methods into the real CoreWindow method. Legacy windowed,
single-sample DISCARD/SEQUENTIAL descriptors become flip sequential with at least
two buffers; unsupported fullscreen, flags or multisampling fail explicitly.
Each method presents 300 solid green frames and checks exact S_OK per Present.
Mapped pages are queried before entry and must contain zero RWX pages.

This is the initial trusted UI-thread factory-import route. Factory acquisition
through a native device/adapter's GetParent and newer factory interfaces remain
outside its scope. Swap chains retain native interfaces; this does not implement
all desktop HWND semantics on native swap-chain methods. It is preparation for
game rendering, not evidence of Unity or Steam compatibility. The mapped guest
and final D3D objects stay alive until the UWP process exits.
