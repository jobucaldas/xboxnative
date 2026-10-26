# CoreWindow window-layer spike

`./build.sh` builds and signs the isolated `Joao.XboxSteam.W32u` package on Linux.
`w32u.exe` is a GUI/MTA/AppContainer CoreApplication host. It ignores activation
arguments, owns one CoreWindow, forwards its events, and logs to LocalState/w32u.log.
`window-test.dll` is a separate mingw x64 PE with no CRT/entrypoint; its exported
test entry is invoked by the host. It imports real Win32 window API names from
`uwph-user32.dll`, and D3D11/DXGI/network/crypto/COM/version from the OS. There is
no CPU emulation, desktop window creation, or OS user32 import.

The display adapter provides RegisterClassExW, CreateWindowExW, DefWindowProcW,
ShowWindow, GetClientRect, GetMessageW, PeekMessageW, TranslateMessage,
DispatchMessageW, PostMessageW, PostQuitMessage and DestroyWindow. Opaque HWNDs
map onto the one CoreWindow. Window operations in this private ABI require the UI thread.
Owned top-level windows share client size; child windows are explicitly rejected.
Class/window records remain allocated as tombstones until process exit to prevent
stale HWND reuse. Default WM_CLOSE destroys the mapped window, with reentrant destruction guarded.
Input targets the active mapped window; resizes update all windows. Initial WM_SIZE/WM_ACTIVATE/WM_PAINT are queued; CoreWindow
size/activation/key/pointer events are forwarded afterward. This is a bounded
spike API surface, not complete Win32 behavior (no GDI, clipboard, IME, timers,
text translation, cross-thread queues or window z-order).

WGI readings map all standard buttons, sticks, triggers, disconnected slots and
packet state to XInputGetState. WGI publishes on the UI thread; XInputGetState
returns a coherent SRW-locked snapshot from any thread. Controller indices follow WGI enumeration order;
axes and triggers are clamped and nonfinite readings become zero. There is no stable identity reservation or XInputSetState/rumble. Physical input
requires a connected controller; disconnected-state evidence cannot prove it.

The guest queries `uwph_corewindow(HWND)` (AddRef result), then uses the real
IDXGIFactory2::CreateSwapChainForCoreWindow. It does not hand the fake HWND to
CreateSwapChainForHwnd. It checks events, message-loop quit, client bounds,
invalid handles and class duplication, and presents 180 green frames. Final
rendering references live until process exit so Device Portal can capture the
frame after the test returns. The host continues calling the guest presenter.

`generate_routes.py` consumes lane imap's `tools/xbox/import_map.json` to build
the runtime policy in `dll_map.cpp`. `uwph_resolve_import` uses the real OS DLL
for classified native routes, and requires an installed callback for Wine,
game-supplied and stub modules. Unknowns and uninstalled builtin callbacks fail;
there is no native fallback for user32/win32u/kernel32/ntdll. Successful native
module references are retained for process lifetime. The package contains no
replacement d3d11/dxgi/ws2_32/bcrypt/ole32/version DLLs.

## Scope note: BACKLOG item 11 acceptance vs. a full Wine syscall backend

BACKLOG.md item 11's literal acceptance is: "test PE RegisterClassExW+
CreateWindowExW+message loop survives N frames, WM_SIZE/WM_ACTIVATE logged on
console." Item 12's is: "one process renders a D3D11 frame with real d3d11
while uwph user32 owns the window (dp screenshot)." Both are met and verified
on the real Series S repeatedly (0.1.3.0 PID 4908, 0.1.4.0 PID 5840, 0.1.5.0
PID 3288) with committed console logs and screenshots under `results/`.
`uwph-user32.dll` exports the real Win32 names (`RegisterClassExW`,
`CreateWindowExW`, `GetMessageW`, `DispatchMessageW`, `XInputGetState`, ...)
via `display.def`, and `dll_map.cpp`'s `uwph_resolve_import` is the import-map
policy: `system`-classified DLLs (d3d11/dxgi/ws2_32/bcrypt/ole32/version)
resolve to the real OS modules, `uwph`/`stub`-classified ones (user32/win32u/
kernel32/ntdll) require an installed builtin callback and explicitly refuse to
fall back to native — this is what "user32/kernel32/ntdll go to uwph builtins"
in the lane brief means, and it is what's built and tested here.

What this is **not**: Wine 10's actual `struct user_driver_funcs` /
`__wine_set_user_driver` mechanism (`include/wine/gdi_driver.h`,
`WINE_UNIX_LIB`), Wine's own HWND/message/server state, or a patched game
import table. That is a much larger, separate effort (porting Wine's win32u
Unix-side backend and loader/syscall bridge) that was never implied by this
lane's concrete acceptance line, only by LANES.md's longer-term architecture
narrative. If/when that port is attempted, it must preserve Wine-owned HWND/
message/server state and connect CreateWindow/DestroyWindow,
WindowPosChanging/Changed, ProcessEvents, focus and input callbacks to
CoreWindow; this spike's private queue must not become a second queue beside
Wine's own. The resolver already exposes `uwph_set_builtin_resolver` as the
seam for that future callback — console evidence shows it correctly refusing
native fallback today because no such callback is installed yet.

Native KERNEL32 imports in the trusted harness/guest are intentional (the
test PE is not a Wine-produced binary) and do not demonstrate Wine kernel32
routing. Do not interpret these passing results as evidence that Wine, Steam
or Megabonk runs on Xbox — only that the HWND-to-CoreWindow adapter, the
gamepad-to-XInput mapping, and the system/uwph import-routing policy this
lane was asked to build all work, verified on the real console.

Console evidence: `results/console-final.log` and `results/green-frame.png` are
from package 0.1.3.0 on the Series S, PID 4908. The console returned zero failures
and the screenshot shows the green guest frame. Native resolver uses
`LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`; `LoadPackagedLibrary` was
observed to fail for system d3d11 with error 126. The six native routes were
resolved and their representative exports exercised. All three PE audits pass.
The fresh package's Game-mode 5 GB limit and physical controller remain unverified.

Additional console regression evidence: `results/console-0.1.4.log` and
`results/green-frame-0.1.4.png`, package 0.1.4.0, PID 5840. UI-thread guard,
child rejection, class atom lookup, default WM_CLOSE, reentrant destruction and
native module cache lookups passed on Xbox, then 180 green frames presented.
The Wine integration and physical input limitations above still apply.

Version 0.1.5.0 adds synthetic WGI/XInput checks before running the guest:
mixed button conversion, axis endpoints, trigger scaling, malformed values,
packet changes, disconnect/reconnect, invalid arguments and worker-thread reads.
Synthetic readings use slot 3 and are cleared before physical WGI polling.
They do not verify a physical controller or stable device identities.

Version 0.1.5.0 console verification: `results/console-0.1.5.log`, PID 3288,
reports passing synthetic conversion/packet/reconnect/worker checks and
`W32U RESULT failures=0` after 180 Presents. The fresh screenshot
`results/green-frame-0.1.5.png` shows green at 1920x1080. Physical pad 0 was
disconnected. All Wine integration and Game-mode limitations above still apply.
