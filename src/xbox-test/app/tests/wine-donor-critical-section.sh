#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
donors=${1:-$root/out/wine-donors/runtime}
[[ -f $donors/msvcp140.dll ]] || { echo "missing rebuilt donor: $donors/msvcp140.dll" >&2; exit 2; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
  "$root/src/xbox-test/app/tests/wine-donor-critical-section.cpp" -o "$work/wine-donor-critical-section.exe"
(
  cd "$donors"
  WINEDEBUG=-all timeout 45s wine "$work/wine-donor-critical-section.exe"
)
