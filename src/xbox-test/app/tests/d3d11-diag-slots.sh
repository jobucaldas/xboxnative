#!/usr/bin/env bash
# Build-time slot audit: every vtable slot the diagnostics and the swap-chain
# overlay hook by number is checked against the SDK's own vtbl struct layout
# with offsetof. A wrong slot forwards a guest call to the wrong method at
# runtime; this test turns that into a build failure instead. Nothing runs: a
# compile that passes is the pass.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out/graphics-tests
x86_64-w64-mingw32-g++ -std=c++20 -O2 -Wall -Wextra -c tests/d3d11-diag-slots.cpp -o out/graphics-tests/d3d11-diag-slots.o
echo "PASS: d3d11-diag-slots (all hooked vtable slots match the SDK vtable order)"
