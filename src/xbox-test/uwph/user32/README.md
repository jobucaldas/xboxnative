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

Play integration also supplies a guest message queue (`message_queue.h`).
`PeekMessageA/W` reads the calling thread's queue, supports window/child and
message-range filters, PM_NOREMOVE/PM_REMOVE, thread-only filters, posted-message
category masks and WM_QUIT. An empty queue leaves the caller's MSG unchanged.
`PostMessageW`, `PostThreadMessageW` and `PostQuitMessage` enqueue real records;
`DispatchMessageW` calls the record window procedure on its owning thread, or
the supplied WM_TIMER callback. Destroyed-window messages are discarded.
The queue never passes record HWNDs to native user32 and does not pump the UI's
CoreDispatcher from a guest worker. The launcher continues that UI pump.

This is a posted-message backend, not a complete Wine message server. Cross-thread
synchronous sends, automatic timer/paint generation, keyboard translation,
text/pointer marshalling and ANSI multibyte character delivery remain unsupported.
PostMessageW rejects known pointer messages and non-ASCII character messages
rather than claiming delivery with corrupted text. No native input is generated
by the queue test. Run `message-queue-test.sh` against the packaged DLL for the
off-device filtering, dispatch, destruction and thread-isolation regression.
