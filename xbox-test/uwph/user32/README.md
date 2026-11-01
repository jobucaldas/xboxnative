# user32 record core (lane u32c)

`core.cpp` builds `uwph-u32c.dll` using documented UWP APIs. It does not create
native windows or attach records to CoreWindow. The existing display adapter
continues to own rendering and swap chains. This DLL is a separate implementation
for record-only callers; do not pass its HWNDs to the display adapter or OS APIs.

Classes have case-insensitive ASCII names, instance identity, class atoms and
bounded extra-byte storage. Window records retain procedure, instance, style,
parent, ID, user data, client position/size, title and extra bytes. Creation calls
WM_NCCREATE/WM_CREATE and destruction calls WM_DESTROY/WM_NCDESTROY. Pointer and
LONG accesses preserve their respective widths and reject invalid offsets.
Properties support strings and integer atoms, ANSI conversion, removal and cleanup.
`uwph_atom_*` is an explicitly process-local atom table; it does not claim Windows
cross-process GlobalAtom semantics. The Global* names in this private DLL are not
routed as kernel32 exports. No kernel32 implementation is changed by this lane.

One desktop/monitor record defaults to the launcher's 1920x1080 viewport;
`uwph_u32c_configure` can update its dimensions. GetSystemMetrics supports screen,
virtual-screen and monitor-count indices; others fail with ERROR_NOT_SUPPORTED.
This is a virtual record description, not a physical display enumeration.

RegisterDeviceNotificationW/UnregisterDeviceNotification remain unresolved: Wine
requires a notification backend. They are not pure geometry and are not replaced
with dummy handles. The historical CSV's 20 PE_PURE classification is incorrect;
see the corrected report. DefWindowProc implements creation, close and text; its
remaining desktop/IME/nonclient behavior is outside this record-only subset.
No keyboard/mouse message queues, raw input delivery, IME, dialog modal loops,
full ANSI window procedures (WM_SETTEXT/WM_GETTEXT are not ANSI-thunked through
the SendMessageA-family entry points), full desktop destruction ordering,
nonclient sizing or cross-thread SendMessage marshaling is claimed. Clipboard is
a process-local store (open/empty/get/set/format-available only; no OS/X11
clipboard sync). SetTimer/KillTimer/TrackMouseEvent are bookkeeping only: actual
WM_TIMER/WM_MOUSELEAVE/WM_MOUSEHOVER delivery needs the message pump (item 43).
AdjustWindowRect(Ex) and SetWindowPos use fixed classic (96 DPI, unthemed)
metrics; SetWindowPos tracks no z-order, same limitation as EnumWindows.
A/W long aliases apply to these Unicode records.

## CoreWindow keyboard polling feed (u32m)

The launcher attaches `corewindow_keyboard.h` in `SetWindow`, following the
CoreWindow `KeyDown`/`KeyUp` subscription pattern in the PPSSPP UWP reference.
It loads the packaged `uwph-u32c.dll`, which is also used by `route.h`, and
passes keyboard transitions to its existing `uwph_u32c_key_event` hook.
No guest window handles reach host USER32 APIs.

`keyboard_feed.h` distinguishes left/right Shift using the scan code and
Control/Alt using the extended-key flag. Generic modifier state remains down
until both sides are released. Repeated KeyDown events refresh the asynchronous press bit, as in Wine
10 server/queue.c. Deactivation and window closure release all held keys, preventing
stuck polling state after a missed KeyUp. Existing asynchronous press bits
remain available for one poll after release. The adapter runs on the UI thread;
the DLL hook and polling exports synchronize their shared state.

This feeds held-state polling only. It does not provide keyboard messages,
raw input, text/IME, host toggle synchronization, or Windows per-thread message-queue
semantics for GetKeyState. It does not map gamepad buttons to keyboard keys.
Pointer position, OS cursor control, activation-to-guest focus notifications,
and console controller/keyboard verification remain outstanding.

The same CoreWindow adapter now feeds physical mouse button snapshots into
VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1 and VK_XBUTTON2 polling.
Wine server/queue.c maps mouse button changes to these same key-state slots.
PointerButtons emits only transitions: repeated movement while held does not
re-arm GetAsyncKeyState's low bit. Touch and pen events do not overwrite mouse
state. Mouse capture loss, deactivation and close release held mouse buttons.
This does not implement mouse messages, raw input, wheel events, position,
native capture, or OS cursor clipping/visibility. Events outside the CoreWindow
are not observed. Xbox delivery remains unverified. keyboard-test.sh exercises
all 32 button combinations against the actual built DLL under off-device Wine.

Run `bash build.sh`, then `bash keyboard-test.sh` for off-device proof under
Wine. The test uses the built record DLL and its actual polling exports,
checking repeat presses, sided and aggregate modifiers, invalid keys,
release/repress, asynchronous press retention, and preserved last error.
The full launcher build additionally compiles the WinRT subscriptions.
Neither check proves events were delivered by an Xbox keyboard.

GetDC/ReleaseDC hand out opaque per-call handles with no real GDI surface: no
rendering exists in this module. LoadCursorA/LoadIconA/W/LoadImageA/W resolve
only standard shared system cursor/icon IDs (hInstance==NULL, integer
resource ID, mirroring Wine's LoadCursorA/LoadIconA/W -> LoadImage call
chain); loading a module's own resource (hInstance!=NULL), LR_LOADFROMFILE,
or IMAGE_BITMAP honestly fail (no PE resource-section parser, file/codec
backend, or GDI bitmap backend here) rather than fabricate a handle.
DestroyIcon/DestroyCursor share one handle table (same as Wine): shared
system handles no-op TRUE, this module's own CreateIconFromResourceEx
handles are actually freed. GetIconInfo never claims real hbmMask/hbmColor
(no GDI bitmap backend); fIcon/hotspot are correct for handles this module
created. CreateIconFromResourceEx accepts BITMAPINFOHEADER-fronted
icon/cursor bits (deriving size from width/height or the embedded DIB
header) and rejects version 0x00020000 and RIFF/.ani data, matching Wine's
CreateIconFromResourceEx gate; it allocates a handle only, never a real
bitmap. LookupIconIdFromDirectoryEx is a real, backend-free computation
(ported from Wine's CURSORICON_FindBestIcon/FindBestCursor) over
caller-supplied CURSORICONDIR bytes.
Handles remain tombstones until process exit (4096 record limit); this prevents
stale handles from aliasing new records. Class extra bytes are reserved for later
class-long APIs. Cross-thread sends explicitly fail rather than pretending delivery.

`build.sh` produces the DLL and a CRT-free own PE regression. `probe.h` maps and
executes only this trusted test through the launcher's real Play path, checks return
42, and logs implementation/missing counts over all 121 user32 imports (gate
<=15 missing) from the
independently checked 628-pair full Megabonk import set. Game entries, DllMain and
TLS callbacks remain uncalled. Off-device Wine is only a development check;
console evidence is required separately. `offdevice.cpp` deliberately substitutes
desktop VM calls and cannot establish Xbox W^X behavior.

### CoreWindow pointer position feed

`uwph_u32c_pointer_position` accepts local CoreWindow DIPs and window size.
The adapter forwards physical mouse positions on press, release and move.
The DLL maps the full window onto its configured virtual desktop, matching
its display metrics, then applies its guest clip rectangle to polling state.
Invalid or zero-size samples leave the previous position intact. This does
not warp the OS pointer, enforce OS clipping, forward mouse messages or raw
input, or provide touch/pen coordinates. Events outside CoreWindow are not
observed. Console delivery requires separate verification; host tests exercise
only the actual DLL hook and guest polling state.

### US-layout key names

`GetKeyNameTextW` supplies key labels for the fixed US keyboard reported by
this backend. Scan code bits 16–23 and extended bit 24 select the key; bit 25
collapses right Shift/Ctrl/Alt to their left-side names. Other bits do not affect
lookup. Named keys include navigation, modifiers, numpad and F1–F24. Printable
keys use the existing US scan table. Unknown codes follow Wine's default
mapping fallback (a NUL character can have return length one). Results truncate
to `size - 1` with a terminating NUL. Null output and nonpositive capacity
return zero without writing; last error is preserved. No host layout or host
USER32 function supplies guest names. This does not add keyboard messages,
raw-input events or controller delivery.

After `build.sh`, run `bash xbox-test/uwph/user32/key-names-test.sh`.
It tests the packaged DLL's exported WINAPI function under off-device Wine:
174 fixed-US cases, short buffers, extended keys, modifier collapsing, ignored
bits, guard words and last error. Fifteen printable-key comparisons use host
Wine after driver warmup. Special-key spelling and capacity-one behavior use
the Wine default-US source contract rather than host display-driver behavior;
Wine's X11 driver can use different labels and leave a size-one buffer intact.
These tests do not establish console execution or runtime adoption.

Device notifications: `RegisterDeviceNotificationW` validates flags, guest window
recipients and filter types in Wine's order. Valid registrations currently fail
with `ERROR_CALL_NOT_IMPLEMENTED`: no hotplug delivery backend exists. No host
HWND or service handle is used. `UnregisterDeviceNotification(NULL)` returns FALSE
and preserves last error; other tokens fail with `ERROR_INVALID_HANDLE` because
this backend never issues notification handles. `device-notifications-test.sh`
checks the packaged DLL against host Wine for invalid requests and exercises
supported validation paths using guest windows. These exports remove unresolved
imports, but do not provide successful device notification registration.

Dialog failure contracts: `DialogBoxParamA/W` reject invalid non-NULL guest
owners before resource access, following Wine's zero return and last-error
behavior (reserved USER handle low words preserve last error). Other requests
return -1 with `ERROR_CALL_NOT_IMPLEMENTED`. Dialog resource parsing and modal
loops are unavailable; neither callbacks nor host resource APIs are invoked.
`EndDialog` returns FALSE for ordinary guest windows without changing last
error, and rejects invalid/destroyed handles with `ERROR_INVALID_WINDOW_HANDLE`.
No dialog state is created, and ordinary windows are neither hidden nor destroyed.
Run `dialog-failures-test.sh` after `build.sh`: 16 off-device cases include
11 Wine comparisons and explicit unsupported-creation checks. These exports
remove unresolved symbols; they do not implement modal dialogs or message pumps.
# MessageBoxA unsupported creation

`MessageBoxA` returns 0 and sets `ERROR_CALL_NOT_IMPLEMENTED`. The backend
cannot display a modal dialog yet. It never fabricates a button selection,
reads guest text, or forwards guest handles to host USER32. Wine's `user32.spec`
defines the ABI; `msgbox.c` requires dialog resources and a modal loop for a
working implementation. This explicit failure route does not implement UI or
claim Wine's successful-dialog behavior. Run `bash message-box-test.sh` after
`build.sh` for 17 off-device ABI/failure/isolation checks.

Classic system metrics: `GetSystemMetrics` now reports border, dialog frame,
resize frame, client edge, normal/small caption and menu dimensions consistent
with this backend's existing fixed 96 DPI `AdjustWindowRectEx`. Unknown indices
return zero and preserve last error, following Wine 10 `win32u/sysparams.c`.
Other unimplemented metric indices still return zero; this is not complete
system-metric support or host theme/DPI discovery. `system-metrics-test.sh`
checks Wine's invariant borders, unknown-index last error, configured desktop
size and each backend's own metric/rectangle consistency off-device.

Input parameter queries: `SystemParametersInfoW` now reads the record session's
fixed keyboard delay/speed, mouse hover width/height/time, mouse sensitivity,
and wheel line/character counts. Defaults follow Wine `win32u/sysparams.c`
`UINT_ENTRY` declarations. Queries write one UINT, ignore uiParam, and preserve
last error; NULL output fails without changing last error. Setters still fail
with `ERROR_NOT_SUPPORTED`; no host registry, repeat scheduling, pointer
acceleration or message delivery is implemented. `input-parameters-test.sh`
checks these contracts off-device, keeping configurable host values separate
from the backend's fixed defaults.

Raw-input registration: the complete batch now validates INPUTSINK/REMOVE
constraints and guest target handles before changing records. Records stay
sorted by usage page/usage; replacement and removal affect only the matching
pair. `GetRegisteredRawInputDevices` exposes this guest registry with Wine 10
count, buffer-size, untouched-output and last-error contracts. A NULL input
array with a nonzero count is defensively rejected. No host HWNDs, actual HID
devices, notifications or WM_INPUT delivery are involved. Run
`raw-registration-test.sh` after `build.sh` for off-device Wine comparisons.

Lock-key polling: Caps Lock, Num Lock and Scroll Lock now latch bit 0 in
`GetKeyState` on fresh key-down transitions, following Wine's
`server/queue.c:set_input_key_state`. Repeated downs do not toggle; releases
and CoreWindow deactivation retain the latch. `GetAsyncKeyState` still reports
only held state and its independent consumable press bit. State starts clear;
this does not synchronize locks changed outside the app or provide per-thread
message-queue state. Run `lock-keys-test.sh` after `build.sh` for off-device
coverage of four cycles per lock, repeats, release-all and last error.
