// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Steam presence for owned games run through the Steam API provider
// (AGENTS.md rule 3): the account shows "In-Game: <game>" to friends on the
// real Steam network, and the game's ISteamFriends::SetRichPresence calls reach
// them as rich presence. Portable: the CM transport is injected, so the wire
// shapes and the coalescing policy are tested natively.
//
// Message shapes follow SteamDatabase/Protobufs (public domain) as vendored in
// SteamKit2 and steam-user; see research/STEAMWORKS-MULTIPLAYER.md.
//
// Presence is two independent things on Steam:
//  - "In-Game" comes from CMsgClientGamesPlayed (EMsg 5410) on the launcher's
//    own CM session, and only shows while that session's persona is Online.
//  - Rich presence is a per-app key/value set uploaded with
//    CMsgClientRichPresenceUpload (EMsg 7501), whose header carries the app id
//    (routing_appid). The blob is Steam's binary KeyValues: type 0 + "RP",
//    then type 1 string pairs, then two end markers.
// Both are dropped by Steam when the CM session ends, so a reconnect during
// play must re-send them; the keeper in launch.h does that.
#include "steam_wire.h"
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace steam_presence {
enum : uint32_t {
    ClientChangeStatus = 716,          // CMsgClientChangeStatus
    ClientPersonaState = 766,          // CMsgClientPersonaState
    ClientRequestFriendData = 815,     // CMsgClientRequestFriendData
    ClientGamesPlayedWithDataBlob = 5410,
    ClientRichPresenceUpload = 7501,   // CMsgClientRichPresenceUpload
    ClientRichPresenceRequest = 7502,  // CMsgClientRichPresenceRequest
    ClientRichPresenceInfo = 7503,     // CMsgClientRichPresenceInfo
    ClientPlayingSessionState = 9600,  // CMsgClientPlayingSessionState
};
// k_EPersonaStateOnline: friends only see a game on an online persona.
enum : uint32_t { PersonaOnline = 1 };
// The persona fields the self-check asks Steam for (EClientPersonaStateFlag):
// status, name, query port, source id, presence, last seen, clan rank, game
// extra info, game data blob, clan data, rich presence.
enum : uint32_t { PersonaFlagsWanted = 6111 };
// client_os_type as the Windows client reports it (EOSType Win11).
enum : uint32_t { OsTypeWindows11 = 20 };

// ---- binary KeyValues, as Steam's client writes the rich-presence blob
inline std::string encode_kv(const std::map<std::string, std::string>& kv) {
    std::string out;
    out += '\0'; out += "RP"; out += '\0';
    for (auto& [key, value] : kv) {
        if (key.empty() || key.find('\0') != std::string::npos || value.find('\0') != std::string::npos) continue;
        out += '\1'; out += key; out += '\0'; out += value; out += '\0';
    }
    out += '\x08'; out += '\x08';
    return out;
}
inline bool decode_kv(const std::string& blob, std::map<std::string, std::string>& out) {
    out.clear();
    size_t i = 0;
    auto cstr = [&](std::string& s) {
        auto end = blob.find('\0', i);
        if (end == std::string::npos) return false;
        s = blob.substr(i, end - i); i = end + 1; return true;
    };
    if (blob.size() < 4 || blob[0] != '\0') return false;
    ++i; std::string name;
    if (!cstr(name) || name != "RP") return false;
    for (;;) {
        if (i >= blob.size()) return false;
        uint8_t type = uint8_t(blob[i++]);
        if (type == 8) return true;   // end of the RP block
        if (type != 1) return false;  // only string values are defined here
        std::string key, value;
        if (!cstr(key) || !cstr(value)) return false;
        if (!key.empty()) out[key] = value;
    }
}

// ---- requests
// An empty app id ends the session's "in game" state, as the real client does
// when the game exits.
inline std::string games_played_body(uint32_t appid, uint32_t os_type = OsTypeWindows11) {
    std::string body;
    if (appid) { std::string game; pb_fixed64(game, 2, appid); pb_s(body, 1, game); }
    pb_u(body, 2, os_type);   // client_os_type, as in ClientLogon
    return body;
}
inline std::string change_status_body(uint32_t state = PersonaOnline) {
    std::string body; pb_u(body, 1, state); return body;
}
inline std::string rich_presence_upload_body(const std::map<std::string, std::string>& kv) {
    std::string body; pb_s(body, 1, encode_kv(kv)); return body;
}
inline std::string rich_presence_request_body(uint64_t steamid) {
    std::string body; pb_fixed64(body, 1, steamid); return body;
}
inline std::string persona_request_body(uint64_t steamid, uint32_t flags = PersonaFlagsWanted) {
    std::string body; pb_u(body, 1, flags); pb_fixed64(body, 2, steamid); return body;
}

// ---- replies
struct Persona {
    bool found = false;
    uint32_t state = 0;
    uint32_t game_played_app_id = 0;
    uint64_t gameid = 0;
    std::string game_name;
    std::map<std::string, std::string> rich_presence;
};
// CMsgClientPersonaState: friends[] (2) -> friendid (1), persona_state (2),
// game_played_app_id (3), game_name (55), gameid (56), rich_presence (71).
inline Persona parse_persona(const std::string& body, uint64_t steamid) {
    Persona out;
    for (auto& friendField : pb_parse(body)) {
        if (friendField.f != 2) continue;
        Persona p; uint64_t id = 0;
        for (auto& f : pb_parse(friendField.s)) {
            if (f.f == 1) id = f.u;
            else if (f.f == 2) p.state = uint32_t(f.u);
            else if (f.f == 3) p.game_played_app_id = uint32_t(f.u);
            else if (f.f == 55) p.game_name = f.s;
            else if (f.f == 56) p.gameid = f.u;
            else if (f.f == 71) {
                std::string key, value;
                for (auto& kv : pb_parse(f.s)) { if (kv.f == 1) key = kv.s; else if (kv.f == 2) value = kv.s; }
                if (!key.empty()) p.rich_presence[key] = value;
            }
        }
        if (id == steamid) { p.found = true; return p; }
    }
    return out;
}
// CMsgClientRichPresenceInfo: rich_presence[] (1) -> steamid_user (1),
// rich_presence_kv (2). The key/value pairs are the same "RP" blob.
inline bool parse_rich_presence_info(const std::string& body, uint64_t steamid,
                                     std::map<std::string, std::string>& out) {
    out.clear();
    for (auto& entry : pb_parse(body)) {
        if (entry.f != 1) continue;
        uint64_t id = 0; std::string blob;
        for (auto& f : pb_parse(entry.s)) { if (f.f == 1) id = f.u; else if (f.f == 2) blob = f.s; }
        if (id == steamid && !blob.empty()) return decode_kv(blob, out);
    }
    return false;
}
// CMsgClientPlayingSessionState: playing_blocked (2), playing_app (3). Steam
// sends it when another of the account's sessions starts or stops a game; while
// blocked, a ClientGamesPlayed from here logs this session off.
struct PlayingState { bool blocked = false; uint32_t app = 0; };
inline PlayingState parse_playing_state(const std::string& body) {
    PlayingState out;
    for (auto& f : pb_parse(body)) {
        if (f.f == 2) out.blocked = f.u != 0;
        else if (f.f == 3) out.app = uint32_t(f.u);
    }
    return out;
}

// ---- coalescing
// A game sets rich presence one key at a time (SetRichPresence per key), often
// several times a second while a menu is open. Steam wants the whole set, so
// changes are gathered and uploaded once they have been quiet for `delay`
// seconds; the first change of a burst starts the clock, later ones do not
// extend it, so an upload always follows within `delay`.
struct Coalescer {
    double delay = 1.0, due = 0;
    bool dirty = false;
    void touch(double now) { if (!dirty) { due = now + delay; dirty = true; } }
    bool ready(double now) const { return dirty && now >= due; }
    void sent() { dirty = false; }
};

// ---- the game's live rich presence, as its SetRichPresence calls left it
struct RichPresence {
    uint32_t appid = 0;
    std::map<std::string, std::string> kv;
    Coalescer coalescer;
    // SetRichPresence(key, NULL or "") deletes the key, as the API documents.
    void set(const std::string& key, const char* value, double now) {
        if (key.empty()) return;
        if (!value || !*value) { if (kv.erase(key)) coalescer.touch(now); return; }
        auto it = kv.find(key);
        if (it != kv.end() && it->second == value) return;
        kv[key] = value;
        coalescer.touch(now);
    }
    void clear(double now) { if (!kv.empty()) { kv.clear(); coalescer.touch(now); } }
    void reset(uint32_t app) { appid = app; kv.clear(); coalescer = Coalescer{}; }
};
} // namespace steam_presence
