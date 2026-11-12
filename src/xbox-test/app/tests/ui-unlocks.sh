#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# ui-unlocks.sh -- build and run the guide achievements glue test (ui_unlocks.h:
# unlock->view mapping, earned-set diffs, schema index lookup, CDN icon URL and
# a fake achievements.json round-trip). Assert-based, no rendering.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
app="$(cd "$here/.." && pwd)"
root="$(cd "$app/../../.." && pwd)"
assets="$root/out/ui-assets"
[[ -f $assets/stb_truetype.h && -f $assets/selawk.ttf ]] || bash "$root/src/tools/fetch-ui-assets.sh" "$assets"
bin=$(mktemp /tmp/ui-unlocks.XXXXXX)
trap 'rm -f "$bin"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I"$assets" -I"$app" "$here/ui-unlocks.cpp" -o "$bin"
"$bin"
