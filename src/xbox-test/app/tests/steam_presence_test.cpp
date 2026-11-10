// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression: the presence wire shapes (games played, persona, rich
// presence blob) and the coalescing policy, against hand-built CM bodies.
#include "../steam_presence.h"
#include <cassert>
#include <cstdio>
using namespace steam_presence;

static void test_kv() {
    std::map<std::string, std::string> kv{{"steam_display", "#Status_InGame"}, {"connect", "+connect 1.2.3.4:27015"}};
    auto blob = encode_kv(kv);
    // type 0 "RP", then type 1 key/value pairs, then two 0x08 end markers.
    assert(blob[0] == '\0' && blob.compare(1, 2, "RP") == 0);
    assert(blob[blob.size() - 1] == '\x08' && blob[blob.size() - 2] == '\x08');
    std::map<std::string, std::string> back;
    assert(decode_kv(blob, back) && back == kv);
    // An empty set is still a valid blob (Steam reads it as "no rich presence").
    auto empty = encode_kv({});
    assert(decode_kv(empty, back) && back.empty());
    // Keys and values with NULs are dropped, not truncated.
    assert(decode_kv(encode_kv({{std::string("a\0b", 3), "v"}}), back) && back.empty());
    assert(!decode_kv("garbage", back));
    assert(!decode_kv(std::string("\0RP\0\2x\0y\0", 9), back));   // unknown value type
    puts("PRESENCE-KV-OK: rich presence blob round-trips; malformed blobs are refused");
}

static void test_requests() {
    auto playing = games_played_body(480);
    auto f = pb_parse(playing);
    assert(pb_get(f, 1));
    auto game = pb_parse(pb_get(f, 1)->s);
    assert(pb_get(game, 2) && pb_get(game, 2)->u == 480);   // fixed64 game_id
    assert(pb_get(f, 2) && pb_get(f, 2)->u == OsTypeWindows11);
    auto stopped = pb_parse(games_played_body(0));
    assert(!pb_get(stopped, 1) && pb_get(stopped, 2));   // empty games_played ends the session
    auto status = pb_parse(change_status_body());
    assert(pb_get(status, 1)->u == PersonaOnline);
    auto rp = pb_parse(rich_presence_upload_body({{"status", "In a run"}}));
    std::map<std::string, std::string> kv;
    assert(pb_get(rp, 1) && decode_kv(pb_get(rp, 1)->s, kv) && kv["status"] == "In a run");
    auto req = pb_parse(rich_presence_request_body(1234));
    assert(pb_get(req, 1)->u == 1234);
    auto persona = pb_parse(persona_request_body(1234));
    assert(pb_get(persona, 1)->u == PersonaFlagsWanted && pb_get(persona, 2)->u == 1234);
    puts("PRESENCE-REQUESTS-OK: games played, status, rich presence upload/request and persona request bodies");
}

static void test_replies() {
    // CMsgClientPersonaState with our friend entry: online, playing 3405340,
    // game name, and one rich presence pair.
    std::string kv; pb_s(kv, 1, "status"); pb_s(kv, 2, "In a run");
    std::string friendEntry; pb_fixed64(friendEntry, 1, 555); pb_u(friendEntry, 2, PersonaOnline);
    pb_u(friendEntry, 3, 3405340); pb_s(friendEntry, 55, "Megabonk"); pb_fixed64(friendEntry, 56, 3405340);
    pb_s(friendEntry, 71, kv);
    std::string body; pb_s(body, 2, friendEntry);
    auto p = parse_persona(body, 555);
    assert(p.found && p.state == PersonaOnline && p.game_played_app_id == 3405340);
    assert(p.game_name == "Megabonk" && p.rich_presence.at("status") == "In a run");
    assert(!parse_persona(body, 556).found);   // another account's entry is not ours

    std::string infoEntry; pb_fixed64(infoEntry, 1, 555); pb_s(infoEntry, 2, encode_kv({{"status", "Lobby"}}));
    std::string info; pb_s(info, 1, infoEntry);
    std::map<std::string, std::string> kvBack;
    assert(parse_rich_presence_info(info, 555, kvBack) && kvBack["status"] == "Lobby");
    assert(!parse_rich_presence_info(info, 556, kvBack));

    std::string playing; pb_u(playing, 2, 1); pb_u(playing, 3, 480);
    auto ps = parse_playing_state(playing);
    assert(ps.blocked && ps.app == 480);
    assert(!parse_playing_state(std::string()).blocked);
    puts("PRESENCE-REPLIES-OK: persona state, rich presence info and playing session state");
}

static void test_coalescer() {
    Coalescer c; c.delay = 1.0;
    assert(!c.ready(0));
    c.touch(10.0);
    assert(!c.ready(10.5));
    c.touch(10.9);                 // a later change does not push the deadline out
    assert(c.ready(11.0));
    c.sent();
    assert(!c.ready(12.0));
    RichPresence rp; rp.reset(480);
    rp.set("status", "Menu", 0.0);
    assert(rp.kv.at("status") == "Menu" && rp.coalescer.dirty);
    rp.coalescer.sent();
    rp.set("status", "Menu", 1.0);     // unchanged value: no upload
    assert(!rp.coalescer.dirty);
    rp.set("status", "Run", 2.0);      // changed value: upload
    assert(rp.coalescer.dirty);
    rp.coalescer.sent();
    rp.set("status", "", 3.0);         // empty value deletes the key
    assert(rp.kv.empty() && rp.coalescer.dirty);
    rp.coalescer.sent();
    rp.set("status", nullptr, 4.0);    // deleting a missing key changes nothing
    assert(!rp.coalescer.dirty);
    rp.set("a", "1", 5.0); rp.set("b", "2", 5.0);
    rp.clear(6.0);
    assert(rp.kv.empty() && rp.coalescer.dirty);
    rp.coalescer.sent(); rp.clear(7.0);
    assert(!rp.coalescer.dirty);       // clearing an empty set changes nothing
    puts("PRESENCE-COALESCE-OK: bursts upload once, unchanged values and empty clears do not");
}

int main() {
    test_kv();
    test_requests();
    test_replies();
    test_coalescer();
    puts("STEAM-PRESENCE-OK");
    return 0;
}
