// SPDX-License-Identifier: GPL-3.0-or-later
// Native wire-shape regression for provider auth tickets and CM acknowledgements.
#include "../steam_auth.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace steam_auth;

struct FakeCm {
    struct Message { uint32_t emsg; std::string body; };
    std::vector<Message> messages;
    void send(uint32_t emsg, const std::string& body) { messages.push_back({emsg, body}); }
};

int main() {
    const uint32_t appid = 3405340, handle = 7;
    std::string ticket(32, '\0');
    uint32_t gc_size = 20; uint64_t token = 0x1122334455667788ULL;
    memcpy(ticket.data(), &gc_size, sizeof gc_size); memcpy(ticket.data() + 4, &token, sizeof token);
    for (size_t i = 12; i < ticket.size(); ++i) ticket[i] = char(i);

    FakeCm cm; Bridge bridge;
    bridge.start(appid, {[&](uint32_t emsg, const std::string& body) { cm.send(emsg, body); }, {}});
    assert(bridge.issue(handle, ticket));
    assert(cm.messages.size() == 2);

    // 5502 is CMsgClientRegisterAuthTicketWithCM: protocol version plus the
    // exact ticket bytes the provider returned to the game.
    assert(cm.messages[0].emsg == ClientRegisterAuthTicketWithCM);
    auto registered = pb_parse(cm.messages[0].body);
    assert(pb_get(registered, 1) && pb_get(registered, 1)->u == SteamClientProtocolVersion);
    assert(pb_get(registered, 3) && pb_get(registered, 3)->s == ticket);

    // The auth-list mirror carries the CRC and ticket, as SteamKit's
    // SteamAuthTicket.SendTickets does; no ticket bytes are synthesized here.
    assert(cm.messages[1].emsg == ClientAuthList);
    auto list = pb_parse(cm.messages[1].body);
    assert(pb_get(list, 5) && pb_get(list, 5)->u == appid);
    assert(pb_get(list, 6) && pb_get(list, 6)->u == 1);
    assert(pb_get(list, 4));
    auto item = pb_parse(pb_get(list, 4)->s);
    assert(pb_get(item, 4) && pb_get(item, 4)->wt == 1 && pb_get(item, 4)->u == appid);
    assert(pb_get(item, 6) && pb_get(item, 6)->u == crc32(ticket));
    assert(pb_get(item, 7) && pb_get(item, 7)->s == ticket);
    assert(gc_token(ticket) == token);

    std::string ack_body; pb_u(ack_body, 1, crc32(ticket)); pb_u(ack_body, 2, appid);
    assert(bridge.ack(ack_body) == 1 && bridge.acknowledged(handle));
    std::string unknown_ack; pb_u(unknown_ack, 1, 0xdeadbeefu);
    assert(bridge.ack(unknown_ack) == 0);

    // A new CM re-registers the active ticket and mirrors it with a new sequence.
    bridge.start(appid, {[&](uint32_t emsg, const std::string& body) { cm.send(emsg, body); }, {}});
    assert(cm.messages.size() == 4 && cm.messages[2].emsg == ClientRegisterAuthTicketWithCM);
    auto rebound = pb_parse(cm.messages[3].body);
    assert(pb_get(rebound, 6) && pb_get(rebound, 6)->u == 2);
    assert(pb_get(rebound, 4));

    // Cancel removes the ticket from the next auth list and never sends it again.
    assert(bridge.cancel(handle));
    assert(cm.messages.size() == 5 && cm.messages.back().emsg == ClientAuthList);
    auto canceled = pb_parse(cm.messages.back().body);
    assert(pb_get(canceled, 5) && pb_get(canceled, 5)->u == appid);
    assert(pb_get(canceled, 6) && pb_get(canceled, 6)->u == 3);
    assert(!pb_get(canceled, 4));

    // Ending a live bridge sends the final empty list before clearing local state.
    Bridge ending;
    ending.start(appid, {[&](uint32_t emsg, const std::string& body) { cm.send(emsg, body); }, {}});
    assert(ending.issue(handle, ticket));
    size_t before_end = cm.messages.size();
    assert(ending.end());
    assert(cm.messages.size() == before_end + 1 && cm.messages.back().emsg == ClientAuthList);
    auto ended = pb_parse(cm.messages.back().body);
    assert(pb_get(ended, 5) && pb_get(ended, 5)->u == appid);
    assert(pb_get(ended, 6) && pb_get(ended, 6)->u == 2);
    assert(!pb_get(ended, 4));
    puts("STEAM-AUTH PASS: protocol, sequence, reconnect replay, ack, cancellation, and shutdown");
    return 0;
}
