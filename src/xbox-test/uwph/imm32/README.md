# IMM32 without an IME backend

This module implements eight imports used by Megabonk for the current backend,
which creates no input contexts. It does not implement IME text entry.

`ImmGetContext` returns no context. A null HWND sets
`ERROR_INVALID_WINDOW_HANDLE`; validated guest windows have no associated
context. An invalid guest window sets `ERROR_INVALID_WINDOW_HANDLE`; an absent
record validator sets `ERROR_NOT_SUPPORTED`. `ImmGetCompositionStringW` returns zero without touching the buffer.
`ImmGetConversionStatus` and `ImmSetCompositionStringW` fail without modifying
output arguments. `ImmNotifyIME` fails and clears last error for a null HIMC.
Composition, conversion, set-composition, and notification calls reject every
non-null foreign HIMC with `ERROR_INVALID_HANDLE`, preserving output buffers.
`ImmReleaseContext` returns TRUE because there is no reference to release,
matching Wine's implementation rather than claiming an IME operation occurred.

These functions follow the no-context branches of Wine 10's
`dlls/imm32/imm.c`, with ABI signatures checked against `imm32.spec` and the
Windows headers. Guest HWND/HIMC values are never passed to the host IMM32.
`ImmAssociateContext` and `ImmAssociateContextEx` support detaching an absent
context from a validated guest window. The resolver receives the record
backend's `IsWindow` export; no native HWND validator sees guest pointers.
Non-null foreign contexts fail with `ERROR_INVALID_HANDLE`. `IACE_DEFAULT`
fails with `ERROR_NOT_SUPPORTED` because this backend cannot create its default
IME. Unknown flag combinations fail and preserve last error, following Wine.
A missing window validator fails with `ERROR_NOT_SUPPORTED`; invalid windows
fail with `ERROR_INVALID_WINDOW_HANDLE`. Null `ImmAssociateContextEx` returns
FALSE preserving last error, matching Wine's immediate return. Child detach
succeeds because every guest child also has no context.

Context creation remains unresolved. A future input-context
backend must replace these routes together; it cannot keep these functions
while enabling context creation elsewhere.

Run `bash src/xbox-test/uwph/imm32/test.sh` for off-device tests. The test compares
return values, last error, and output preservation with Wine's IMM32 exports
for null and foreign contexts through typed WINAPI function pointers. This is Wine
evidence on the development machine, not Windows or Xbox execution proof.
The routing-plan CSV count is unchanged by adding individual resolver routes.

Association tests add null-HWND Wine differentials and a guest-record validator
regression for detach, children, unsupported defaults, invalid handles and flags.
