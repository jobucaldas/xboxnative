// SPDX-License-Identifier: GPL-3.0-or-later
// steam_app_ticket.h: Play passes the owner's genuine ticket through whole,
// with the offsets the Steam client reports, and only for that ticket's app.
#include "../host/steam_app_ticket.h"
#include <cstdio>
#include <vector>
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)
static std::string ticket(uint32_t signedLength, uint64_t steamid, uint32_t appid, size_t signature) {
    std::string t(signedLength + signature, '\0');
    memcpy(&t[0], &signedLength, 4); uint32_t version = 4; memcpy(&t[4], &version, 4);
    memcpy(&t[8], &steamid, 8); memcpy(&t[16], &appid, 4);
    for (size_t i = signedLength; i < t.size(); ++i) t[i] = char(i);
    return t;
}
int main() {
    const uint64_t steamid = 0x0110000100000001ULL;   // synthetic account, not a real one
    auto t = ticket(50, steamid, 1259790, 128);
    steam_app_ticket::Layout l;
    CHECK(steam_app_ticket::parse(t, l) && l.size == 178 && l.appid == 1259790 && l.steamid == steamid);
    CHECK(l.signatureOffset == 50 && l.signatureSize == 128 && l.appidOffset == 16 && l.steamidOffset == 8);
    std::vector<char> out(0x400, 'x'); uint32_t a = 0, s = 0, g = 0, n = 0;
    CHECK(steam_app_ticket::copy(t, 1259790, out.data(), uint32_t(out.size()), &a, &s, &g, &n) == 178);
    CHECK(!memcmp(out.data(), t.data(), t.size()) && out[178] == 'x' && a == 16 && s == 8 && g == 50 && n == 128);
    // Another app, a short buffer, no ticket, or a malformed one: no ticket at all.
    CHECK(steam_app_ticket::copy(t, 480, out.data(), 0x400, &a, &s, &g, &n) == 0);
    CHECK(steam_app_ticket::copy(t, 1259790, out.data(), 177, &a, &s, &g, &n) == 0);
    CHECK(steam_app_ticket::copy({}, 1259790, out.data(), 0x400, &a, &s, &g, &n) == 0);
    CHECK(!steam_app_ticket::parse(ticket(178, steamid, 1259790, 0), l));
    CHECK(!steam_app_ticket::parse(ticket(50, 0, 1259790, 128), l));
    printf("STEAM APP TICKET checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
