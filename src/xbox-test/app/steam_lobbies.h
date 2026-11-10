// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Real Steam lobbies for an owned game: the CM side of ISteamMatchmaking.
//
// The Steam API provider (gbe_fork) answers lobbies from its own LAN peer
// network, so with no peers a game sees no lobbies and its friends never see
// the game's. The launcher already holds the account's signed-in CM session
// (steamdl.h), and Valve's lobby service is a plain request/response protocol
// on that session: CMsgClientMMS* (EMsg 6601-6627). This header is the wire
// layer for the calls a game's lobby flow makes, plus the policy that keeps the
// launcher's own view of the lobby consistent across them.
//
// What this is not: a replacement for ISteamMatchmaking. The provider owns the
// callback queue the game reads (LobbyCreated_t, LobbyEnter_t,
// LobbyDataUpdate_t, LobbyChatUpdate_t), so a CM answer only reaches the game
// when the launcher owns the interface object or feeds the provider's queue.
// That gate is described in research/STEAM-REAL-ONLINE.md; this slice is the
// part that can be verified off-device, against a fake CM service.
//
// Portable: the CM transport and logging are injected. Message shapes follow
// SteamDatabase/Protobufs (public domain) as vendored in steam-user; see
// research/STEAMWORKS-MULTIPLAYER.md.
#include "steam_wire.h"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace steam_lobbies {

// EMsg values from enums_clientserver.proto (k_EMsgClientMMS*).
enum : uint32_t {
    CreateLobby = 6601, CreateLobbyResponse = 6602,
    JoinLobby = 6603, JoinLobbyResponse = 6604,
    LeaveLobby = 6605, LeaveLobbyResponse = 6606,
    GetLobbyList = 6607, GetLobbyListResponse = 6608,
    SetLobbyData = 6609, SetLobbyDataResponse = 6610,
    GetLobbyData = 6611, LobbyData = 6612,
    SendLobbyChatMsg = 6613, LobbyChatMsg = 6614,
    SetLobbyOwner = 6615, SetLobbyOwnerResponse = 6616,
    SetLobbyGameServer = 6617, LobbyGameServerSet = 6618,
    UserJoinedLobby = 6619, UserLeftLobby = 6620,
    InviteToLobby = 6621, SetLobbyLinked = 6624,
    GetLobbyStatus = 6626, GetLobbyStatusResponse = 6627,
};

// ELobbyType / ELobbyFilterType / ELobbyComparison, as the game passes them.
enum : int { LobbyTypePrivate = 0, LobbyTypeFriendsOnly = 1, LobbyTypePublic = 2, LobbyTypeInvisible = 3 };
enum : int { FilterString = 0, FilterNumerical = 1, FilterSlotsAvailable = 2, FilterNearValue = 3 };
enum : int { CompareEqualOrLess = 0, CompareLess = 1, CompareEqual = 2, CompareGreater = 3, CompareEqualOrGreater = 4, CompareNotEqual = 5 };
// EMMSLobbyStatus.
enum : int { LobbyStatusInvalid = 0, LobbyStatusNotMember = 1, LobbyStatusMember = 2, LobbyStatusOwner = 3 };

// k_cchSteamIDLobbyMax is not a thing; these are the API's own limits.
enum : unsigned { MaxLobbyName = 128, MaxMetadataKey = 255, MaxMetadataValue = 8192, MaxChatMsg = 4096 };
// EResult values this layer cares about.
enum : int32_t { ResultOK = 1, ResultFail = 2, ResultNoMatch = 9, ResultLimitExceeded = 25, ResultAccessDenied = 15 };

struct Io {
    // One CM request -> one response, with the app id in the header: Steam
    // only answers an MMS request that carries it.
    std::function<bool(uint32_t emsg, uint32_t routing_appid, const std::string& body, std::string& response)> call;
    std::function<void(const std::string&)> log;
};

// One member as the CM reports it. `metadata` is the member's KeyValues blob.
struct Member { uint64_t steamid = 0; std::string persona; std::string metadata; };
// One lobby as GetLobbyList reports it.
struct LobbySummary {
    uint64_t steamid = 0;
    int32_t max_members = 0, lobby_type = 0, lobby_flags = 0, num_members = 0;
    std::string metadata;
    float distance = 0.f;
};
// The full lobby record (join response / pushed LobbyData).
struct Lobby {
    bool found = false;
    int32_t eresult = ResultFail;
    uint64_t steamid = 0, owner = 0;
    int32_t max_members = 0, lobby_type = 0, lobby_flags = 0;
    std::string metadata;
    std::vector<Member> members;
};
// One lobby chat message as the CM pushes it.
struct ChatMsg { uint64_t lobby = 0, sender = 0; std::string message; };

// ---- wire
// CMsgClientMMSCreateLobby: app_id (1), max_members (2), lobby_type (3),
// lobby_flags (4), cell_id (5), metadata (7), persona_name_owner (8).
inline std::string create_body(uint32_t appid, int max_members, int lobby_type, int lobby_flags,
                               const std::string& metadata, const std::string& persona) {
    std::string b;
    pb_u(b, 1, appid); pb_u(b, 2, uint64_t(int64_t(max_members))); pb_u(b, 3, uint64_t(int64_t(lobby_type)));
    pb_u(b, 4, uint64_t(int64_t(lobby_flags)));
    if (!metadata.empty()) pb_s(b, 7, metadata);
    if (!persona.empty()) pb_s(b, 8, persona);
    return b;
}
// CMsgClientMMSCreateLobbyResponse: app_id (1), steam_id_lobby (2), eresult (3).
inline Lobby parse_create(const std::string& body, int32_t headerEresult) {
    Lobby out; int32_t eresult = headerEresult;
    for (auto& f : pb_parse(body)) {
        if (f.f == 2) out.steamid = f.u;
        else if (f.f == 3) eresult = int32_t(f.u);
    }
    out.eresult = eresult;
    out.found = eresult == ResultOK && out.steamid != 0;
    return out;
}
// CMsgClientMMSJoinLobby: app_id (1), steam_id_lobby (2), persona_name (3).
inline std::string join_body(uint32_t appid, uint64_t lobby, const std::string& persona) {
    std::string b;
    pb_u(b, 1, appid); pb_fixed64(b, 2, lobby);
    if (!persona.empty()) pb_s(b, 3, persona);
    return b;
}
// CMsgClientMMSJoinLobbyResponse: app_id (1), steam_id_lobby (2),
// chat_room_enter_response (3), max_members (4), lobby_type (5), lobby_flags
// (6), steam_id_owner (7), metadata (8), members (9) -> steam_id (1),
// persona_name (2), metadata (3).
inline Lobby parse_join(const std::string& body, int32_t headerEresult) {
    Lobby out; int32_t eresult = headerEresult;
    for (auto& f : pb_parse(body)) {
        if (f.f == 2) out.steamid = f.u;
        else if (f.f == 3) eresult = int32_t(f.u);
        else if (f.f == 4) out.max_members = int32_t(f.u);
        else if (f.f == 5) out.lobby_type = int32_t(f.u);
        else if (f.f == 6) out.lobby_flags = int32_t(f.u);
        else if (f.f == 7) out.owner = f.u;
        else if (f.f == 8) out.metadata = f.s;
        else if (f.f == 9) {
            Member m;
            for (auto& x : pb_parse(f.s)) {
                if (x.f == 1) m.steamid = x.u;
                else if (x.f == 2) m.persona = x.s;
                else if (x.f == 3) m.metadata = x.s;
            }
            out.members.push_back(std::move(m));
        }
    }
    out.eresult = eresult;
    out.found = eresult == ResultOK && out.steamid != 0;
    return out;
}
// CMsgClientMMSLeaveLobby: app_id (1), steam_id_lobby (2).
inline std::string leave_body(uint32_t appid, uint64_t lobby) {
    std::string b; pb_u(b, 1, appid); pb_fixed64(b, 2, lobby); return b;
}
// CMsgClientMMSLeaveLobbyResponse: app_id (1), steam_id_lobby (2), eresult (3).
inline int32_t parse_leave(const std::string& body, int32_t headerEresult) {
    int32_t eresult = headerEresult;
    for (auto& f : pb_parse(body)) if (f.f == 3) eresult = int32_t(f.u);
    return eresult;
}
// CMsgClientMMSGetLobbyList: app_id (1), num_lobbies_requested (3), cell_id
// (4), filters (6) -> key (1), value (2), comparision (3), filter_type (4).
struct Filter { std::string key, value; int comparison = CompareEqual, type = FilterString; };
inline std::string list_body(uint32_t appid, int want, const std::vector<Filter>& filters) {
    std::string b;
    pb_u(b, 1, appid); pb_u(b, 3, uint64_t(int64_t(want)));
    for (auto& flt : filters) {
        std::string f;
        pb_s(f, 1, flt.key); pb_s(f, 2, flt.value);
        pb_u(f, 3, uint64_t(int64_t(flt.comparison))); pb_u(f, 4, uint64_t(int64_t(flt.type)));
        pb_s(b, 6, f);
    }
    return b;
}
// CMsgClientMMSGetLobbyListResponse: app_id (1), eresult (3), lobbies (4) ->
// steam_id (1), max_members (2), lobby_type (3), lobby_flags (4), metadata (5),
// num_members (6), distance (7).
inline bool parse_list(const std::string& body, int32_t headerEresult, std::vector<LobbySummary>& out) {
    out.clear(); int32_t eresult = headerEresult;
    for (auto& f : pb_parse(body)) {
        if (f.f == 3) eresult = int32_t(f.u);
        else if (f.f == 4) {
            LobbySummary s;
            for (auto& x : pb_parse(f.s)) {
                if (x.f == 1) s.steamid = x.u;
                else if (x.f == 2) s.max_members = int32_t(x.u);
                else if (x.f == 3) s.lobby_type = int32_t(x.u);
                else if (x.f == 4) s.lobby_flags = int32_t(x.u);
                else if (x.f == 5) s.metadata = x.s;
                else if (x.f == 6) s.num_members = int32_t(x.u);
                else if (x.f == 7 && x.wt == 5) { float d; memcpy(&d, &x.u, 4); s.distance = d; }
            }
            out.push_back(std::move(s));
        }
    }
    return eresult == ResultOK;
}
// CMsgClientMMSSetLobbyData: app_id (1), steam_id_lobby (2), steam_id_member
// (3, non-zero for member data), max_members (4), lobby_type (5), lobby_flags
// (6), metadata (7).
inline std::string set_data_body(uint32_t appid, uint64_t lobby, uint64_t member,
                                 const std::string& metadata, int max_members, int lobby_type, int lobby_flags) {
    std::string b;
    pb_u(b, 1, appid); pb_fixed64(b, 2, lobby);
    if (member) pb_fixed64(b, 3, member);
    if (max_members) pb_u(b, 4, uint64_t(int64_t(max_members)));
    if (lobby_type) pb_u(b, 5, uint64_t(int64_t(lobby_type)));
    if (lobby_flags) pb_u(b, 6, uint64_t(int64_t(lobby_flags)));
    pb_s(b, 7, metadata);
    return b;
}
// CMsgClientMMSSetLobbyDataResponse: app_id (1), steam_id_lobby (2), eresult (3).
inline int32_t parse_set_data(const std::string& body, int32_t headerEresult) {
    int32_t eresult = headerEresult;
    for (auto& f : pb_parse(body)) if (f.f == 3) eresult = int32_t(f.u);
    return eresult;
}
// CMsgClientMMSGetLobbyData: app_id (1), steam_id_lobby (2).
inline std::string get_data_body(uint32_t appid, uint64_t lobby) {
    std::string b; pb_u(b, 1, appid); pb_fixed64(b, 2, lobby); return b;
}
// CMsgClientMMSLobbyData: app_id (1), steam_id_lobby (2), num_members (3),
// max_members (4), lobby_type (5), lobby_flags (6), steam_id_owner (7),
// metadata (8), members (9), lobby_cellid (10), owner_should_accept_changes
// (11). This is both the GetLobbyData answer and the pushed update.
inline Lobby parse_lobby_data(const std::string& body, int32_t headerEresult) {
    Lobby out; int32_t eresult = headerEresult;
    for (auto& f : pb_parse(body)) {
        if (f.f == 2) out.steamid = f.u;
        else if (f.f == 4) out.max_members = int32_t(f.u);
        else if (f.f == 5) out.lobby_type = int32_t(f.u);
        else if (f.f == 6) out.lobby_flags = int32_t(f.u);
        else if (f.f == 7) out.owner = f.u;
        else if (f.f == 8) out.metadata = f.s;
        else if (f.f == 9) {
            Member m;
            for (auto& x : pb_parse(f.s)) {
                if (x.f == 1) m.steamid = x.u;
                else if (x.f == 2) m.persona = x.s;
                else if (x.f == 3) m.metadata = x.s;
            }
            out.members.push_back(std::move(m));
        }
    }
    out.eresult = eresult;
    out.found = eresult == ResultOK && out.steamid != 0;
    return out;
}
// CMsgClientMMSSendLobbyChatMsg: app_id (1), steam_id_lobby (2),
// steam_id_target (3), lobby_message (4).
inline std::string chat_body(uint32_t appid, uint64_t lobby, uint64_t target, const std::string& message) {
    std::string b;
    pb_u(b, 1, appid); pb_fixed64(b, 2, lobby);
    if (target) pb_fixed64(b, 3, target);
    pb_s(b, 4, message);
    return b;
}
// CMsgClientMMSLobbyChatMsg (pushed): app_id (1), steam_id_lobby (2),
// steam_id_sender (3), lobby_message (4).
inline ChatMsg parse_chat(const std::string& body) {
    ChatMsg out;
    for (auto& f : pb_parse(body)) {
        if (f.f == 2) out.lobby = f.u;
        else if (f.f == 3) out.sender = f.u;
        else if (f.f == 4) out.message = f.s;
    }
    return out;
}
// CMsgClientMMSSetLobbyOwner: app_id (1), steam_id_lobby (2),
// steam_id_new_owner (3).
inline std::string set_owner_body(uint32_t appid, uint64_t lobby, uint64_t new_owner) {
    std::string b; pb_u(b, 1, appid); pb_fixed64(b, 2, lobby); pb_fixed64(b, 3, new_owner); return b;
}
// CMsgClientMMSSetLobbyOwnerResponse: app_id (1), steam_id_lobby (2), eresult (3).
inline int32_t parse_set_owner(const std::string& body, int32_t headerEresult) {
    int32_t eresult = headerEresult;
    for (auto& f : pb_parse(body)) if (f.f == 3) eresult = int32_t(f.u);
    return eresult;
}
// CMsgClientMMSSetLobbyGameServer: app_id (1), steam_id_lobby (2),
// deprecated_game_server_ip (3), game_server_port (4), game_server_steam_id
// (5), game_server_ip (6). `ipv4` is host order; 0 means "no server".
inline std::string set_server_body(uint32_t appid, uint64_t lobby, uint32_t ipv4, uint32_t port, uint64_t server_steamid) {
    std::string b;
    pb_u(b, 1, appid); pb_fixed64(b, 2, lobby);
    if (ipv4) { pb_u(b, 3, ipv4); pb_u(b, 4, port); }
    if (server_steamid) pb_fixed64(b, 5, server_steamid);
    return b;
}
// CMsgClientMMSLobbyGameServerSet (pushed): same fields as the request.
struct GameServer { uint64_t lobby = 0; uint32_t ipv4 = 0, port = 0; uint64_t steamid = 0; };
inline GameServer parse_server_set(const std::string& body) {
    GameServer out;
    for (auto& f : pb_parse(body)) {
        if (f.f == 2) out.lobby = f.u;
        else if (f.f == 3) out.ipv4 = uint32_t(f.u);
        else if (f.f == 4) out.port = uint32_t(f.u);
        else if (f.f == 5) out.steamid = f.u;
    }
    return out;
}
// CMsgClientMMSUserJoinedLobby / ...UserLeftLobby (pushed): app_id (1),
// steam_id_lobby (2), steam_id_user (3), persona_name (4).
struct Membership { uint64_t lobby = 0, user = 0; std::string persona; bool joined = false; };
inline Membership parse_membership(const std::string& body, bool joined) {
    Membership out; out.joined = joined;
    for (auto& f : pb_parse(body)) {
        if (f.f == 2) out.lobby = f.u;
        else if (f.f == 3) out.user = f.u;
        else if (f.f == 4) out.persona = f.s;
    }
    return out;
}
// CMsgClientMMSInviteToLobby: app_id (1), steam_id_lobby (2),
// steam_id_user_invited (3).
inline std::string invite_body(uint32_t appid, uint64_t lobby, uint64_t invitee) {
    std::string b; pb_u(b, 1, appid); pb_fixed64(b, 2, lobby); pb_fixed64(b, 3, invitee); return b;
}
// CMsgClientMMSGetLobbyStatus: app_id (1), steam_id_lobby (2),
// claim_membership (3), claim_ownership (4).
inline std::string status_body(uint32_t appid, uint64_t lobby, bool claim_membership, bool claim_ownership) {
    std::string b;
    pb_u(b, 1, appid); pb_fixed64(b, 2, lobby);
    pb_u(b, 3, claim_membership ? 1 : 0); pb_u(b, 4, claim_ownership ? 1 : 0);
    return b;
}
// CMsgClientMMSGetLobbyStatusResponse: app_id (1), steam_id_lobby (2),
// lobby_status (3).
inline int32_t parse_status(const std::string& body, int32_t headerEresult) {
    int32_t status = LobbyStatusInvalid;
    for (auto& f : pb_parse(body)) if (f.f == 3) status = int32_t(f.u);
    if (headerEresult != ResultOK) return LobbyStatusInvalid;
    return status;
}

// ---- the calls
inline Lobby create(Io& io, uint32_t appid, int max_members, int lobby_type, int lobby_flags,
                    const std::string& metadata, const std::string& persona) {
    std::string resp;
    if (!io.call(CreateLobby, appid, create_body(appid, max_members, lobby_type, lobby_flags, metadata, persona), resp)) {
        io.log("lobbies: create request failed (transport)");
        return Lobby{};
    }
    auto lobby = parse_create(resp, ResultFail);
    if (!lobby.found) {
        char line[160];
        snprintf(line, sizeof line, "lobbies: create refused (eresult %d)", lobby.eresult);
        io.log(line);
    }
    return lobby;
}
inline Lobby join(Io& io, uint32_t appid, uint64_t lobby, const std::string& persona) {
    std::string resp;
    if (!io.call(JoinLobby, appid, join_body(appid, lobby, persona), resp)) {
        io.log("lobbies: join request failed (transport)");
        return Lobby{};
    }
    auto joined = parse_join(resp, ResultFail);
    if (!joined.found) {
        char line[160];
        snprintf(line, sizeof line, "lobbies: join refused (eresult %d)", joined.eresult);
        io.log(line);
    }
    return joined;
}
inline bool leave(Io& io, uint32_t appid, uint64_t lobby) {
    std::string resp;
    if (!io.call(LeaveLobby, appid, leave_body(appid, lobby), resp)) {
        io.log("lobbies: leave request failed (transport)");
        return false;
    }
    int32_t eresult = parse_leave(resp, ResultFail);
    if (eresult != ResultOK) {
        char line[160];
        snprintf(line, sizeof line, "lobbies: leave refused (eresult %d)", eresult);
        io.log(line);
    }
    return eresult == ResultOK;
}
inline bool list(Io& io, uint32_t appid, int want, const std::vector<Filter>& filters, std::vector<LobbySummary>& out) {
    std::string resp;
    if (!io.call(GetLobbyList, appid, list_body(appid, want, filters), resp)) {
        io.log("lobbies: list request failed (transport)");
        out.clear();
        return false;
    }
    if (!parse_list(resp, ResultFail, out)) {
        io.log("lobbies: list unavailable");
        out.clear();
        return false;
    }
    return true;
}
inline Lobby data(Io& io, uint32_t appid, uint64_t lobby) {
    std::string resp;
    if (!io.call(GetLobbyData, appid, get_data_body(appid, lobby), resp)) {
        io.log("lobbies: data request failed (transport)");
        return Lobby{};
    }
    // CMsgClientMMSLobbyData carries no eresult of its own: a job that came
    // back at all succeeded, and the lobby is identified by its id.
    return parse_lobby_data(resp, ResultOK);
}
inline bool set_data(Io& io, uint32_t appid, uint64_t lobby, uint64_t member, const std::string& metadata,
                     int max_members = 0, int lobby_type = 0, int lobby_flags = 0) {
    std::string resp;
    if (!io.call(SetLobbyData, appid, set_data_body(appid, lobby, member, metadata, max_members, lobby_type, lobby_flags), resp)) {
        io.log("lobbies: set-data request failed (transport)");
        return false;
    }
    return parse_set_data(resp, ResultFail) == ResultOK;
}
inline bool send_chat(Io& io, uint32_t appid, uint64_t lobby, uint64_t target, const std::string& message) {
    if (message.size() > MaxChatMsg) { io.log("lobbies: chat message too large"); return false; }
    std::string resp;
    if (!io.call(SendLobbyChatMsg, appid, chat_body(appid, lobby, target, message), resp)) {
        io.log("lobbies: chat request failed (transport)");
        return false;
    }
    // SendLobbyChatMsg has no response of its own; the CM reports failures by
    // not delivering the pushed form. A transport success is all there is.
    return true;
}
inline bool set_owner(Io& io, uint32_t appid, uint64_t lobby, uint64_t new_owner) {
    std::string resp;
    if (!io.call(SetLobbyOwner, appid, set_owner_body(appid, lobby, new_owner), resp)) {
        io.log("lobbies: set-owner request failed (transport)");
        return false;
    }
    return parse_set_owner(resp, ResultFail) == ResultOK;
}
inline bool set_server(Io& io, uint32_t appid, uint64_t lobby, uint32_t ipv4, uint32_t port, uint64_t server_steamid) {
    std::string resp;
    if (!io.call(SetLobbyGameServer, appid, set_server_body(appid, lobby, ipv4, port, server_steamid), resp)) {
        io.log("lobbies: set-server request failed (transport)");
        return false;
    }
    // Like chat, the effect arrives as the pushed LobbyGameServerSet.
    return true;
}
inline bool invite(Io& io, uint32_t appid, uint64_t lobby, uint64_t invitee) {
    std::string resp;
    if (!io.call(InviteToLobby, appid, invite_body(appid, lobby, invitee), resp)) {
        io.log("lobbies: invite request failed (transport)");
        return false;
    }
    return true;
}
inline int32_t status(Io& io, uint32_t appid, uint64_t lobby, bool claim_membership, bool claim_ownership) {
    std::string resp;
    if (!io.call(GetLobbyStatus, appid, status_body(appid, lobby, claim_membership, claim_ownership), resp)) {
        io.log("lobbies: status request failed (transport)");
        return LobbyStatusInvalid;
    }
    // The status is the response's only verdict; a job that came back succeeded.
    return parse_status(resp, ResultOK);
}

// ---- the bridge policy
// What the launcher knows about the lobby the game is in, kept across calls so
// a pushed message can be matched to the lobby the game asked about. One of
// these per launch. `lobby` is 0 when the game is in no lobby.
struct Bridge {
    std::mutex m;
    uint64_t lobby = 0;
    uint64_t owner = 0;
    int32_t max_members = 0, lobby_type = 0, lobby_flags = 0;
    std::string metadata;
    std::map<uint64_t, std::string> members;   // steamid -> persona
    bool owner_self = false;

    // Creates a lobby and remembers it. A create that Steam refuses leaves the
    // bridge in no lobby, which is what the game's LobbyCreated_t must say.
    // CreateLobbyResponse carries only the id, so the member list and the
    // metadata the CM settled on come from one GetLobbyData round trip.
    bool create(Io& io, uint32_t appid, int max, int type, int flags, const std::string& meta, const std::string& persona) {
        std::lock_guard<std::mutex> lock(m);
        auto made = steam_lobbies::create(io, appid, max, type, flags, meta, persona);
        if (!made.found) { clear(); return false; }
        lobby = made.steamid; owner = 0; owner_self = true;
        max_members = max; lobby_type = type; lobby_flags = flags; metadata = meta;
        members.clear();
        auto settled = steam_lobbies::data(io, appid, lobby);
        if (settled.found) {
            owner = settled.owner;
            if (settled.max_members) max_members = settled.max_members;
            if (settled.lobby_type) lobby_type = settled.lobby_type;
            lobby_flags = settled.lobby_flags;
            if (!settled.metadata.empty()) metadata = settled.metadata;
            for (auto& mem : settled.members) members[mem.steamid] = mem.persona;
        }
        char line[200];
        snprintf(line, sizeof line, "lobbies: created %s (%zu/%d, type %d)", lobby_name().c_str(), members.size(), max, type);
        if (io.log) io.log(line);
        return true;
    }
    // Joins a lobby and adopts the member list the CM returned.
    bool join(Io& io, uint32_t appid, uint64_t id, const std::string& persona) {
        std::lock_guard<std::mutex> lock(m);
        auto joined = steam_lobbies::join(io, appid, id, persona);
        if (!joined.found) { clear(); return false; }
        lobby = joined.steamid; owner = joined.owner;
        max_members = joined.max_members; lobby_type = joined.lobby_type; lobby_flags = joined.lobby_flags;
        metadata = joined.metadata;
        members.clear();
        for (auto& mem : joined.members) members[mem.steamid] = mem.persona;
        owner_self = false;
        char line[200];
        snprintf(line, sizeof line, "lobbies: joined %s (%zu members)", lobby_name().c_str(), members.size());
        if (io.log) io.log(line);
        return true;
    }
    // Leaves the lobby the bridge is in. A leave the CM refuses still clears
    // the local view: the game asked to leave, and holding a stale id would
    // make every later message address a lobby the account is not in.
    bool leave(Io& io, uint32_t appid) {
        std::lock_guard<std::mutex> lock(m);
        if (!lobby) return false;
        bool ok = steam_lobbies::leave(io, appid, lobby);
        clear();
        return ok;
    }
    // A pushed LobbyData (6612) for the lobby the bridge is in.
    bool apply_lobby_data(const Lobby& data) {
        std::lock_guard<std::mutex> lock(m);
        if (!lobby || data.steamid != lobby) return false;
        owner = data.owner;
        max_members = data.max_members; lobby_type = data.lobby_type; lobby_flags = data.lobby_flags;
        metadata = data.metadata;
        members.clear();
        for (auto& mem : data.members) members[mem.steamid] = mem.persona;
        return true;
    }
    // A pushed UserJoinedLobby (6619) / UserLeftLobby (6620).
    bool apply_membership(const Membership& change) {
        std::lock_guard<std::mutex> lock(m);
        if (!lobby || change.lobby != lobby) return false;
        if (change.joined) members[change.user] = change.persona;
        else {
            members.erase(change.user);
            // The owner left: Steam hands ownership to someone still in the
            // lobby, and the pushed LobbyData that follows is authoritative.
            if (change.user == owner) owner = 0;
        }
        return true;
    }
    // A pushed LobbyChatMsg (6614) for the lobby the bridge is in.
    bool accepts_chat(const ChatMsg& msg) const { return lobby != 0 && msg.lobby == lobby; }
    // A pushed LobbyGameServerSet (6618) for the lobby the bridge is in.
    bool accepts_server(const GameServer& server) const { return lobby != 0 && server.lobby == lobby; }
    bool in_lobby() const { return lobby != 0; }
    // The id as the log should show it: the low 32 bits, never the whole
    // account-adjacent value in a file that may be shared.
    std::string lobby_name() const {
        char b[24];
        snprintf(b, sizeof b, "lobby#%u", unsigned(lobby & 0xffffffffu));
        return b;
    }
    void clear() {
        lobby = 0; owner = 0; max_members = 0; lobby_type = 0; lobby_flags = 0;
        metadata.clear(); members.clear(); owner_self = false;
    }
};

} // namespace steam_lobbies
