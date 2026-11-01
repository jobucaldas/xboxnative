# IMM32 without an IME backend

This module implements eight imports used by Megabonk for the current backend,
which creates no input contexts. It does not implement IME text entry.

`ImmGetContext` returns no context. A null HWND sets
`ERROR_INVALID_WINDOW_HANDLE`; non-null guest windows have no associated
context. `ImmGetCompositionStringW` returns zero without touching the buffer.
`ImmGetConversionStatus` and `ImmSetCompositionStringW` fail without modifying
output arguments. `ImmNotifyIME` fails and clears last error for a null HIMC.
`ImmReleaseContext` returns TRUE because there is no reference to release,
matching Wine's implementation rather than claiming an IME operation occurred.

These functions follow the no-context branches of Wine 10's
`dlls/imm32/imm.c`, with ABI signatures checked against `imm32.spec` and the
Windows headers. Guest HWND/HIMC values are never passed to the host IMM32.
Context creation remains unresolved. A future input-context
backend must replace these routes together; it cannot keep these functions
while enabling context creation elsewhere.

Run `bash xbox-test/uwph/imm32/test.sh` for off-device tests. The test compares
return values, last error, and output preservation with Wine's IMM32 exports
for null contexts through typed WINAPI function pointers. This is Wine
evidence on the development machine, not Windows or Xbox execution proof.
The routing-plan CSV count is unchanged by adding individual resolver routes.

`ImmAssociateContext` and `ImmAssociateContextEx` resolve through the packaged
`uwph-u32c.dll` so validation uses the same guest window records as USER32.
Detaching a context and `IACE_IGNORENOCONTEXT` succeed for ordinary valid
guest windows because they already have no context. The previous context is
NULL. Unknown HIMCs fail with `ERROR_INVALID_HANDLE`; invalid HWNDs fail with
`ERROR_INVALID_WINDOW_HANDLE` after window lookup. The Ex API rejects NULL
HWND before lookup and preserves last error. As in Wine, an invalid HIMC can override the
initial invalid-window error. Flags are accepted individually, not as a mask.
`IACE_CHILDREN` follows Wine's unconditional TRUE for a non-NULL parent; no
child has a context to detach. This does not claim successful IME creation.
`IACE_DEFAULT` fails with `ERROR_CALL_NOT_IMPLEMENTED`: no default context
backend exists. No host IME receives a guest handle. Association routes remain
unresolved if the packaged helper DLL is unavailable.

After building USER32, run `bash xbox-test/uwph/user32/ime-association-test.sh`.
It compares 36 no-context association cases with host Wine, plus explicit
unsupported-default and destroyed-window checks. Packaged-library resolution
and Xbox execution require console integration; these tests are off-device.
