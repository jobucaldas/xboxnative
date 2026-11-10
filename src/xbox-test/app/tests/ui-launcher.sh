#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# ui-launcher.sh -- build the native launcher-screen test, run its assertions and
# render every approved mockup state to $UI_RENDER_OUT (default /tmp/xb51-renders)
# as launcher-*.png at 1920x1080 for RMSE comparison against the mockups.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
app="$(cd "$here/.." && pwd)"
root="$(cd "$app/../../.." && pwd)"
assets="$root/out/ui-assets"
[[ -f $assets/stb_truetype.h && -f $assets/selawk.ttf ]] || bash "$root/src/tools/fetch-ui-assets.sh" "$assets"
out="${UI_RENDER_OUT:-/tmp/xb51-renders}"
mkdir -p "$out"
bin=$(mktemp /tmp/ui-launcher.XXXXXX)
trap 'rm -f "$bin"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I"$assets" -I"$app" "$here/ui-launcher.cpp" -o "$bin"
UI_RENDER_OUT="$out" "$bin" "$assets"
