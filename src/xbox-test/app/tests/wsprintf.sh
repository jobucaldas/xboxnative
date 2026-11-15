#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/wsprintf.XXXXXX")
trap 'rm -rf "$work"' EXIT
link_flags=()
if [[ -n ${MINGW_LDFLAGS:-} ]]; then read -r -a link_flags <<< "$MINGW_LDFLAGS"; fi
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
    "${link_flags[@]}" "$here/wsprintf.cpp" -o "$work/wsprintf.exe"
WINEDEBUG=-all timeout -k 2 20 wine "$work/wsprintf.exe"
