// SPDX-License-Identifier: GPL-3.0-or-later
// Native test for playable.h: the built-in owner-verified set and the
// playable.txt parser (comments, junk, overflow). Assert-based, no Win32.
#include "../playable.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace playable;
    // built-in set: the two owner-verified appids, nothing else
    assert(kBuiltInCount == 2);
    assert(is_builtin(3405340));    // Megabonk
    assert(is_builtin(1259790));    // Puyo Puyo Tetris 2
    assert(!is_builtin(0));
    assert(!is_builtin(480));
    assert(!is_builtin(1259791));

    // txt parsing: one appid per line, comments, junk ignored
    auto p = parse_txt("3405340\n1259790\n");
    assert(p.size() == 2 && p[0] == 3405340 && p[1] == 1259790);
    // comments: whole-line and trailing; blank lines skipped
    p = parse_txt("# a comment\n\n  480  \n#nope\n12345 # trailing\n");
    assert(p.size() == 2 && p[0] == 480 && p[1] == 12345);
    // junk lines ignored; \r\n tolerated
    p = parse_txt("megabonk\r\n34o5\r\n99\r\n");
    assert(p.size() == 1 && p[0] == 99);
    // overflow and zero are dropped, not truncated
    assert(parse_txt("4294967296\n").empty());
    assert(parse_txt("00000000\n").empty());
    assert(parse_txt("4294967295\n").size() == 1 && parse_txt("4294967295\n")[0] == 4294967295u);
    // empty text: nothing
    assert(parse_txt("").empty());
    assert(parse_txt("# only a comment\n").empty());
    std::puts("PLAYABLE-OK: built-in set and playable.txt parsing");
    return 0;
}
