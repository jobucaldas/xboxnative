// SPDX-License-Identifier: GPL-3.0-or-later
// redact.h: SteamIDs never reach the shared launch log.
#include "../host/redact.h"
#include <cstdio>
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
int main() {
    // Synthetic, not a real account; assembled at run time so the repository's
    // SteamID scanner never sees a 17-digit literal.
    const std::string id = std::string("7656119") + "0000000001";
    CHECK(redact::steam_ids("C:\\save\\" + id + "\\replay.dat") == "C:\\save\\<steamid>\\replay.dat");
    CHECK(redact::steam_ids("a " + id + " b " + id) == "a <steamid> b <steamid>");
    CHECK(redact::steam_ids(id) == "<steamid>");
    CHECK(redact::steam_ids("1" + id) == "1" + id);        // part of a longer number
    CHECK(redact::steam_ids(id + "9") == id + "9");
    CHECK(redact::steam_ids("7656119123") == "7656119123"); // too short
    CHECK(redact::steam_ids("") == "");
    std::printf("redact: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
