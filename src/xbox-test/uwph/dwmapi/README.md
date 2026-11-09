# Guest window DWM boundary

`route.h` overrides the two Megabonk DWM imports before native system addresses
can be returned to guest callers. Native DWM must never receive record HWNDs.
The resolver binds only the packaged user32 record callbacks.

`DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` returns physical screen
bounds from the existing borderless record backend. It validates HWND, output,
buffer size, and child-window style. Failures leave output unchanged. The record
backend has no nonclient shadow or DPI virtualization to add to these bounds.
Unknown attributes return `E_NOTIMPL` without changing output.

`DwmSetWindowAttribute` validates the record HWND and returns `E_NOTIMPL` for
unsupported DWM policies. There is no DWM renderer or policy storage for guest
records. Wine 10's setter is explicitly an unconditional-success stub; copying
it would falsely advertise implemented behavior.

`DwmFlush` has no composition queue to wait on: guest presents go to the
CoreWindow swapchain, which paces itself, and this backend has no DWM
compositor. It returns `E_NOTIMPL` for the same reason the setter does — Wine
10's `S_OK` is the same unconditional stub, and the console's `dwmapi.dll` does
not export the name at all (Fields of Mistria calls it). `DwmGetColorizationColor`
stays on the native route since no HWND reaches it.

Run `WINEPREFIX=<prefix> bash xbox-test/uwph/dwmapi/test.sh`. Invalid HWND get
calls compare typed ABI/results/untouched output against Wine 10. Remaining
checks use host callbacks representing guest records: negative screen origins,
size boundaries, child rejection, backend errors, unknown attributes, unsupported
setters, resolver restrictions and sentinel bytes. The test also explicitly
loads the built production `app/out/layout/uwph-u32c.dll`, registers a class,
creates a record window, verifies its bounds, and rejects its destroyed handle.
Build the launcher first so this packaged DLL exists. This proves off-device
contracts only, not Xbox composition, presentation, or gameplay.
