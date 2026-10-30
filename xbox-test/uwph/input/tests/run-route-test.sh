#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
out="${TMPDIR:-/tmp}/wxl-input-route-$$"
trap 'rm -f "$out"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -pedantic "$here/route-test.cpp" -o "$out"
"$out"
