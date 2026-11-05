# sysx native routes

`native.h` permits only seventeen named dbghelp/WinINet exports (six dbghelp ones serve gbe_fork, the Steam API provider for owned games). It loads the real
System32 DLL and returns its exact export. Missing libraries or exports remain
unresolved, so Play's existing missing-import trap reports them. There is no
manufactured success, broad DLL forwarding, relaxed TLS validation, guest DLL
search, or protocol substitute. The game remains responsible for DbgHelp's
single-threaded calling contract. Native WinINet networking remains subject to
AppContainer capabilities and Xbox policy; these routes grant no exemptions.

`WINEPREFIX=<isolated-prefix> bash src/xbox-test/uwph/sysx/test.sh` runs off-device:

- Resolver rejects null arguments, unapproved DLLs/exports, and wrong-case names.
- SymInitialize opens a real symbol session. SymLoadModuleEx registers the
  regression PE at its actual base. SymFromAddr finds its exported function
  with zero displacement and rejects an unknown address.
- SymRegisterFunctionEntryCallback64 matches the direct OS export's return and
  last error. Wine 10 returns ERROR_CALL_NOT_IMPLEMENTED; that is preserved and
  is not evidence of working callback registration on Wine or Xbox.
- InternetCrackUrlA extracts HTTPS host, port, and path and rejects invalid input.
- InternetOpenA, InternetConnectA, HttpOpenRequestA and HttpSendRequestA perform
  an actual synchronous GET against a local TCP server. HttpQueryInfoA verifies
  status, content length, and the insufficient-buffer contract.
- InternetCloseHandle releases all three real handles and rejects NULL.

The server uses TCP, never AF_UNIX. Tests use no external service or console.
The runner requires listener readiness within five seconds and bounds Wine to
30 seconds, with forced termination five seconds later if needed. A timeout or
listener failure fails the test; neither is native-operation evidence.
No Xbox export availability, deployment, UWP networking, frame, or gameplay
milestone is established by these tests. Pending native exports remain in
`research/sysx-todo.csv` until console verification. Regenerate inventory with
`python3 src/research/sysx/audit.py`. Source CSV defaults to the shared research
input and can be supplied explicitly with `--source`.

`WINEPREFIX=<isolated-prefix> bash src/xbox-test/uwph/sysx/system-test.sh` exercises
existing `system` routes from the actual launcher route table. Every tested
export must belong to a system-routed DLL and load through System32. The test
checks the SHA-256 `abc` vector, hash buffer sizing and destroyed-handle failure,
the random-generation API, a volatile per-process HKCU key with a persisted
typed value and missing-value error, token buffer sizing and a real user SID,
balanced COM/WinRT apartment initialization, COM memory and GUID formatting,
OLEAUT32 BSTR imports by the game's ordinals, and WinRT strings containing
embedded NULs, reference strings and duplicates surviving deletion of their
source handle. The test deletes its registry key on success. No external
network, account, game file or console is used. These checks establish host
operation coverage only; they do not certify Xbox permissions or behavior.
The runner bounds Wine to 30 seconds, with a five-second forced-kill grace;
a timeout fails the regression without changing any API assertion.

`WINEPREFIX=<isolated-prefix> bash src/xbox-test/uwph/sysx/network-test.sh` checks
Winsock through the production `system` route and real System32 exports. The
regression verifies the game's ordinal aliases, numeric address conversion and
resolution, a real IPv4 TCP listener/client pair, select readiness, send/recv
and WSASend/WSARecv payloads, nonblocking WSAEWOULDBLOCK, orderly EOF,
closed-socket WSAENOTSOCK, and event set/reset/wait lifecycle. Receive operations
have finite timeouts; the runner also enforces a 30-second deadline and rejects
Wine unhandled-fault diagnostics even if Wine exits zero. No external service,
AF_UNIX socket, account, game file or console is used by the regression. This
is host operation coverage, not Xbox/AppContainer networking permission proof.

`WINEPREFIX=<isolated-prefix> bash src/xbox-test/uwph/sysx/certificate-test.sh`
checks all seven game-imported crypt32 exports through the production system
route. OpenSSL generates an ephemeral self-signed DER fixture, without adding
it to any system trust store. The regression rejects malformed DER and duplicate
insertion, checks certificate bytes and context lifetime after closing a memory
store, builds a cache-only chain, and verifies signature/time status. Base chain
policy must reject the untrusted root. Flipping a signature bit must produce
both the chain's invalid-signature flag and TRUST_E_CERT_SIGNATURE from policy
evaluation. Policy evaluation returning TRUE alone never counts as certificate
acceptance. No external certificate requests, trust exemptions, accounts or
console access are used. Path conversion and Wine execution have deadlines;
Wine unhandled-fault diagnostics fail the runner even with exit zero. This is
host operation proof, not Xbox crypt32 or AppContainer permission proof.

`WINEPREFIX=<isolated-prefix> bash src/xbox-test/uwph/sysx/proxy-test.sh` exercises
all four imported WinHTTP exports through the production system route. It
checks invalid sessions/options, reads current-user proxy configuration and
frees returned strings without changing settings, then serves a PAC script
from an ephemeral IPv4 loopback HTTP listener. The script must receive the
expected URL and host and return the expected named proxy. The regression
also rejects an already closed session. No WPAD discovery, external DNS,
external service, proxy connection or AF_UNIX transport is used. Socket reads,
listener readiness and the overall Wine invocation have deadlines. This
establishes host proxy operation coverage, not Xbox permission or PAC support.
The parameter-error oracle is Wine 10's `dlls/winhttp/tests/winhttp.c` and
`dlls/winhttp/session.c`; production system exports are exercised directly.

`WINEPREFIX=<isolated-prefix> bash src/xbox-test/uwph/sysx/path-test.sh` checks both
imported SHLWAPI exports through the production `pe_tail` resolver and verifies
that their addresses equal the actual loaded System32 exports. Path checks
cover absolute, relative and UNC normalization, parent traversal at a drive
root, empty input and invalid null arguments. Registry checks create a unique
volatile HKCU subtree, delete a populated nested key, preserve a sibling,
require ERROR_FILE_NOT_FOUND on repeated deletion and remove the test subtree.
No existing user key is modified. Wine execution has a 30-second deadline and
five-second termination grace; unhandled faults fail even with exit zero.
Wine 10's `dlls/kernelbase/path.c` and `dlls/shlwapi/tests/path.c` provide the
path oracle. Input/output buffers are distinct; overlapping buffers are not
part of this contract. These are off-device operation checks, not Xbox proof.

`WINEPREFIX=<isolated-prefix> bash src/xbox-test/uwph/sysx/version-test.sh` checks
all three imported version APIs through the production DLL route classification
and actual System32 exports. The runner embeds a deterministic VERSIONINFO
resource into its temporary regression PE. Checks cover exact file/product
version words, file type, language/codepage translation, ANSI description and
version strings, absent-value length and missing-file errors. No installed
game or system file is modified. Wine execution has a 30-second deadline and
five-second termination grace; unhandled faults fail even with exit zero.
This is off-device operation proof, not Xbox export or AppContainer proof.

`WINEPREFIX=<isolated-prefix> bash src/xbox-test/uwph/sysx/adapter-test.sh` checks
all three imported IP Helper exports through the production system route and
real System32 exports. It verifies adapter buffer sizing, IPv4 enumeration
including loopback, and agreement with GetIfEntry on index, type, MTU and
physical address. GetNetworkParams must provide correctly sized, terminated
network metadata; null parameters must return ERROR_INVALID_PARAMETER.
Sizing races permit at most four retries, only on ERROR_BUFFER_OVERFLOW.
The test changes no adapter, DNS or proxy setting and opens no connection.
Wine execution has a 30-second deadline and five-second termination grace;
unhandled faults fail even with exit zero. This is host operation proof,
not Xbox adapter visibility or AppContainer permission proof.
