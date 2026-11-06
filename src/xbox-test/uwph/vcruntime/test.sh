#!/bin/bash
# Off-device contract test for the vcruntime140 route (Wine). No console use.
# Uses the pinned uwp-crossbuild toolchain (UWP_CROSSBUILD), as app/tests do.
set -euo pipefail
cd "$(dirname "$0")"
CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
UWP_OBJ_DIR="$work/obj" "$CB/scripts/build.sh" --out "$work/vcruntime-test.exe" test.cpp -- /MT /O1 /EHsc >"$work/build.log" 2>&1 || { tail -20 "$work/build.log"; exit 1; }
WINEDEBUG=-all wine "$work/vcruntime-test.exe"
