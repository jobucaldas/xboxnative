#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds and runs the focused guest oleaut32 route contract test.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tmp=$(mktemp -d /tmp/x86-oleaut.XXXXXX); trap 'rm -rf "$tmp"' EXIT
"${CXX:-c++}" -std=c++17 -O1 -mno-red-zone -Wall -Wextra -Werror "$here/x86-oleaut.cpp" -o "$tmp/x86-oleaut"
"$tmp/x86-oleaut"
