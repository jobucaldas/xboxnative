# Entry import routes (lane stubs)

This is a nonexecuting three-image import probe. A fixed-up IAT proves that an
export exists or a named adapter has the matching x64 ABI; it does not prove
Unity can initialize. No Megabonk/Unity/baselib entry, DllMain, or TLS callback
is invoked. Unknown imports fail mapping, without a generic fallback.

`mlp_probe.h` logs each IAT route with DLL, symbol/ordinal, category, and provider.
System resolution is restricted to System32/API sets. Kernel32 searches the
real kernel32 DLL as well as kernelbase and the runtime-support contract. The
old discovery omitted kernel32 itself; its ten missing exports were therefore
not evidence that the Xbox lacks those functions. lane/k32t removes the synthetic
computer name, zero startup parameters, fixed power state and unconditional
version/affinity/directory failures. Tail calls now reach the real OS exports.
Toolhelp enumeration validates the Wine-required structure size and translates
system exceptions for invalid snapshot handles into ERROR_INVALID_HANDLE.
See `xbox-test/shared/kernel_tail_test.h` for reversible synthetic host checks.

`entry_kernel.h` still overrides VirtualAlloc/VirtualProtect with FromApp calls
that refuse RWX and executable-copy-on-write. CreateProcessW returns FALSE,
ERROR_ACCESS_DENIED, and zero PROCESS_INFORMATION. Native export presence does
not imply permission to create desktop processes or arbitrary device access.

`uwph-user32.dll` reuses the existing UI-thread-only opaque HWND/CoreWindow
backend. Added class/geometry/focus adapters preserve that model. A-message-loop
imports use the corresponding W backend; no ANSI character translation is
implemented. Supported windows are top-level and use the single CoreWindow's
size. Other user32 imports are explicit named failure/empty services. The
monitor shim describes one primary 1920x1080 monitor; input polling reports no
legacy keyboard/raw-input devices, clipboard cannot be opened, HDC creation
fails, and unsupported UI operations fail. There is no native HWND interop.

`entry_stubs.cpp` has compiler assertions against Windows headers for every
named signature. GDI/WGL/IME operations fail without creating resources.
Composition queries return IMM_ERROR_NODATA; shell folder queries fail with
E_NOTIMPL and cleared output pointers; ShellExecute returns its documented
small error handle; file operations are aborted; registry deletion is denied.
Wave device counts are zero, open/device-cap requests return BADDEVICEID, and
handle operations return INVALHANDLE. timeGetTime uses GetTickCount64 truncated
to DWORD. Period requests call the actual system winmm exports when available;
if unavailable they return TIMERR_NOCANDO and claim no resolution change.
HID/setupapi reuse item 24's typed empty-enumeration/failure backend. New
builtins and stubs log each function once; guest probe never calls them.

## Required work before guest initialization (item 28)

Resolving native loader/module/thread functions does not teach the OS about
manually mapped images. Integrate GetModuleHandle/GetProcAddress/LoadLibrary and
module-filename queries with the guest module registry before relying on them.
Native CreateThread/TLS APIs also require the merged TLS lane's carrier/lifecycle
integration for mapped modules; a raw resolved system CreateThread is not that
integration. Current directory/environment/path behavior remains that of the
host AppContainer. Native CloseHandle/waits/files/sync use a consistent OS handle
domain; opaque in-process-server handles must not be mixed into those calls.

The CoreWindow adapter lacks the full window-long, timer, input, class teardown,
placement and messaging surface. Unsupported calls may stop Unity startup and
must be implemented from actual call evidence. There is no claim of guest
startup, rendering, game compatibility, Steam session, or DRM behavior here.
