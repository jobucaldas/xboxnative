#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$root" -x c++ -o "$tmp/test" - <<'CPP'
#include "host/d3d12_sm_cap.h"
#include <cassert>
int main() {
    using d3d12_sm_cap::parse;
    assert(parse("6.0", 3) == 0);
    assert(parse(" 6.2\r\n", 6) == 2);
    assert(parse("6.8\n", 4) == 8);
    assert(parse("6.9", 3) == -1);
    assert(parse("5.0", 3) == -1);
    assert(parse("6.x", 3) == -1);
    assert(parse("6.10", 4) == -1);
    assert(parse(nullptr, 0) == -1);
}
CPP
"$tmp/test"
echo 'PASS d3d12 shader-model cap opt-in parser'
