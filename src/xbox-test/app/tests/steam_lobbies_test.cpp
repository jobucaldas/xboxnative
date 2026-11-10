// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the CM lobby wire layer against a fake CM service that
// answers in the real CMsgClientMMS* shapes, and for the bridge policy that
// keeps the launcher's view of the lobby consistent across pushed messages.
#include "../steam_lobbies.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using namespace steam_lobbies;

// A fake Steam backend: lobbies keyed by id, members, metadata, and the pushed
// forms the CM would emit for a change. Every request is recorded with its
// routing app id, because Steam answers nothing without it.
struct Server {
    struct Row {
        uint64_t owner = 0;
        int32_t max_members = 0, lobby_type = 0, lobby_flags = 0;
        std::string metadata;
        std::map<uint64_t, std::string> members;   // steamid -> persona
    };
    std::map<uint64_t, Row> lobbies;
    std::vector<std::pair<uint32_t, uint32_t>> routed;   // (emsg, routing_appid)
    std::string last_set_body, last_server_body, last_list_body;
    unsigned creates = 0, joins = 0, leaves = 0, lists = 0, sets = 0, chats = 0, invites = 0, statuses = 0, servers = 0;
    int32_t create_eresult = ResultOK;
    int32_t join_eresult = ResultOK;
    int32_t leave_eresult = ResultOK;
    int32_t set_eresult = ResultOK;
    int32_t list_eresult = ResultOK;
    uint64_t next_id = 0x1100001000000ull;

    static std::string member_blob(uint64_t id, const std::string& persona, const std::string& meta = {}) {
        std::string m;
        pb_fixed64(m, 1, id); pb_s(m, 2, persona);
        if (!meta.empty()) pb_s(m, 3, meta);
        return m;
    }
    void emit_lobby_data(std::string& resp, uint64_t id) const {
        auto it = lobbies.find(id);
        if (it == lobbies.end()) return;
        const Row& r = it->second;
        pb_u(resp, 1, 3405340); pb_fixed64(resp, 2, id);
        pb_u(resp, 3, uint64_t(int64_t(r.members.size()))); pb_u(resp, 4, uint64_t(int64_t(r.max_members)));
        pb_u(resp, 5, uint64_t(int64_t(r.lobby_type))); pb_u(resp, 6, uint64_t(int64_t(r.lobby_flags)));
        pb_fixed64(resp, 7, r.owner); pb_s(resp, 8, r.metadata);
        for (auto& m : r.members) pb_s(resp, 9, member_blob(m.first, m.second));
    }
    bool call(uint32_t emsg, uint32_t routing, const std::string& body, std::string& resp) {
        routed.emplace_back(emsg, routing);
        auto f = pb_parse(body);
        resp.clear();
        switch (emsg) {
        case CreateLobby: {
            ++creates;
            uint64_t id = next_id++;
            Row r;
            r.max_members = int32_t(pb_get(f, 2)->u);
            r.lobby_type = int32_t(pb_get(f, 3)->u);
            r.lobby_flags = int32_t(pb_get(f, 4)->u);
            if (auto m = pb_get(f, 7)) r.metadata = m->s;
            r.owner = 0x110000100000001ull;
            r.members[r.owner] = pb_get(f, 8) ? pb_get(f, 8)->s : std::string();
            pb_u(resp, 1, routing);
            if (create_eresult == ResultOK) {
                lobbies[id] = r;
                pb_fixed64(resp, 2, id);
            }
            pb_u(resp, 3, uint64_t(int64_t(create_eresult)));
            return true;
        }
        case JoinLobby: {
            ++joins;
            uint64_t id = pb_get(f, 2)->u;
            pb_u(resp, 1, routing);
            auto it = lobbies.find(id);
            if (join_eresult != ResultOK || it == lobbies.end()) {
                // A lobby Steam does not have answers k_EResultNoMatch, not OK.
                int32_t why = it == lobbies.end() ? ResultNoMatch : join_eresult;
                pb_fixed64(resp, 2, id);
                pb_u(resp, 3, uint64_t(int64_t(why)));
                return true;
            }
            Row& r = it->second;
            uint64_t me = 0x110000100000009ull;
            r.members[me] = pb_get(f, 3) ? pb_get(f, 3)->s : std::string();
            pb_fixed64(resp, 2, id);
            pb_u(resp, 3, 1);
            pb_u(resp, 4, uint64_t(int64_t(r.max_members)));
            pb_u(resp, 5, uint64_t(int64_t(r.lobby_type)));
            pb_u(resp, 6, uint64_t(int64_t(r.lobby_flags)));
            pb_fixed64(resp, 7, r.owner);
            pb_s(resp, 8, r.metadata);
            for (auto& m : r.members) pb_s(resp, 9, member_blob(m.first, m.second));
            return true;
        }
        case LeaveLobby: {
            ++leaves;
            uint64_t id = pb_get(f, 2)->u;
            pb_u(resp, 1, routing); pb_fixed64(resp, 2, id);
            pb_u(resp, 3, uint64_t(int64_t(leave_eresult)));
            if (leave_eresult == ResultOK) lobbies.erase(id);
            return true;
        }
        case GetLobbyList: {
            ++lists;
            last_list_body = body;
            pb_u(resp, 1, routing);
            pb_u(resp, 3, uint64_t(int64_t(list_eresult)));
            if (list_eresult != ResultOK) return true;
            for (auto& l : lobbies) {
                std::string entry;
                pb_fixed64(entry, 1, l.first);
                pb_u(entry, 2, uint64_t(int64_t(l.second.max_members)));
                pb_u(entry, 3, uint64_t(int64_t(l.second.lobby_type)));
                pb_u(entry, 4, uint64_t(int64_t(l.second.lobby_flags)));
                pb_s(entry, 5, l.second.metadata);
                pb_u(entry, 6, uint64_t(int64_t(l.second.members.size())));
                pb_s(resp, 4, entry);
            }
            return true;
        }
        case SetLobbyData: {
            ++sets;
            last_set_body = body;
            uint64_t id = pb_get(f, 2)->u;
            pb_u(resp, 1, routing); pb_fixed64(resp, 2, id);
            pb_u(resp, 3, uint64_t(int64_t(set_eresult)));
            if (set_eresult == ResultOK) {
                auto it = lobbies.find(id);
                if (it != lobbies.end()) {
                    if (auto m = pb_get(f, 7)) it->second.metadata = m->s;
                    if (auto m = pb_get(f, 4)) it->second.max_members = int32_t(m->u);
                    if (auto m = pb_get(f, 5)) it->second.lobby_type = int32_t(m->u);
                }
            }
            return true;
        }
        case GetLobbyData: {
            emit_lobby_data(resp, pb_get(f, 2)->u);
            if (resp.empty()) return false;
            return true;
        }
        case SendLobbyChatMsg: {
            ++chats;
            return true;   // no response message exists
        }
        case InviteToLobby: {
            ++invites;
            return true;
        }
        case SetLobbyGameServer: {
            ++servers;
            last_server_body = body;
            return true;   // the effect arrives as the pushed LobbyGameServerSet
        }
        case GetLobbyStatus: {
            ++statuses;
            uint64_t id = pb_get(f, 2)->u;
            pb_u(resp, 1, routing); pb_fixed64(resp, 2, id);
            int32_t status = lobbies.count(id) ? LobbyStatusMember : LobbyStatusNotMember;
            if (lobbies.count(id) && lobbies[id].owner == 0x110000100000001ull) status = LobbyStatusOwner;
            pb_u(resp, 3, uint64_t(int64_t(status)));
            return true;
        }
        }
        return false;
    }
};

static bool has_line(const std::vector<std::string>& log, const std::string& needle) {
    for (auto& l : log) if (l.find(needle) != std::string::npos) return true;
    return false;
}

int main() {
    Server server;
    std::vector<std::string> log;
    Io io;
    io.call = [&](uint32_t emsg, uint32_t routing, const std::string& body, std::string& resp) {
        return server.call(emsg, routing, body, resp);
    };
    io.log = [&](const std::string& line) { log.push_back(line); };

    // ---- the Steamworks enum values, which the CM compares against
    assert(CompareEqualOrLess == -2 && CompareLess == -1 && CompareEqual == 0);
    assert(CompareGreater == 1 && CompareEqualOrGreater == 2 && CompareNotEqual == 3);
    assert(LobbyTypePrivate == 0 && LobbyTypeFriendsOnly == 1 && LobbyTypePublic == 2 && LobbyTypeInvisible == 3);

    // ---- create
    auto made = create(io, 3405340, 8, LobbyTypePublic, 0, "mode=coop", "host");
    assert(made.found);
    assert(made.steamid != 0);
    assert(made.eresult == ResultOK);
    assert(server.creates == 1);
    assert(server.routed.back().first == CreateLobby && server.routed.back().second == 3405340);
    uint64_t lobby_id = made.steamid;

    // ---- create refused: the game must see the failure, not a fake id
    {
        Server refusing;
        refusing.create_eresult = ResultLimitExceeded;
        Io io2;
        io2.call = [&](uint32_t e, uint32_t r, const std::string& b, std::string& resp) { return refusing.call(e, r, b, resp); };
        std::vector<std::string> lines;
        io2.log = [&](const std::string& l) { lines.push_back(l); };
        auto refused = create(io2, 3405340, 8, LobbyTypePublic, 0, "", "");
        assert(!refused.found && refused.steamid == 0);
        assert(refused.eresult == ResultLimitExceeded);
        assert(has_line(lines, "create refused (eresult 25)"));
    }

    // ---- list
    std::vector<LobbySummary> found;
    std::vector<Filter> filters = {{"mode", "coop", CompareEqual, FilterString}};
    assert(list(io, 3405340, 10, filters, found));
    assert(found.size() == 1);
    assert(found[0].steamid == lobby_id);
    assert(found[0].max_members == 8 && found[0].lobby_type == LobbyTypePublic);
    assert(found[0].num_members == 1);
    assert(found[0].metadata == "mode=coop");
    assert(server.routed.back().second == 3405340);
    {
        auto f = pb_parse(server.last_list_body);
        auto filt = pb_get(f, 6);   // repeated Filter; 5 is the deprecated public ip
        assert(filt && !filt->s.empty());
        auto inner = pb_parse(filt->s);
        auto cmp = pb_get(inner, 3);
        assert(cmp && int32_t(cmp->u) == CompareEqual);
    }

    // ---- join: the member list and owner come from the CM
    auto joined = join(io, 3405340, lobby_id, "friend");
    assert(joined.found && joined.steamid == lobby_id);
    assert(joined.owner == 0x110000100000001ull);
    assert(joined.members.size() == 2);
    assert(joined.members[0].persona == "host" || joined.members[1].persona == "host");
    assert(joined.metadata == "mode=coop");

    // ---- join refused: a lobby Steam does not have
    {
        auto missing = join(io, 3405340, 0x11000010000ffffull, "friend");
        assert(!missing.found && missing.steamid == 0x11000010000ffffull);
        assert(missing.eresult == ResultNoMatch);
        assert(has_line(log, "join refused (eresult 9)"));
    }

    // ---- data
    auto data_now = data(io, 3405340, lobby_id);
    assert(data_now.found && data_now.steamid == lobby_id);
    assert(data_now.owner == 0x110000100000001ull);
    assert(data_now.members.size() == 2);

    // ---- set data, chat, invite, server, owner, status
    assert(set_data(io, 3405340, lobby_id, 0, "mode=coop;map=1"));
    assert(server.sets == 1);
    assert(server.lobbies[lobby_id].metadata == "mode=coop;map=1");
    {
        // Metadata only: the int fields are absent, so the CM keeps them.
        auto f = pb_parse(server.last_set_body);
        assert(pb_get(f, 7) && pb_get(f, 7)->s == "mode=coop;map=1");
        assert(!pb_get(f, 4) && !pb_get(f, 5) && !pb_get(f, 6));
    }
    // A change to private is 0 on the wire, not a dropped field.
    assert(set_data(io, 3405340, lobby_id, 0, "mode=coop;map=1", std::nullopt, LobbyTypePrivate));
    {
        auto f = pb_parse(server.last_set_body);
        auto type = pb_get(f, 5);
        assert(type && type->u == 0);
        assert(server.lobbies[lobby_id].lobby_type == LobbyTypePrivate);
    }
    assert(send_chat(io, 3405340, lobby_id, 0, "hello"));
    assert(server.chats == 1);
    assert(invite(io, 3405340, lobby_id, 0x11000010000000aull));
    assert(server.invites == 1);
    assert(set_server(io, 3405340, lobby_id, 0x0a000001u, 27015, 0));
    assert(server.servers == 1);
    {
        auto f = pb_parse(server.last_server_body);
        auto addr = pb_get(f, 6);
        assert(addr && addr->wt == 2);
        auto a = pb_parse(addr->s);
        assert(a.size() == 1 && a[0].f == 1 && a[0].wt == 5 && uint32_t(a[0].u) == 0x0a000001u);
        assert(pb_get(f, 4) && pb_get(f, 4)->u == 27015);
    }
    // A Steam-ID-only server has no address, but its port still goes out.
    assert(set_server(io, 3405340, lobby_id, 0, 27016, 0x11000010000000bull));
    {
        auto f = pb_parse(server.last_server_body);
        assert(!pb_get(f, 6));
        assert(pb_get(f, 4) && pb_get(f, 4)->u == 27016);
        assert(pb_get(f, 5) && pb_get(f, 5)->u == 0x11000010000000bull);
    }
    assert(status(io, 3405340, lobby_id, true, true) == LobbyStatusOwner);
    assert(status(io, 3405340, 0x11000010000ffffull, true, false) == LobbyStatusNotMember);
    // A chat message over the API's limit is refused before the wire.
    std::string huge(MaxChatMsg + 1, 'x');
    assert(!send_chat(io, 3405340, lobby_id, 0, huge));
    assert(server.chats == 1);

    // ---- every request carried the app id
    for (auto& r : server.routed) assert(r.second == 3405340);

    // ---- pushed forms
    {
        std::string body;
        pb_u(body, 1, 3405340); pb_fixed64(body, 2, lobby_id);
        pb_fixed64(body, 3, 0x110000100000002ull); pb_s(body, 4, "hi");
        auto chat = parse_chat(body);
        assert(chat.lobby == lobby_id && chat.sender == 0x110000100000002ull && chat.message == "hi");

        std::string joined_body;
        pb_u(joined_body, 1, 3405340); pb_fixed64(joined_body, 2, lobby_id);
        pb_fixed64(joined_body, 3, 0x110000100000003ull); pb_s(joined_body, 4, "new");
        auto joined_msg = parse_membership(joined_body, true);
        assert(joined_msg.joined && joined_msg.user == 0x110000100000003ull && joined_msg.persona == "new");
        auto left_msg = parse_membership(joined_body, false);
        assert(!left_msg.joined);

        std::string server_body;
        pb_u(server_body, 1, 3405340); pb_fixed64(server_body, 2, lobby_id);
        pb_u(server_body, 3, 0x0a000001u); pb_u(server_body, 4, 27015);
        auto server_set = parse_server_set(server_body);
        assert(server_set.lobby == lobby_id && server_set.ipv4 == 0x0a000001u && server_set.port == 27015);

        // The modern field, which is what a current CM sends, is read too.
        std::string modern_body;
        pb_u(modern_body, 1, 3405340); pb_fixed64(modern_body, 2, lobby_id);
        pb_s(modern_body, 6, ip_address(0x0a000002u)); pb_u(modern_body, 4, 27016);
        auto modern = parse_server_set(modern_body);
        assert(modern.lobby == lobby_id && modern.ipv4 == 0x0a000002u && modern.port == 27016);
    }

    // ---- the bridge policy
    {
        Server s2;
        Io io2;
        io2.call = [&](uint32_t e, uint32_t r, const std::string& b, std::string& resp) { return s2.call(e, r, b, resp); };
        std::vector<std::string> lines;
        io2.log = [&](const std::string& l) { lines.push_back(l); };
        Bridge bridge;
        assert(!bridge.in_lobby());

        // create -> the bridge is the owner of the new lobby
        assert(bridge.create(io2, 3405340, 4, LobbyTypeFriendsOnly, 0, "mode=coop", "host"));
        assert(bridge.in_lobby());
        assert(bridge.owner_self);
        // The owner is the account that created it, as the CM reports it — not
        // the lobby id, which is what CreateLobbyResponse alone would leave.
        assert(bridge.owner == 0x110000100000001ull);
        assert(bridge.members.size() == 1);
        assert(bridge.max_members == 4 && bridge.metadata == "mode=coop");
        assert(has_line(lines, "created lobby#"));
        uint64_t id2 = bridge.lobby;

        // a pushed join for this lobby is applied; one for another lobby is not
        Membership other; other.lobby = id2 + 1; other.user = 0x110000100000004ull; other.joined = true;
        assert(!bridge.apply_membership(other));
        Membership mine; mine.lobby = id2; mine.user = 0x110000100000004ull; mine.persona = "friend"; mine.joined = true;
        assert(bridge.apply_membership(mine));
        assert(bridge.members.size() == 2 && bridge.members[0x110000100000004ull] == "friend");

        // a pushed LobbyData replaces the view
        Lobby pushed;
        pushed.found = true; pushed.steamid = id2; pushed.owner = 0x110000100000004ull;
        pushed.max_members = 4; pushed.lobby_type = LobbyTypeFriendsOnly;
        pushed.metadata = "mode=coop;map=2";
        pushed.members = {Member{0x110000100000004ull, "friend", {}}};
        assert(bridge.apply_lobby_data(pushed));
        assert(bridge.owner == 0x110000100000004ull && bridge.metadata == "mode=coop;map=2");
        assert(bridge.members.size() == 1);

        // chat and server pushes are only for this lobby
        ChatMsg chat; chat.lobby = id2; chat.sender = 0x110000100000004ull; chat.message = "x";
        assert(bridge.accepts_chat(chat));
        chat.lobby = id2 + 1;
        assert(!bridge.accepts_chat(chat));
        GameServer gs; gs.lobby = id2;
        assert(bridge.accepts_server(gs));
        gs.lobby = 0;
        assert(!bridge.accepts_server(gs));

        // the owner leaving drops the owner until the pushed LobbyData says who has it
        Membership owner_left; owner_left.lobby = id2; owner_left.user = 0x110000100000004ull; owner_left.joined = false;
        assert(bridge.apply_membership(owner_left));
        assert(bridge.owner == 0);
        assert(bridge.members.empty());

        // leave clears the local view even when the CM refuses
        s2.leave_eresult = ResultFail;
        assert(!bridge.leave(io2, 3405340));
        assert(!bridge.in_lobby());
        assert(bridge.members.empty() && bridge.owner == 0);

        // create refused leaves the bridge in no lobby
        s2.create_eresult = ResultFail;
        assert(!bridge.create(io2, 3405340, 4, LobbyTypePublic, 0, "", ""));
        assert(!bridge.in_lobby());
    }

    // ---- transport failure is reported, never thrown
    {
        Io dead;
        dead.call = [](uint32_t, uint32_t, const std::string&, std::string&) { return false; };
        std::vector<std::string> lines;
        dead.log = [&](const std::string& l) { lines.push_back(l); };
        auto none = create(dead, 3405340, 4, LobbyTypePublic, 0, "", "");
        assert(!none.found);
        std::vector<LobbySummary> rows;
        assert(!list(dead, 3405340, 10, {}, rows));
        assert(rows.empty());
        assert(!leave(dead, 3405340, 1));
        assert(has_line(lines, "transport"));
    }

    puts("STEAM-LOBBIES PASS: MMS wire shapes, routing app id, bridge membership/owner policy, transport failure");
    return 0;
}
