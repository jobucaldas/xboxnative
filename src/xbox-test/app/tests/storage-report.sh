#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device evidence for free-space reading and launcher leftovers (installs.h)
# on a real filesystem under Wine.
set -euo pipefail
cd "$(dirname "$0")/.."
out=out/storage-report-tests
rm -rf "$out"; mkdir -p "$out"
"${CONFIG_CXX:-x86_64-w64-mingw32-g++}" -std=c++17 -O2 -Wall -Wextra -static tests/storage-report.cpp -o "$out/storage-report.exe"
root=$(WINEDEBUG=-all winepath -w "$out/root")
WINEDEBUG=-all timeout -k 2 30 wine "$out/storage-report.exe" "$root" | tr -d '\r' | tee "$out/result.log"
grep -q '^STORAGE-REPORT PASS: ' "$out/result.log"
