#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/gap-summary.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror "$here/gap-summary.cpp" -o "$tmp/gap-summary"
"$tmp/gap-summary"
