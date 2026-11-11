#!/usr/bin/env bash
# MSVC SEH is required for the compiler-generated termination handler.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
clang-19 --target=x86_64-pc-windows-msvc -c unwind_target.S -o "$work/target.obj"
CB=${UWP_CROSSBUILD:-out/uwp-crossbuild}
UWP_OBJ_DIR="$work/obj" "$CB/scripts/build.sh" --uwp --out "$work/unwind-test.exe" --link-arg "$work/target.obj" unwind_test.cpp -- /MT /O2
WINEDEBUG=-all timeout 45s wine "$work/unwind-test.exe"
