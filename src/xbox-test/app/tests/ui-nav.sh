#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# ui-nav.sh -- build and run the launcher navigation test (ui_nav.h rules:
# sidebar focus, view switching, sort cycling, queue status copy, game-screen
# focus slots, logout reachability). Assert-based, no rendering.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
app="$(cd "$here/.." && pwd)"
bin=$(mktemp /tmp/ui-nav.XXXXXX)
trap 'rm -f "$bin"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I"$app" "$here/ui-nav.cpp" -o "$bin"
"$bin"
