# Launcher GDI object query

The isolated launcher route resolves Megabonk's `gdi32.dll!GetObjectA`.
This backend creates no guest GDI objects, so every object lookup fails with
zero, preserving the output buffer and last error. Wine 10's
`dlls/gdi32/objects.c` delegates unknown non-font handles to
`NtGdiExtGetObjectW` in `dlls/win32u/gdiobj.c`, which has this behavior;
`gdi32.spec` supplies the three-argument signature.

Guest numeric handles never reach host GDI. This does not supply bitmap,
font, pen, DC, OpenGL, or rendering support. A future guest GDI object
registry must replace this route and implement successful object queries.

Run `bash xbox-test/uwph/gdi32/test.sh` for off-device Wine comparisons
through resolved WINAPI pointers: invalid handles, negative/zero/positive
sizes, null/non-null output, buffer guards, last error and resolver isolation.
These tests do not establish Xbox execution or reduce a measured console
missing-import count. No console test has run for this route.

`GetDeviceCaps` resolves only when the caller supplies the packaged record
DLL's `uwph_u32c_device_caps` hook. The launcher supplies that hook from the
same DLL used by USER32. The hook accepts only live guest DC records from
`GetDC`; unknown or released records return zero and `ERROR_INVALID_HANDLE`,
matching Wine's `win32u/dc.c` and `gdiobj.c`. No guest handle reaches host GDI.

For live records, display dimensions follow `uwph_u32c_configure`, matching
`GetSystemMetrics` and `EnumDisplaySettings`. Color depth, refresh and DPI
describe the existing virtual display (32 bits, 60 Hz, 96 DPI), not measured
physical output. Physical millimeter dimensions use Wine's DPI conversion.
`device_caps.h` implements the documented capability indices; Wine 10's
`win32u/driver.c` provides the display/color relationships. Rendering capability
bits remain zero because there is no GDI drawing backend. This route does not
provide pixel formats, OpenGL, bitmaps, fonts or drawing.

After `bash xbox-test/uwph/user32/build.sh`, run
`bash xbox-test/uwph/gdi32/caps-test.sh`. It exercises the actual UWP DLL under
off-device Wine: 40 invalid-DC differential cases, live/released and independent
DCs, display reconfiguration, dimensions up to LONG_MAX, preserved last error,
conservative capability bits, and driver-independent Wine display values.
Launcher routing requires integration into the current runtime before real
Play acceptance; building this older probe launcher is not console proof.
