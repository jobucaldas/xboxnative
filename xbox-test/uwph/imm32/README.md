# IMM32 without an IME backend

This module implements six imports used by Megabonk for the current backend,
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
Context creation and association remain unresolved. A future input-context
backend must replace these routes together; it cannot keep these functions
while enabling context creation elsewhere.

Run `bash xbox-test/uwph/imm32/test.sh` for off-device tests. The test compares
return values, last error, and output preservation with Wine's IMM32 exports
for null contexts through typed WINAPI function pointers. This is Wine
evidence on the development machine, not Windows or Xbox execution proof.
The routing-plan CSV count is unchanged by adding individual resolver routes.
