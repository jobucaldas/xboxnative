#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${TMPDIR:-/tmp}/dsound-ring-$$"
trap 'rm -f "$out"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror "$here/ring-test.cpp" -o "$out"
"$out"
