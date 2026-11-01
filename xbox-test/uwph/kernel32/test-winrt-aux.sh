#!/bin/bash
# Off-device exact WinRT SDK contracts. Wine stub results remain explicit.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
UWP_OBJ_DIR="$work/obj" "$CB/scripts/build.sh" --uwp --out "$work/winrt-aux.exe" winrt_aux_test.cpp -- /MT /O2
WINEDEBUG=-all timeout 45s wine "$work/winrt-aux.exe"
