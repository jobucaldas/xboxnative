# Isolated shell32 command-line route

`CommandLineToArgvW` adapts Wine 10 `dlls/shcore/main.c`, with the ABI from
`shell32.spec`. It preserves executable-token parsing, whitespace, escaped
backslashes, consecutive quotes, Unicode, null-terminated argv, and a single
`LocalAlloc` allocation that the caller releases with `LocalFree`. A null
argument-count pointer fails with `ERROR_INVALID_PARAMETER` before reading
input. Like Wine, a non-null count pointer requires a readable input string.

Empty input requires the guest executable path. The process owner can call
`configure_image_path` before guest threads start. Without configuration this
case explicitly fails with `ERROR_CALL_NOT_IMPLEMENTED`, preserving the count;
it never returns the launcher's host executable path. This branch does not
modify the runtime loader or configure that path. Process identity integration
and allocator interoperability in the executing guest remain acceptance work.

The small resolver addition in `app/host/mlp_probe.h` supplies the parser to
this branch's existing mapper probe. It does not change `launch.h`, execution,
the message pump, or the separately integrated prun runtime.

`bash test.sh` compares 12,000 deterministic quote/backslash-heavy nonempty
strings with Wine's actual shell32 export. It also checks argument validation,
last error, freeing both allocations, resolver isolation, configured empty
input and explicit failure without guest identity. These are off-device tests;
they do not prove Xbox execution or runtime missing-import reduction.

`ShellExecuteW` has a typed unavailable-service route in `execute.h`.
This backend has no guest shell associations or shell execution service:
it returns `SE_ERR_NOASSOC` (31), with `ERROR_NO_ASSOCIATION`, and never
reads guest arguments or invokes the host shell. This is an explicit failure
contract, not full Wine ShellExecute behavior. `execute-test.sh` checks
80 argument/isolation cases and the exact SDK function type off-device;
it deliberately never invokes the host ShellExecute export.
