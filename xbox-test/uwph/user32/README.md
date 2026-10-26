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
No keyboard/mouse queues, raw input, clipboard, IME, timers, dialogs, full ANSI
window procedures, full desktop destruction ordering, nonclient sizing or cross-thread
SendMessage marshaling is claimed. A/W long aliases apply to these Unicode records.
Handles remain tombstones until process exit (4096 record limit); this prevents
stale handles from aliasing new records. Class extra bytes are reserved for later
class-long APIs. Cross-thread sends explicitly fail rather than pretending delivery.

`build.sh` produces the DLL and a CRT-free own PE regression. `probe.h` maps and
executes only this trusted test through the launcher's real Play path, checks return
42, and logs implementation/missing counts over all 121 user32 imports from the
independently checked 628-pair full Megabonk import set. Game entries, DllMain and
TLS callbacks remain uncalled. Off-device Wine is only a development check;
console evidence is required separately. `offdevice.cpp` deliberately substitutes
desktop VM calls and cannot establish Xbox W^X behavior.
