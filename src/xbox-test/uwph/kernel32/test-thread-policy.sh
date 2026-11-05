#!/bin/bash
# Compile the actual UWP branch and audit its activation imports.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
UWP_OBJ_DIR="$work/obj" "$CB/scripts/build.sh" --uwp --out "$work/thread-policy.exe" thread_policy_test.cpp -- /MT /O2
PATH="$HOME/.local/bin:$PATH" bash "$CB/scripts/pe-import-audit.sh" --allow-kernel32 "$work/thread-policy.exe"
WINEDEBUG=-all timeout 45s wine "$work/thread-policy.exe"
