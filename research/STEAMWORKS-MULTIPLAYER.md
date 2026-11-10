# Steamworks multiplayer: what the owned games use, and what can serve it

The lobby wire layer this map describes is implemented in
`app/steam_lobbies.h`; the route decision and the off-device evidence for
hosting Valve's client in-process are in
[STEAM-REAL-ONLINE.md](STEAM-REAL-ONLINE.md).

This is the map for issue #55 part 2: which Steamworks interfaces the owned
games actually reference, what the pinned Steam API provider (gbe_fork) already
serves, what a launcher-side CM bridge could serve against the real Steam
network, and what would need Valve's own client. It is a plan, not a
compatibility claim: nothing here has been run on the console, and a static
string in a binary is a lower bound on what the game calls, never proof that a
call executes.

Scope rule (AGENTS.md rule 3): every path below runs a game the signed-in
account owns, installed from official depots. No path patches a game or Valve
file on disk, and every path keeps the ownership gate
(`owner_ticket()` → `ClientGetAppOwnershipTicket`, EMsg 857/858) in front of
anything that is staged.

## Method and limits

- Inventory: `strings`/byte scan of the game's own binaries under
  `/home/jobu/steam-owned/games/*`, excluding `steam_api*.dll`,
  `steamclient*.dll`, `steamclient_loader*` and `GameOverlayRenderer*` — those
  contain every symbol by construction and would only report the SDK's surface.
  Matched `SteamAPI_ISteam<Interface>_<Function>` and `Steam<Interface>_vNNN`
  version strings.
- Provider coverage: the pinned gbe_fork release from `deps.pin`
  (`gbe-fork-win`, `release-2026_09_27`), inspected as a PE export table, its
  shipped `steam_settings.EXAMPLE` and `README.release.md`, and a read-only
  checkout of the same tag's source (`git describe` = `release-2026_09_27`,
  commit `7103add7`). Statements about gbe's internals below cite the file they
  come from; no gbe code is copied into this repository.
- CM message names and field numbers: SteamDatabase/Protobufs as vendored in
  `node_modules/steam-user/protobufs/*.proto` (steam-user 5.3.0, MIT). The
  `.proto` files themselves are Valve's published protocol definitions
  (SteamDatabase/Protobufs is public domain / Unlicense); steam-user only
  repackages them. See [Proto sources and licences](#proto-sources-and-licences).

## What each owned game references

| Game | App | Steam binaries | Multiplayer-relevant surface |
|---|---:|---|---|
| Megabonk | 3405340 | `GameAssembly.dll` (IL2CPP, 894 Steam API symbols) | Everything: `ISteamMatchmaking` (38), `ISteamMatchmakingServers` (17), `ISteamNetworking` (22), `ISteamNetworkingSockets` (47), `ISteamNetworkingMessages` (6), `ISteamNetworkingUtils` (27), `ISteamFriends` (80, incl. rich presence and invites), `ISteamUser` (35, incl. `GetAuthSessionTicket`, `BeginAuthSession`, `AdvertiseGame`), `ISteamParties` (12), `ISteamGameSearch` (14), `ISteamRemotePlay` (8), `ISteamGameServer` (41), `ISteamUserStats` (45), `ISteamUGC` (94), `ISteamInventory` (38), `ISteamRemoteStorage` (59) |
| Fields of Mistria | 2142790 | `FieldsOfMistria.exe`, `SDL3.dll` | `ISteamNetworkingSockets::CloseConnection`/`CloseListenSocket` only (teardown), `ISteamUserStats` (4), `ISteamApps`, `ISteamUtils`, `ISteamScreenshots`; SDL3 uses `ISteamRemoteStorage` for cloud |
| Mega Man Legacy Collection | 363440 | `Proteus.exe` (PE32), `steam_api.dll` | 32-bit: maps for inspection only, no execution yet, so no runtime surface |
| Puyo Puyo Tetris 2 | 1259790 | `PuyoPuyoTetris2.exe` | Imports only the flat C API (`SteamAPI_Init`, `SteamAPI_RunCallbacks`, `SteamInternal_*`), no `SteamAPI_ISteam*` names; carries the `steam_display` rich-presence key. Interfaces are reached through `SteamInternal_FindOrCreateUserInterface` at runtime, so the static surface under-reports |
| Hyperdimension Neptunia Re;Birth1 | 282900 | `xgameFinal.exe` | Same flat C API shape as PPT2; no `SteamAPI_ISteam*` names |
| Slime Rancher | 433340 | `Managed/Assembly-CSharp.dll` | Managed `Steamworks` bindings, `SetRichPresence` + a `RichPresenceDirector`; no Steam DLL ships in the install (Unity loads it from the client) |
| Say No! More | 1191900 | — | No Steam API references in its own binaries |

Two consequences worth stating plainly:

1. **Only Megabonk exercises the multiplayer surface broadly.** Mistria's
   networking calls are teardown-only, and the rest either reach Steam through
   the flat C API (so the interface set is decided at runtime) or do not
   reference Steam at all. A CM bridge built for lobbies is a Megabonk-shaped
   investment first.
2. **IL2CPP strings over-report.** `GameAssembly.dll` contains the generated
   bindings for the full Steamworks surface the game's C# assembly references,
   including wrappers the game may never call. The list above is "compiled in",
   not "called".

## Presence (part 1 of #55)

Presence is the one integration that needs no game cooperation beyond
`ISteamFriends::SetRichPresence`, and it is implemented in this branch
(`app/steam_presence.h`, `app/launch.h`, `app/host/play_runtime.h`). The model:

| What friends see | Where it comes from | Message |
|---|---|---|
| "In-Game: <game>" | the launcher's own CM session | `CMsgClientGamesPlayed` (EMsg 5410), `games_played[1].game_id[2]`, `client_os_type[2]` |
| The online dot | persona state | `CMsgClientChangeStatus` (EMsg 716), `persona_state[1] = 1` |
| Rich-presence line | per-app key/value set | `CMsgClientRichPresenceUpload` (EMsg 7501), `rich_presence_kv[1]`, header `routing_appid[3]` |

The blob is Steam's binary KeyValues: `\0` + `"RP"` + `\0`, then per key
`\1` + key + `\0` + value + `\0`, then `\x08\x08`. Steam drops both the game
state and the key set when the session that sent them ends, so the keeper
re-sends on every new session; `CMsgClientPlayingSessionState` (EMsg 9600,
`playing_blocked[2]`, `playing_app[3]`) says when another of the account's
clients is playing and this session must stay quiet (sending games-played while
blocked logs the session off).

gbe_fork does not help here: its `SetRichPresence` keeps the set in memory and
broadcasts it only to its own LAN peers, and its release build compiles the
debug print out. There is no settings file or saved blob to read. That is why
the launcher observes the call at the export boundary instead of reading
provider state.

The observation covers the **flat export** `SteamAPI_ISteamFriends_SetRichPresence`
(static import or `GetProcAddress`) and its `ClearRichPresence` sibling. That is
the path the games that set rich presence take: Megabonk's IL2CPP
`GameAssembly.dll` carries the export name and binds it at runtime, and Slime
Rancher's Steamworks.NET assembly P/Invokes the same flat export. A game built
against the C++ SDK calls `SteamFriends()->SetRichPresence(...)`, which is an
inline vtable call on the object `SteamInternal_FindOrCreateUserInterface`
returned — that never passes an export, so it would not be observed. Covering it
would mean rewriting a slot in the provider's own vtable at runtime; not done
here, and no owned game needs it today.

The source confirms it (`dll/steam_friends.cpp`, tag `release-2026_09_27`):
`SetRichPresence` writes `us.mutable_rich_presence()` and calls
`resend_friend_data()`, which only sets `modified = true`; `RunCallbacks()`
then builds a `Friend` protobuf and calls `network->sendToAllIndividuals(&msg,
true)` — gbe's own UDP peer network, nothing to do with Steam's CM. The
`PRINT_DEBUG` that would have exposed the values is compiled out of the release
build. Nothing is persisted, so there is no file for the launcher to watch.

## Leaderboards (part 2 of #55)

The owner's check: launch Megabonk, open the Rank screen in the main menu, and
other players' entries should be listed. They are not.

**What the game asks for.** Megabonk's IL2CPP metadata carries the flat export
names `SteamAPI_ISteamUserStats_FindLeaderboard`,
`..._FindOrCreateLeaderboard`, `..._DownloadLeaderboardEntries`,
`..._GetDownloadedLeaderboardEntry` and `..._UploadLeaderboardScore`, plus the
board-name literals `kills`, `kills_weekly`, `friends` and a `v2_` prefix, and a
serialized default blob `\x04 v2 | \x0a kills | \x18 kills_weekly | \x0e friends`.
The names it passes to Steam are therefore most likely `v2_kills`,
`v2_kills_weekly` and `v2_friends` — a hypothesis until the console log shows the
seed line for each. (The 2026-11-09 console run settled it: the real names are
`v2_kills`, `v2_kills_friends`, `v2_kills_weekly`, `v2_kills_weekly_friends` and
`banned_players`; see the console section at the end.)

**Why the board is empty today.** gbe_fork answers `FindLeaderboard` from its own
cache (`dll/steam_user_stats_leaderboard.cpp`, tag `release-2026_09_27`):

1. `find_leaderboard` looks the name up in `cached_leaderboards`; a miss calls
   `cache_leaderboard_ifneeded`, which builds the board and calls
   `load_leaderboard_entries(name)` **once**, on that first Find in the process.
2. `load_leaderboard_entries` reads
   `<local_save_path>\<appid>\leaderboard\<lowercased name>` and nothing else.
3. `DownloadLeaderboardEntries` answers from that in-memory vector.

With `disable_networking=1` (which the launcher sets) and no LAN peers, no file
is ever written, so the board is empty. The console evidence matches: the
leaderboard folder does not exist under
`LocalState\steam\3405340\saves\3405340\`, and `launch_probe.log` has zero
`FindLeaderboard`/`ISteamUserStats` hits. The game's own "Error when finding
leaderboard" string is the path it takes.

**The route.** Option (b), the launcher's CM bridge, with the provider's own
cache file as the delivery surface. Three ways to get the CM answer into the
provider were considered:

| Delivery surface | How it would work | Why not (or why) |
|---|---|---|
| `steam_settings` | a file the provider reads at init | gbe_fork has no setting for leaderboard rows. `leaderboards.txt` names a board and its sort/display method only (`parse_leaderboards`, `dll/settings_parser.cpp`), and `disable_leaderboards_create_unknown` only changes whether an unknown board is created. No key supplies entries. Not a channel. |
| CallResult injection in Play | synthesise `LeaderboardFindResult_t` / `LeaderboardScoresDownloaded_t` into the provider's callback queue | the queue is the provider's private state; reaching it means knowing its internal layout, and the game's own `DownloadLeaderboardEntries` would still be answered from the provider's empty vector. It replaces the provider rather than feeding it. |
| **the provider's cache file** (chosen) | write the rows where the provider already looks, before its first Find | it is the provider's documented input (`load_leaderboard_entries`), it needs no internal knowledge, the provider's own callbacks still run unchanged, and a wrong guess fails safe: the game sees exactly what it sees today. |
| a thin `ISteamUserStats` shim in `stage_steam_provider` | answer the leaderboard exports ourselves instead of forwarding | it would own the whole interface (handles, callbacks, every other `ISteamUserStats` call the game makes) for one family, and would diverge from the provider on stats/achievements that already work. Kept as the fallback if the cache file proves insufficient. |

The cache file is the smallest surface that is still the provider's own code
path:

1. Play observes the provider's `FindLeaderboard`, `FindOrCreateLeaderboard` and
   `UploadLeaderboardScore` exports, the same export-tap mechanism rich presence
   already uses (`play_runtime::steamApiExport`).
2. On the game's `Find`, **before** the provider is called, the launcher asks
   Steam for the board and its top rows over its own `CmSession`:
   `CMsgClientLBSFindOrCreateLB` (5416) → `CMsgClientLBSFindOrCreateLBResponse`
   (5417), then `CMsgClientLBSGetLBEntries` (5418) →
   `...GetLBEntriesResponse` (5419), with the header's `routing_appid` set to the
   game's app id.
3. It writes the rows into the provider's own leaderboard file in the provider's
   own format — flat little-endian `uint32`: steam id low, steam id high, score,
   details count, details… — then lets the provider's `Find` run.
4. The provider reads that file on its first Find, sorts it, and answers the
   game's `DownloadLeaderboardEntries` with real players. The game's call still
   runs, still receives the provider's handle, and no game or Valve file is
   touched.
5. On `UploadLeaderboardScore`, after the provider answers, the launcher sends
   `CMsgClientLBSSetScore` (5414) with the board id the seed learned and the
   game's own upload method (keep-best / force-update), so the player's score
   reaches their real account. The board is identified by asking the provider
   itself (`GetLeaderboardName`) what the handle the game passed names: `Find`
   returns a `SteamAPICall_t`, while the game uploads with the `SteamLeaderboard_t`
   from `LeaderboardFindResult_t`, and the two are different values.

The ordering is the whole design: a background fetch after the Find can never be
seen, because the provider never re-reads the file. The seed therefore runs on
the game's thread, bounded by a 3 s CM timeout per request, and only once per
board per launch (a board Steam does not have is remembered as such and not
asked again). A board Steam *does* have keeps its id even when the rows request
fails, so a score for it is still uploaded that launch; only the rank list is
missing.

| EMsg | Message | Fields used |
|---:|---|---|
| 5416 / 5417 | `CMsgClientLBSFindOrCreateLB` / `...Response` | request `app_id[1]`, `leaderboard_sort_method[2]`, `leaderboard_display_type[3]`, `create_if_not_found[4]`, `leaderboard_name[5]`; response `eresult[1]`, `leaderboard_id[2]`, `leaderboard_entry_count[3]`, `leaderboard_sort_method[4]`, `leaderboard_display_type[5]`, `leaderboard_name[6]` |
| 5418 / 5419 | `CMsgClientLBSGetLBEntries` / `...Response` | request `app_id[1]`, `leaderboard_id[2]`, `range_start[3]`, `range_end[4]`, `leaderboard_data_request[5]`; response `eresult[1]`, `leaderboard_entry_count[2]`, `entries[3]` (`steam_id[1]`, `global_rank[2]`, `score[3]`, `score_details[4]`) |
| 5414 / 5415 | `CMsgClientLBSSetScore` / `...Response` | request `app_id[1]`, `leaderboard_id[2]`, `score[3]`, `score_details[4]`, `upload_score_method[5]`; response `eresult[1]`, `leaderboard_entry_count[2]`, `score_changed[3]`, `global_rank_previous[4]`, `global_rank_new[5]` |

**What this does not do.** The board names are a hypothesis until a console run
shows them. `FindOrCreateLeaderboard` with `create=false` (which is what
`FindLeaderboard` becomes) may answer `k_EResultNoMatch` for a board the account
has never seen, in which case the game sees the empty board it saw before.
Only the first Find of a board in a process benefits. And the seed blocks the
game's thread for one CM round trip per board it asks for.

Two gaps are known and are *not* fixed by the cache-file route. Both are
visible on the Rank screen, so a console run should be read with them in mind
rather than as "the bridge did nothing":

1. **The AMIGOS tab will show global players, not friends** (read from the
   provider's source, not yet seen on console: reaching it needs a right-press
   inside the guest and no pad was connected in the runs so far). gbe's
   `DownloadLeaderboardEntries` treats `k_ELeaderboardDataRequestFriends` as
   "all rows in the cache" (`dll/steam_user_stats_leaderboard.cpp`: the range
   clamp is skipped for the friends request, then
   `m_cEntryCount = entries.size()`), so the tab is not empty — it lists
   everyone the seed wrote. `DownloadLeaderboardEntriesForUsers` does filter:
   it counts only users already in that same vector. The cache file carries no
   friend flag, so the bridge cannot make the friends request narrower than the
   global one. Serving it properly means asking CM for `DataFriends` rows per
   request — i.e. the request has to be observed at
   `DownloadLeaderboardEntries`, not only at Find, which is the shim route
   below.
2. **Ranks are 1..N, not Steam's ranks.** gbe derives `m_nGlobalRank` from the
   entry's index in its sorted array (`1 + (&entry - &entries[0])`), not from
   Steam's `global_rank`, and the cache file has no field for it. That is right
   for the top of the board and wrong for the user's own row whenever they are
   not inside the seeded slice.

The GLOBAL tab — the owner's stated check — is unaffected by the first gap: it
lists real players' scores and ranks, and it is the tab the console runs above
captured. The AMIGOS tab shares the same seeded rows, so it is expected to list
them too; that is read from the provider's source, not yet photographed.

A third gap, the "Unknown User" player column, is fixed: gbe's
`GetFriendPersonaName` answers only a `Friend` record from its own LAN peer
network (`dll/steam_friends.cpp`), so a real Steam row's steam id had no name
behind it. The launcher now asks Steam for those names on its own CM session and
answers the friends exports in memory — see
[Persona names on those rows](#persona-names-on-those-rows). Console proof:
0.5.256.0 showed real names on all ten Rank rows (below).

### Refreshing rows after the first Find

The provider never re-reads a board's file, so a later Find cannot refresh it.
The launcher therefore keeps the freshest rows itself and hands them to the game
through a second export tap, `GetDownloadedLeaderboardEntry`:

- A Find inside the bridge's TTL (30 s) answers from the rows already held and
  asks Steam nothing.
- A Find past the TTL refetches `CMsgClientLBSGetLBEntries` and replaces the
  held rows.
- A successful `UploadLeaderboardScore` marks the board dirty, so the next Find
  refetches at once, inside the TTL: the owner sees their own new score and rank
  without waiting.
- A failed refresh keeps the previous rows and logs
  `refresh unavailable … keeping N rows`; the game is never shown less than it
  already had.
- A successful fetch that returns no rows is Steam's answer, not a failure: a
  weekly board that reset really is empty, so the held rows are replaced with
  the empty set, the provider's cache file is emptied, and a download of that
  board returns no entries.

The tap calls the provider's own `GetDownloadedLeaderboardEntry` first and then
overwrites `user`, `rank`, `score` and `details` in the entry struct the game
passed, only when the launcher holds rows for the board the provider's handle
names. It patches only when the launcher's row carries the same global rank as
the index the game asked for, so a shorter refresh or a ranged download cannot
mix rows from different positions; an index past the launcher's rows reports no
entry, because that player is no longer on the board. The provider's call, its
handle and its callbacks are unchanged.

One limit is the provider's, not the tap's: the game loops
`index < m_cEntryCount`, and that count is fixed by the provider's own
`DownloadLeaderboardEntries` from its in-memory copy
(`cached_leaderboards[handle - 1].entries.size()`) and delivered in its
CallResult. The tap can replace rows the provider reports; it cannot report
more. A board that **grew** since its first Find therefore shows only the rows
the provider knew at that Find. A board that was **empty** at its first Find
stays empty for the launch, with one exception: this account's own upload,
which the provider appends to its copy itself
(`UploadLeaderboardScore` → `update_leaderboard_entry`), so the owner's row
appears. Growing the count for other players would mean writing into the
provider's board, which only its own score-upload path may do.

### Persona names on those rows

With the rows in place, every row still drew "Unknown user". The game resolves a
row's name through `ISteamFriends`, and gbe_fork answers from a friend list that
only ever holds peers on its own LAN network
(`dll/steam_friends.cpp`, tag `release-2026_09_27`):

- `GetFriendPersonaName` returns the literal `"Unknown User"` unless the SteamID
  is the local user or is in `friends` (`find_friend`), and `friends` is filled
  only from gbe's own UDP peer callbacks — empty under `disable_networking=1`.
- `RequestUserInformation` is `PRINT_DEBUG_TODO()` and returns `false`.
- `GetPersonaName` returns the staged `account_name` (`Player` by default).

The launcher answers the same three exports from its own signed-in session:

1. After a seed, it sends `CMsgClientRequestFriendData` (EMsg 815) with
   `persona_state_requested` = `Status | PlayerName` and the SteamIDs of the rows
   just fetched, in batches of 100. The message has no job id: the answer is an
   unsolicited `CMsgClientPersonaState` (EMsg 766) push, which the launcher's
   existing presence push handler already receives on that session.
2. `CMsgClientPersonaState.friends[]` carries `friendid[1]`, `persona_state[2]`
   and `player_name[15]`; each name is stored against its SteamID. Only a push
   that mentions one of the SteamIDs this request asked for counts as its
   answer: the same session also carries persona state for the account itself,
   for friends and for other lookups, and one of those arriving first must not
   end the wait before the names do.
3. `GetFriendPersonaName` returns the launcher's name when it has one and the
   provider's own answer otherwise; `GetPersonaName` returns the account's own
   name once Steam answered for it. `RequestUserInformation` answers `false`
   ("known, call the getters now") in every case: a name the launcher holds is
   known, and one it does not is queued for the keeper and reported as known
   too. Answering `true` ("in flight, wait for `PersonaStateChange_t`") would
   strand a game that waits on that callback, because Play does not post it —
   the provider owns the callback plumbing. The getters are called instead and
   return the provider's answer until Steam's name lands, at which point the tap
   hands out the real one.
4. The seed waits on the game's thread only for a bounded time: at most
   `SeedWaitMs` (250 ms) for Steam's first push that mentions a requested
   SteamID, then a 500 ms quiet window after each such push, and never past the
   caller's own timeout. The game is drawing behind this call, so a Find that
   has to wait for Steam costs a fraction of a second rather than the two
   seconds the caller asks for; a name that lands after the seed returned is
   still handed out by the tap on the game's next draw and still cached for the
   next launch. A push that mentions a requested SteamID but carries no name
   (private profiles) still ends the wait, so a rank screen is not stalled for
   accounts Steam will not name, and a reply slower than the quiet window is
   still waited for.
5. Names are kept in the app's own `LocalState` (`persona_names.txt`, hex-encoded
   so a name with a space or newline cannot corrupt the record, 7-day TTL) and
   adopted at launch, so a row drawn before Steam answers still reads correctly.
   The file is private on-device state: never logged, never committed.
6. The request set is bounded by distinct SteamIDs (2000 per launch): a rank
   screen polling the same unresolved rows adds nothing.
7. Log lines carry counts only (`persona: N of M leaderboard names resolved`);
   no SteamID and no persona name is ever written to a log.

The provider's own call always runs and its answer is replaced only when the
launcher has one. Nothing on disk changes.

| EMsg | Message | Fields used |
|---:|---|---|
| 815 | `CMsgClientRequestFriendData` | `persona_state_requested[1]`, `friends[2]` (repeated `fixed64`) |
| 766 | `CMsgClientPersonaState` (unsolicited push) | `status_flags[1]`, `friends[2]` → `friendid[1]`, `persona_state[2]`, `player_name[15]` |

**What this does not do.** Avatars (`GetMediumFriendAvatar`,
`AvatarImageLoaded_t`) are not served yet; a game that draws one gets the
provider's answer. A name Steam does not return stays the provider's "Unknown
User". And Steam answers a persona request for any SteamID the account can see,
so the launcher only ever asks for SteamIDs that came from a board the game
itself requested.

## The three serving options

For each interface family:

- **(a) gbe_fork offline** — the provider answers from local state. Works with
  no network and no Valve client; peers are only the ones on the same LAN.
- **(b) launcher CM bridge** — the launcher translates the game's Steamworks
  call into a CM message on its own signed-in session, and feeds the reply back
  through the provider's callback queue. Real Steam lobbies, real friends, real
  auth tickets; needs the launcher to own the interface object the game talks
  to, or to fill in what the provider leaves empty.
- **(c) Valve's client** — the real Steam client owns the interface. Not
  available on the console (no supported guest process model; see
  [STEAM-API-PATH.md](STEAM-API-PATH.md)).

| Interface | Games | (a) gbe_fork today | (b) CM bridge | (c) Valve client |
|---|---|---|---|---|
| `ISteamFriends` rich presence | Megabonk, Slime Rancher, PPT2 | in-memory + LAN only | **done in this branch**: observed at the export, uploaded on the launcher session | — |
| `ISteamFriends` persona names | Megabonk | local account only; every other id is "Unknown User" unless a LAN peer pushed a `Friend` record | **done**: `CMsgClientRequestFriendData` for the seeded rows' ids, names collected from the `CMsgClientPersonaState` push and served through an export tap on `GetFriendPersonaName` / `GetPersonaName` / `RequestUserInformation`; a simple version of the tap was console-verified in 0.5.256.0, the wait-and-cache version here is off-device only so far | — |
| `ISteamUserStats` leaderboards | Megabonk | reads `<appid>\leaderboard\<name>` on a board's first Find and answers downloads from it; never writes it with `disable_networking=1`, so boards are empty | **done in this branch**: observed at the export, board + top rows fetched on the launcher session and written into the provider's own file before its first Find; scores uploaded as `CMsgClientLBSSetScore` | — |
| `ISteamFriends` invites / `GameLobbyJoinRequested` | Megabonk | overlay/invite handling is experimental-build only; `auto_accept_invite.txt` exists | needs `CMsgClientMMSInviteToLobby` (6621) and the `ClientMMSLobbyChatMsg` (6614) path to raise callbacks | — |
| `ISteamMatchmaking` lobbies | Megabonk | LAN-only lobbies over its own UDP port (`listen_port`, default 47584): `CreateLobby` queues a pending create and `create_pending_lobbies()` mints a local lobby id, answers `LobbyCreated_t`/`LobbyEnter_t` itself and broadcasts the lobby over gbe's network (`dll/steam_matchmaking.cpp`); `disable_lobby_creation` makes it fail instead | **feasible**: the 14 messages below map almost 1:1 to the API | — |
| `ISteamMatchmakingServers` | Megabonk | returns LAN server list; `matchmaking_server_list_actual_type` and `..._via_source_query` change the list type | not a CM thing: it is a UDP server query (`A2S`), which the console can do over its own sockets if the AppContainer allows outbound UDP | — |
| `ISteamNetworking` (old P2P) | Megabonk | LAN P2P over its own sockets | needs `CMsgClientP2PConnectionInfo` (5434) rendezvous and a relay; old API is deprecated by Valve | — |
| `ISteamNetworkingSockets` | Megabonk, Mistria | exports exist (`ConnectP2P`, `InitAuthentication`, `GetCertificateRequest`, `ReceivedRelayAuthTicket`, `FindRelayAuthTicketForServer`) but they are stubs: `GetCertificateRequest`/`SetCertificate` return `false` with `PRINT_DEBUG_TODO`, `ReceivedRelayAuthTicket` returns `false`, `FindRelayAuthTicketForServer` returns `0`, and `ConnectP2P` only opens gbe's own UDP path (`dll/steam_networking_sockets.cpp`). No SDR datagram protos are compiled in either (`CMsgSteamDatagramConnectRequest`, `CMsgSteamDatagramP2PSessionRequest`, `CMsgSteamDatagramRelayAuthTicket` are all absent from the DLL), so it cannot speak SDR | **large**: SDR is a whole transport (certificates, relay tickets, ping/pop selection, encryption). `CMsgClientNetworkingCertRequest` (5621/5622) is only the first step | — |
| `ISteamNetworkingMessages` | Megabonk | sits on NetworkingSockets, so same answer | same as SDR | — |
| `ISteamUser` auth tickets | Megabonk | gbe generates and signs its own ticket with a fake key (`dll/auth.cpp`, `app_ticket_key`, `sign_auth_data`), and `BeginAuthSession` validates only tickets that same key signed — so it works between emulated peers and nowhere else | **client side only, and not by reuse**: gbe's ticket is not a Valve ticket, so it cannot be registered with CM as one. A real ticket would have to come from CM for the app, which means the launcher owns `GetAuthSessionTicket` too. Validating *other* players' tickets is the game-server side | — |
| `ISteamParties` / `ISteamGameSearch` | Megabonk | exports present, LAN-scoped | parties ride lobbies (`CMsgClientMMS*`); GameSearch is a Valve-hosted matchmaking service the launcher would have to call as the client | — |
| `ISteamRemotePlay` | Megabonk | exports present, no-op | needs the Steam client's streaming stack | required |
| `ISteamUGC` / `ISteamInventory` | Megabonk | file-backed workshop/inventory from `steam_settings` | UGC can use the published Web API (`ISteamRemoteStorage/GetPublishedFileDetails`); inventory is a GC service | — |
| `ISteamGameServer` | Megabonk, Mistria | local emulation | a real game server needs a server login (`CMsgClientLogon` with a server account) — out of scope for a player launcher | required |

## CM messages a lobby bridge would use

All names and numbers from `enums_clientserver.proto` and
`steammessages_clientserver_mms.proto`:

| EMsg | Message | Fields that matter |
|---:|---|---|
| 6601 / 6602 | `CMsgClientMMSCreateLobby` / `...Response` | `app_id[1]`, `max_members[2]`, `lobby_type[3]`, `lobby_flags[4]`, `cell_id[5]`, `metadata[7]`; response `steam_id_lobby[2]`, `eresult[3]` |
| 6603 / 6604 | `CMsgClientMMSJoinLobby` / `...Response` | `steam_id_lobby[2]`; response carries `members[9]` (`steam_id[1]`, `persona_name[2]`, `metadata[3]`), `steam_id_owner[7]`, `metadata[8]` |
| 6605 / 6606 | `CMsgClientMMSLeaveLobby` / `...Response` | `steam_id_lobby[2]` |
| 6607 / 6608 | `CMsgClientMMSGetLobbyList` / `...Response` | `num_lobbies_requested[3]`, `filters[6]` (`key[1]`, `value[2]`, `comparision[3]`, `filter_type[4]`); response `lobbies[4]` (`steam_id[1]`, `max_members[2]`, `lobby_type[3]`, `lobby_flags[4]`, `metadata[5]`, `num_members[6]`) |
| 6609 / 6610 | `CMsgClientMMSSetLobbyData` / `...Response` | `steam_id_member[3]` for member data, `metadata[7]` |
| 6611 / 6612 | `CMsgClientMMSGetLobbyData` / `CMsgClientMMSLobbyData` | the pushed form (`6612`) is unsolicited: it must reach the game as a `LobbyDataUpdate_t` |
| 6613 / 6614 | `CMsgClientMMSSendLobbyChatMsg` / `CMsgClientMMSLobbyChatMsg` | `lobby_message[4]`; the pushed form carries `steam_id_sender[3]` |
| 6615 / 6616 | `CMsgClientMMSSetLobbyOwner` / `...Response` | `steam_id_new_owner[3]` |
| 6617 / 6618 | `CMsgClientMMSSetLobbyGameServer` / `...LobbyGameServerSet` | `game_server_ip[6]` (`CMsgIPAddress`), `game_server_port[4]`, `game_server_steam_id[5]` |
| 6619 / 6620 | `CMsgClientMMSUserJoinedLobby` / `...UserLeftLobby` | pushed; raise `LobbyChatUpdate_t` |
| 6621 | `CMsgClientMMSInviteToLobby` | `steam_id_user_invited[3]` |
| 6624 | `CMsgClientMMSSetLobbyLinked` | `steam_id_lobby2[3]` |
| 6626 / 6627 | `CMsgClientMMSGetLobbyStatus` / `...Response` | `claim_membership[3]`, `claim_ownership[4]`, `lobby_status[3]` (`EMMSLobbyStatus`) |

Auth and networking neighbours:

| EMsg | Message | Use |
|---:|---|---|
| 857 / 858 | `CMsgClientGetAppOwnershipTicket` / response | the existing ownership gate; unchanged |
| 5502 | `CMsgClientRegisterAuthTicketWithCM` | register the ticket gbe handed the game so a real server can validate it |
| 5431 / 5432 / 5575 | `CMsgClientRequestAuthList` / `CMsgClientAuthList` / `...Ack` | the client's ticket list to CM |
| 5429 | `CMsgClientTicketAuthComplete` | result of a ticket CM validated |
| 5526 / 5527 | `CMsgClientRequestEncryptedAppTicket` / response | `GetEncryptedAppTicket` |
| 5621 / 5622 | `CMsgClientNetworkingCertRequest` / response | first step of SDR: `key_data[2]`, `app_id[3]` → `CMsgSteamDatagramCertificateSigned` |
| 5434 / 5435 | `CMsgClientP2PConnectionInfo` / `...FailInfo` | old P2P rendezvous (`candidate[4]`, `rendezvous[6]`) |

SDR (for `ISteamNetworkingSockets`) additionally needs
`CMsgSteamDatagramRelayAuthTicket`, `CMsgSteamDatagramConnectRequest`,
`CMsgSteamDatagramConnectOK`, `CMsgSteamDatagramP2PSessionRequest`,
`CMsgSteamDatagramP2PRoutingSummary` and the ping/pop messages in
`steamdatagram_messages_auth.proto` / `steamdatagram_messages_sdr.proto`.

## Effort and risks

| Slice | Effort | Main risk |
|---|---|---|
| Rich presence (this branch) | done | Steam may ignore `routing_appid` for an app the session did not launch through the client; the console log says whether it did |
| Leaderboards (this branch) | done for the read path and score upload | the board names are a hypothesis until the console log shows them; `FindLeaderboard` becomes `FindOrCreateLeaderboard(create=false)`, which may answer `k_EResultNoMatch` for a board the account has never seen; the seed blocks the game's thread for one CM round trip per board |
| Lobby list / create / join | medium: 14 messages, each a request/response plus a pushed form that must become the right `_t` callback; the game also needs `LobbyEnter_t`, `LobbyDataUpdate_t`, `LobbyChatUpdate_t`, `LobbyKicked_t` | the provider owns the callback queue. Either the launcher feeds gbe's queue (needs a hook gbe does not document) or the launcher answers the interface itself, which means replacing `ISteamMatchmaking` wholesale — a much bigger change than observing one export |
| Auth tickets | medium | gbe's ticket is signed by the emulator; CM will not accept it as a client ticket, so only the "register what gbe gave the game" direction is realistic. Validating other players is server-side |
| `ISteamNetworkingSockets` over SDR | large | a full transport implementation: certificates, relay tickets, POP selection, encryption, and the console's AppContainer UDP rules. Not a launcher-session feature |
| `ISteamMatchmakingServers` | small | independent of Steam: it is UDP `A2S` queries. Worth checking whether the AppContainer lets the launcher open outbound UDP at all before designing anything |

Two structural risks apply to everything in option (b):

- **The callback queue.** Steamworks delivers asynchronous results as
  `*_t` structs through `SteamAPI_RunCallbacks`. gbe owns that queue. A bridge
  that only talks CM cannot deliver a callback unless it either goes through
  gbe's queue or replaces the interface object. The rich-presence slice avoids
  this because `SetRichPresence` is fire-and-forget; lobbies are not.
- **Two sessions, one account.** The launcher already holds the account's CM
  session. Lobby messages must go on that session, with `routing_appid` set,
  or Steam treats them as belonging to no app.

## Proto sources and licences

| Source | What it provided | Licence |
|---|---|---|
| SteamDatabase/Protobufs (as vendored in steam-user 5.3.0 `protobufs/*.proto`) | `CMsgClientMMS*`, `CMsgClientLBS*`, `CMsgClientPersonaState`, `CMsgClientRichPresence*`, `CMsgClientPlayingSessionState`, `CMsgClientGamesPlayed`, `CMsgProtoBufHeader`, `CMsgSteamDatagram*` field numbers | The `.proto` files are Valve's published protocol definitions, distributed by SteamDatabase as public domain (Unlicense). steam-user is MIT (Copyright (c) 2015 Alex Corn); its own code was not copied |
| SteamKit2 | cross-check of message names and `EMsg` values | LGPL-2.1 — no source copied; names and numbers are facts, not expression |

Nothing from either project is vendored into this repository. The message
numbers and field tags used by `app/steam_presence.h` are protocol facts; the
encoding helpers (`pb_varint`, `pb_u`, `pb_s`, `pb_fixed64`) are this project's
own (`app/steam_wire.h`). If a future slice copies actual proto text or
generated code, it must be pinned in `deps.pin`, fetched by a `src/tools`
script, and listed in `NOTICE`.

## What is verified, and what is not

Verified off-device:

- The presence wire shapes round-trip against hand-built CM bodies, and the
  coalescing policy behaves as described
  (`app/tests/steam_presence_test.cpp`, run by CI).
- The leaderboard wire shapes round-trip against a fake CM service that answers
  in the real `CMsgClientLBS*` shapes, the provider's cache file is byte-exact
  against gbe_fork's reader, the seed policy asks Steam once per board and never
  for a board it already knows, and a score is only sent for a seeded board
  (`app/tests/steam_leaderboards_test.cpp`, run by CI).
- Off-device under Wine, a guest that statically imports `steam_api64.dll` as a
  Steamworks game does finds a board, and Play's seed runs before the provider's
  Find: the provider reads three players out of the file Play wrote, the guest
  downloads them in rank order and uploads a score that Play forwards. A board
  the seed did not cover stays empty, which is the pre-bridge behaviour
  (`app/tests/play-runtime.sh leaderboards`, fixtures
  `app/tests/play-leaderboard.cpp` and `play-leaderboard-provider.cpp`).
- The **pinned gbe_fork binary itself** reads the file the launcher writes. The
  test above uses a stand-in provider written here, so it proves the bridge and
  the file format but not the real reader; `app/tests/steam-leaderboard-gbe.sh`
  runs the fetched, hash-checked `steam_api64.dll` as a game would, seeds with
  the launcher's own encoder, and gets 3 rows back at ranks 1-3 with the seeded
  scores. With the file removed the same guest gets an empty board. That is the
  link a new pin or a changed path would break silently.
- The refresh policy: a Find inside the TTL asks Steam nothing, a Find past it
  refetches, an upload dirties the board so the next Find refetches at once, a
  failed refresh keeps the previous rows, and a successful empty fetch replaces
  them (a reset weekly board really is empty)
  (`app/tests/steam_leaderboards_test.cpp`).
- Persona names: the request and push wire shapes round-trip, a name is only
  handed out once Steam answered, a rename replaces the name in place, a push
  about somebody else (the account, a friend, another board) neither ends the
  wait nor is counted as its answer, a push that mentions a requested id without
  naming it does end it, the seed never holds the game's thread past
  `SeedWaitMs`, the private cache round-trips and expires, and no SteamID or
  name reaches the log (`app/tests/steam_persona_test.cpp`, run by CI).
  Off-device under Wine, the provider stand-in answers "Unknown User" and the
  launcher's tap replaces it with the name the fixture backend sent,
  `RequestUserInformation` never reports a name as in flight (Play posts no
  `PersonaStateChange_t`), and the rows after the guest's own upload are the
  refreshed ones (`app/tests/play-runtime.sh leaderboards`).
- The launcher sources compile with the UWP toolchain.
- gbe_fork exports 1,032 `SteamAPI_ISteam*` names. Of the 894 Megabonk
  references, 890 are exported; the 4 that are not are the
  `ISteamNetworkingConnectionSignaling` and `ISteamNetworkingSignalingRecvContext`
  callbacks (`Release`, `SendSignal`, `OnConnectRequest`,
  `SendRejectionSignal`), which only matter to a game that supplies its own
  custom signaling — Megabonk also references `ConnectP2PCustomSignaling`, so
  this is a real gap if that path is ever taken. Mistria's 25 references are
  all exported.
- gbe_fork has no SDR datagram protos compiled in, so its
  `ISteamNetworkingSockets` cannot reach Steam's relay network.

Not verified:

- That Steam accepts the rich-presence upload with `routing_appid`, or shows
  the game to friends. That needs a console run and a friend's client; the
  console log line `STEAM presence: Steam reports persona_state=… game=…` is
  the check.
- Anything about lobbies, P2P or auth tickets end to end. No CM lobby message
  has been sent from this codebase.
- That the AMIGOS tab lists only the account's real friends, or that ranks match
  Steam's `global_rank` for a row outside the seeded slice (gaps 1 and 2 above).
- That the refresh policy is what the console shows: a Find past the 30 s TTL,
  or one after this account's own upload, giving the owner their new score and
  rank without a relaunch. The checks are
  `STEAM leaderboards: '…' refreshed N rows (id …, N on Steam)` and, on the
  screen, the owner's row moved. Off-device the policy is proven
  (`app/tests/steam_leaderboards_test.cpp`, `app/tests/play-runtime.sh
  leaderboards`); on the console it has not been observed yet.
- That the persona route in `steam_persona.h` — the request batch, the wait for
  the push, the private cache and the `RequestUserInformation` contract — draws
  names on the console. The checks are `STEAM persona: N of M leaderboard names
  resolved` and, on the screen, the Rank screen naming players. The run below
  proved the simpler tap this branch replaces; this one is off-device only so
  far.

**Console run, 2026-11-09 (0.5.255.0, `feat/steam-leaderboards` @ 7dd2f0f).**
The tap fired, and Steam answered every board the game asked for, in one launch:

```
STEAM leaderboards: SteamAPI_ISteamUserStats_FindLeaderboard observed; rows come from the launcher's Steam session
STEAM leaderboards: 'banned_players' seeded 72 rows (id 17849113, 72 on Steam, file written)
STEAM leaderboards: 'v2_kills' seeded 0 rows (id 17843353, 0 on Steam, file empty)
STEAM leaderboards: 'v2_kills_friends' seeded 100 rows (id 17772166, 3735220 on Steam, file written)
STEAM leaderboards: 'v2_kills_weekly' seeded 100 rows (id 17850492, 29116 on Steam, file written)
STEAM leaderboards: 'v2_kills_weekly_friends' seeded 100 rows (id 17850493, 29171 on Steam, file written)
```

So the board names are not a hypothesis any more, `CMsgClientLBSFindOrCreateLB`
answers on the launcher's session, and the cache files on the console decode to
the row counts in the log. `v2_kills` is genuinely empty on Steam — that is
Steam's answer, not a bridge failure — and the game's real boards are the
`_friends` / `_weekly` names plus `banned_players`. Note that `_friends` is a
board *name*, not the friends data request: it is a separate global board.

**The Rank screen lists the rows.** A later run on the same build captured the
LEADERBOARDS panel with GLOBAL selected, on Megabonk's main menu:

```
LEADERBOARDS                    Reset 140:12:26
GLOBAL | FRIENDS
RANK  PLAYER          KILLS
#1    Unknown User    3.01M
#2    Unknown User    2.94M
#3    Unknown User    1.41M
...
#10   Unknown User    934.01K
```

Real other players, real scores, ranked 1..10 — the owner's acceptance check on
this build. The JOGADOR column read "Unknown User" on every row, which is gap 2
above; the name fix was verified on the next package (below).

**The Rank screen shows real names (0.5.256.0, `feat/steam-leaderboards` @
`a552964`).** Same panel, same framing, one build later — only the PLAYER column
changed:

```
LEADERBOARDS                    Reset 139:43:06
GLOBAL | FRIENDS
RANK  PLAYER            KILLS
#1    Банановый сок    3.01M
#2    Gaskarth         2.94M
#3    Time             1.41M
#4    Ryan Gosling     1.36M
#5    Iar              1.20M
#6    MrWifiss         1.08M
#7    casual gamer     1.08M
#8    Rollngar         1.03M
#9    blaizerd         993.27K
#10   pepequentin      934.01K
```

Scores and ranks are identical to the run above, so the name path is isolated:
the tap fired once
(`STEAM persona names: SteamAPI_ISteamFriends_GetFriendPersonaName observed;
names come from the launcher's Steam session`) and the persona push named 294 of
the seeded accounts, one per `CMsgClientPersonaState` entry, interleaved with
the seeding (`STEAM leaderboards: 1 player names received from Steam (N known)`,
N reaching 294). No input was needed: the photosensitivity warning had already
been accepted in two earlier runs with a pad connected, and the game persists
that.

