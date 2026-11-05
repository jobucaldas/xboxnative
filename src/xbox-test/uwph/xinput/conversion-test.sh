#!/bin/bash
# Uses actual SDK WGI data types; never activates WGI or contacts the console.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
UWP_OBJ_DIR="$work/obj" "$CB/scripts/build.sh" --out "$work/conversion.exe" conversion-test.cpp -- /MT /O2 /EHsc
WINEDEBUG=-all wine "$work/conversion.exe"
