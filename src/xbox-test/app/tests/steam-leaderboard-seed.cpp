// SPDX-License-Identifier: GPL-3.0-or-later
// Host-side fixture for tests/steam-leaderboard-gbe.sh: writes a board's cache
// file with the launcher's own encoder (app/steam_leaderboards.h), so the test
// proves the file the launcher writes is the file the pinned provider reads —
// not a hand-built copy of its format that could drift.
// Usage: steam-leaderboard-seed <saves root>   (writes <root>/3405340/leaderboard/v2_kills)
#include "steam_leaderboards.h"
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { fputs("usage: steam-leaderboard-seed <saves root>\n", stderr); return 2; }
    std::vector<steam_leaderboards::Entry> rows(3);
    rows[0].steamid = 0x1100000100000001ull; rows[0].score = 1500;
    rows[1].steamid = 0x1100000100000002ull; rows[1].score = 900;
    rows[2].steamid = 0x1100000100000003ull; rows[2].score = 400;
    auto path = steam_leaderboards::cache_path(argv[1], 3405340, "v2_kills");
    if (!path) { fputs("bad board name\n", stderr); return 3; }
    if (!steam_leaderboards::write_cache(*path, rows)) { fputs("write failed\n", stderr); return 4; }
    printf("%s\n", path->string().c_str());
    return 0;
}
