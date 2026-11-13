#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device regression for PE32+ runtime-function table validation.
set -euo pipefail
cd "$(dirname "$0")/.."
out=out/unwind-registration-tests
mkdir -p "$out"
cxx=${CONFIG_CXX:-x86_64-w64-mingw32-g++}
"$cxx" -std=c++17 -O2 -static tests/unwind-registration.cpp -o "$out/unwind-registration.exe" -lbcrypt
WINEDEBUG=-all timeout -k 2 30 wine "$out/unwind-registration.exe" | tr -d '\r' | tee "$out/result.log"
grep -Fx 'UNWIND-REGISTRATION PASS: empty exception directory accepted without registration' "$out/result.log"
grep -Fx 'UNWIND-REGISTRATION PASS: valid .pdata registered' "$out/result.log"
grep -Fx 'UNWIND-REGISTRATION PASS: exact duplicate ranges registered with one bounded warning' "$out/result.log"
grep -Fx 'UNWIND-REGISTRATION PASS: sorted overlapping function ranges rejected with first-failure details' "$out/result.log"
grep -Fx 'UNWIND-REGISTRATION PASS: unwind bounds diagnostic identifies the failing RVA section' "$out/result.log"
grep -Fx 'UNWIND-REGISTRATION PASS: unsorted table rejected before registration' "$out/result.log"
grep -Fx 'UNWIND-REGISTRATION PASS: out-of-image function range remains fatal' "$out/result.log"
