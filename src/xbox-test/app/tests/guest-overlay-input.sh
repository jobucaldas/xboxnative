#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")"
out=$(mktemp)
trap 'rm -f "$out"' EXIT
c++ -std=c++17 -O2 -Wall -Wextra -Werror guest-overlay-input.cpp -o "$out"
"$out"
