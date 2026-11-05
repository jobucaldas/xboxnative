#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Requires freshly source-built app/layout donors and record API, not a console.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
out=out/donor-tests
mkdir -p "$out/donors"
CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
UWP_OBJ_DIR="$out/obj" "$CB/scripts/build.sh" --uwp --out "$out/runner.exe" tests/donor-runtime.cpp -- /MT /O2 /Iout/obj
cp out/layout/donors/*.dll "$out/donors/"
cp out/layout/uwph-u32c.dll "$out/"
WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 40 wine "$out/runner.exe" | tr -d '\r' | tee "$out/result.log"
grep -Fx 'DONOR-RUNTIME PASS: behavior gate, cross-thread publication, retained addresses; no game code' "$out/result.log"
