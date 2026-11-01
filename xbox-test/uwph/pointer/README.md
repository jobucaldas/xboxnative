# CoreWindow mouse feed

`feed.h` translates mouse position and five button states into the owning
user32 record registry. It does not call system USER with a guest HWND.
CoreWindow DIP coordinates scale to the current guest client rectangle.
Owner coordinate conversion and cursor clipping determine message coordinates.
Button transitions update `GetAsyncKeyState`; scalar messages use the owner
queue when its `PostMessageW` export is available. Shift/control flags come
from the owner keyboard state. Capture loss and deactivation clear button state.

Run `bash test.sh` for deterministic off-device regression under Wine.
Run `bash queue-test.sh /absolute/path/to/uwph-u32c.dll` for cross-thread
integration with an existing owner DLL. This test requires the real owner
queue exports and fails if they are missing. Neither test proves physical
pointer input, Xbox rendering, or gameplay.

The launcher connects mouse move, press and release events only. Wheel,
guest capture selection, raw mouse packets, touch and pen are not implemented
by this feed. An absent or refusing queue leaves the CoreWindow event unhandled.
The owner still receives cursor/button state if its typed exports are available.
