#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# ui-screens.sh -- build and run the download-queue/storage screen navigation
# test (tests/ui-screens.cpp, pure ui_nav.h rules, no rendering).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
app="$(cd "$here/.." && pwd)"
bin=$(mktemp /tmp/ui-screens.XXXXXX)
trap 'rm -f "$bin"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I"$app" -I"$app/out/ui-assets" "$here/ui-screens.cpp" -o "$bin"
"$bin"
