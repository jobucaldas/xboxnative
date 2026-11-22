#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Owned fixture only; offline Wine evidence, never a console claim.
# Builds the real packaged TLS carrier with the UWP toolchain, then checks the
# thread-exit order of a large swapped static-TLS block under Wine.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
cd "$(dirname "$0")/.."
out=out/tls-thread-exit
mkdir -p "$out/obj"
CB=${UWP_CROSSBUILD:-$root/out/uwp-crossbuild}
UWP_OBJ_DIR="$out/obj/carrier" "$CB/scripts/build.sh" --uwp --out "$out/carrier.dll" --link-arg /dll \
 ../uwph/host/tls_carrier.cpp -- /MT /O2 /GS-
# A NixOS mingw gcc keeps libstdc++'s gthread behind mcfgthread; point at the
# dev output when the environment names it (a distro mingw needs neither).
extra=()
[[ -n ${MCFG_INC:-} ]] && extra+=(-I"$MCFG_INC")
[[ -n ${MCFG_LIB:-} ]] && extra+=(-L"$MCFG_LIB")
"${CONFIG_CXX:-x86_64-w64-mingw32-g++}" -std=c++17 -O2 -static "${extra[@]}" tests/tls-thread-exit.cpp -o "$out/tls-thread-exit.exe" -lbcrypt
WINEDEBUG=-all timeout -k 2 60 wine "$out/tls-thread-exit.exe" "$out/carrier.dll" | tr -d '\r' | tee "$out/result.log"
grep -Fxq 'TLS-THREAD-EXIT PASS: FLS callbacks see guest TLS; carrier detach gets its own block back; no leak' "$out/result.log"
