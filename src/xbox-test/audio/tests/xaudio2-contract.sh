#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${TMPDIR:-/tmp}/xaudio2-contract-$$.o"
trap 'rm -f "$out"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -Wall -Wextra -Werror -c "$here/xaudio2-contract.cpp" -o "$out"
echo "PASS: XAudio2 and WASAPI SDK signatures, arities and vtable slots"
