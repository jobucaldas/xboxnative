# Guest OpenGL state queries

The isolated resolver supplies Megabonk's `wglGetCurrentContext` and
`wglGetCurrentDC` imports. Both return NULL and preserve last error because
this backend cannot create or bind guest OpenGL contexts. Wine 10
`dlls/opengl32/wgl.c` returns these values when the thread has no current
context; `opengl32.spec` defines their zero-argument WINAPI signatures.

Host TEB state must never be mistaken for guest OpenGL state. Context
creation and rendering remain unsupported and unresolved here. When guest
context support arrives, these queries must read its per-thread registry.

Run `bash src/xbox-test/uwph/opengl32/test.sh` for off-device Wine differential
checks on two threads without contexts, preserved last error and resolver
isolation. This is not Xbox execution or real launcher Play acceptance.

The resolver also supplies `wglDeleteContext` and `wglMakeCurrent`, following
Wine 10 `dlls/opengl32/unix_wgl.c` and the WINAPI signatures in
`opengl32.spec`. Because no guest context handles exist, deletion and binding
a non-null context fail with `ERROR_INVALID_HANDLE`. With no previous context,
`wglMakeCurrent(non_null_dc, NULL)` returns TRUE and preserves last error;
`wglMakeCurrent(NULL, NULL)` fails with `ERROR_INVALID_HANDLE`. The successful
case does not validate the DC, create a context, or change either current query.
Tests compare all these cases through resolved pointers with host Wine on two
threads, using null and arbitrary handle values. Host handles never reach GDI.

## Loading `OPENGL32.DLL`

`modules.h` makes `opengl32.dll` a Play-owned module (like the XInput modules): `LoadLibrary`, `GetModuleHandle(Ex)`,
`GetProcAddress` (only the names `resolve()` serves), `FreeLibrary` and `GetModuleFileName` work the same on the
console, which has no `opengl32.dll`, and under Wine. A guest- or runtime-shipped `opengl32.dll` is mapped first and
takes precedence, which is how a real driver (issue #176) replaces these stubs.
`bash src/xbox-test/uwph/opengl32/runtime-test.sh` (after `app/tests/play-runtime.sh`) runs a guest through Play's
production loader; `app/tests/play-sdl3-gl.sh` runs an SDL3 GL program that loads the library and then reports the
missing pixel formats as BLOCKED.
