# Legitimate Steam architecture and acceptance

## Scope and status

The target is the **complete, unmodified Valve Windows client and original game
Steam API DLL running on Xbox**, inside the launcher's Wine-style/uwph UWP
Game-mode runtime. This is an architecture to validate, **not a working native
Steam integration or a claim that Megabonk is playable**. A host PC client is not
the production design; host Wine observations cannot establish Xbox feasibility.

Keep these three layers distinct:

1. **Launcher authentication and content:** a Steam network session for the
   owner's library and official depot downloads.
2. **Native Steamworks runtime:** a running genuine client, game discovery,
   user/app context, IPC, interfaces and callbacks used by the original API DLL.
3. **Game execution:** loader initialization, rendering, input, audio and stable
   play through the launcher's Play route.

Success at one layer does not certify the next. Synthetic runtime/input tests,
static import inventories, mapped images and a visible login screen are not
proof of an authenticated native client, original API success or gameplay.

## What exists in this repository

| Route | What it provides; what it does not prove |
| --- | --- |
| [`app/main.cpp`](../xbox-test/app/main.cpp): `begin_qr`, `poll_qr`, `cm_build_client_logon`, `user_session` | Launcher QR authentication and its CM session. This is not a game Steamworks user/pipe. |
| [`app/steamdl.h`](../xbox-test/app/steamdl.h): `CmSession` and depot download code | CM requests, manifest/depot-key retrieval, CDN download, decryption and integrity checks. Downloading owned files does not create native Steam interfaces. |
| [`app/launch.h`](../xbox-test/app/launch.h): `start_launch`, `lp_run_impl` | Play probes/maps dependencies and writes a launch result with `gameCodeExecuted=false`. It does not establish guest game execution. |
| [`app/host/wxl_mapper.h`](../xbox-test/app/host/wxl_mapper.h): `wxl::run`, mapped branch of `load` | Play's mapped-image path returns before TLS/unwind/load-config initialization or entry. Separate test-PE execution is not game execution. |
| [`uwph/host/entry_kernel.h`](../xbox-test/uwph/host/entry_kernel.h): `process` | The guest `CreateProcessW` route clears the result and returns `ERROR_ACCESS_DENIED`; isolated Steam/CEF child-process execution is not supplied. |

These are source observations, not fresh console results. Changes on other
branches need their own integration evidence; do not import historical Play or
smoke results as certification of this source or a future package.

The earlier research's `tools/steam-map` analyzers, probes and process fixtures
are **not included in this public tree**. They are not prerequisites developers
can run from this checkout. Their publication scope remains undecided; this guide
neither restores them nor treats their historical outputs as reproducible public
test evidence. No Valve client build is pinned by this document or by the
repository's current [`deps.pin`](../deps.pin). External comparison revisions
in [GAMENATIVE-STEAM.md](GAMENATIVE-STEAM.md) are source citations, not shipped
dependencies or a verified client ABI.

## Native Steam prerequisites

Valve's [Steamworks API overview](https://partner.steamgames.com/doc/sdk/api)
lists a running Steam client, a discoverable App ID, the same OS user/privilege
context, a license on the active account, and appropriate app release/package
configuration among initialization requirements. A development App ID hint
identifies the app; it does not grant ownership. Never fabricate registry
identity, user/pipe handles, interface objects or successful initialization.

After genuine initialization, deliver callbacks and asynchronous call results
according to the **actual original API and binding used by the game**. Preserve
callback IDs, payload layout, result correlation and buffer lifetimes. A compiled
wrapper or static call edge does not establish that it executes during startup;
calling a callback pump alone does not prove that a callback was delivered.
Do not derive game-specific fixes from generated disassembly.

Valve's [authentication guide](https://partner.steamgames.com/doc/features/auth)
describes session/encrypted tickets as identity and ownership evidence for
peers, servers or backends. It does not document a ticket-only replacement for
the client/runtime interfaces. Launcher CM login, a depot key and an app ticket
are different artifacts, none equivalent to a native game's live Steam context.
No supported launcher-session import is established here: plan a separate
genuine-client sign-in and let that client retain its own private session.

## Xbox compatibility gates

Validate prerequisites before staging or authenticating the client. These are
**required observations, not existing public fixture commands or passed gates**:

1. **Guest processes:** demonstrate parent/child execution with isolated writable
   state, including overlapping preferred image addresses, process IDs, TLS,
   exception/module state, inherited handles, exit codes and wait/cleanup.
   Relocating a second image into a thread is not equivalent to process isolation.
2. **Internal IPC:** demonstrate cross-process named-pipe discovery/request/reply,
   shared memory, events, overlapped completion, cancellation, reconnect and peer
   teardown in one guest namespace. Use supported UWP mechanisms; do not depend
   on AF_UNIX or unsupported local listeners. Test outbound Steam connectivity
   separately from local IPC; a local-listener failure says nothing by itself
   about a remote port.
3. **Complete client lifecycle:** record the tested official client build, hashes,
   PE machine types and exercised child processes/imports, including CEF. Trace
   actual failures before implementing compatibility behavior. Static imports
   are only a lower bound, not an executed-call trace. Test update/restart and
   ordinary service-free behavior; do not invent service success or assume every
   shipped x86 helper is indispensable. Conversely, an x64 main executable does
   not establish an x64-only lifecycle.
4. **Client UI and authentication:** render genuine login on Xbox with usable
   input and certificate validation intact; authenticate, display the owned
   library and perform a client-managed download of the test game. Do not copy
   a scratch host's session files or inject launcher credentials.
5. **Game/client integration:** launch the original game/API in the client's guest
   namespace, retain the client for the game's lifetime, and establish real
   initialization, account/app entitlement, discovery and callback delivery.
   Teardown must release resources without revoking the user's credentials.

Microsoft documents
[`FullTrustProcessLauncher`](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslauncher)
for the Windows Desktop Extension SDK with `runFullTrust`, and
[multi-instance UWP](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/multi-instance-uwp)
for desktop targets. Neither documents a manifest shortcut to Xbox desktop child
processes. A supported guest process model remains an architectural prerequisite;
changing a denial to success or weakening Xbox/Dev Mode security is not a solution.

Budget for browser helpers: Valve
[retired `-no-browser` and `-noreactlogin`](https://steamcommunity.com/groups/SteamClientBeta/discussions/3/3710433479207750727/).
Do not assume `-silent` or disabling GPU acceleration removes CEF. Valve's
[December 19, 2025 client update](https://steamcommunity.com/ogg/593110/announcements/detail/528740542771627405)
([public news API text](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=593110&count=20&maxlength=0&enddate=1766534400&feeds=steam_community_announcements))
announces a 64-bit Windows client, not a per-helper architecture guarantee or
Xbox compatibility. Inspect the actual tested installation.

## Acceptance and evidence

Use [`xbox-test/tools/play-test.sh`](../xbox-test/tools/play-test.sh) with app ID
`3405340` for launcher acceptance, under the shared console-lock protocol in
[`dp-lib.sh`](../xbox-test/dp/dp-lib.sh). This is future on-device validation,
not something performed by this documentation change. Its **exit 0 means a
launch attempt and GAP summary were reported**, not that the game or Steam works.
Check fresh session logs and result timestamps against the tested source/package;
a stale `launch_result.json` is not evidence.

Record genuine native initialization and interface availability, the current
app's entitlement in the real client, and receipt/handling of an identified
callback or asynchronous result. For the online acceptance case, record
[`BLoggedOn`](https://partner.steamgames.com/doc/api/ISteamUser#BLoggedOn) separately:
it reports connectivity, not ownership, and Valve does not require every game
to stop when offline. Use the documented
[current-app subscription semantics](https://partner.steamgames.com/doc/api/ISteamApps#BIsSubscribed);
a boolean alone is not independent proof of the complete integration.

Then satisfy **all** of [`docs/PLAYABLE.md`](../docs/PLAYABLE.md): actual guest
execution, visible stable menu, labelled real/synthetic controller input,
non-zero audio and a playable run for at least five minutes with measured FPS.
Run unchanged [`xbox-test/smoke-all.sh`](../xbox-test/smoke-all.sh) in full for
on-device changes; `--offline` omits console gates and cannot certify them.
No failed or skipped prerequisite becomes a pass.

Keep raw captures, QR challenges, account identifiers, session files, credentials
and generated analysis local. Public evidence should give source/package identity,
account-free artifact basenames, checks actually run and precise failures—not raw
account-bearing logs. Do not commit game/Valve binaries or inventories of private
installations. No Valve/game-file patching or Xbox security bypass is part of this
design. (Policy since 2026-11-05: an emulated Steam API for owned games is also
allowed, with achievements and cloud saves synced to the real account; see CONTRIBUTING.md.)
