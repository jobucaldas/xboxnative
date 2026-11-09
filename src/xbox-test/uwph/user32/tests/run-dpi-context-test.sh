#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Host compiler only: the DPI_AWARENESS_CONTEXT encoding and validation rules
# carry no Windows dependency, so CI checks them without Wine or a cross build.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/u32c-dpi-context.XXXXXX")"
trap 'rm -rf "$work"' EXIT
out="$work/dpi-context-test"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -pedantic "$here/dpi-context-test.cpp" -o "$out"
"$out"
