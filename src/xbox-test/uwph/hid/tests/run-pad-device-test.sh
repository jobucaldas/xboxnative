#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Host compiler only: the synthetic pad's HID layout and cap rules carry no
# Windows dependency, so CI checks them without Wine or a cross build.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/pad-device.XXXXXX")"
trap 'rm -rf "$work"' EXIT
out="$work/pad-device-test"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -pedantic "$here/pad-device-test.cpp" -o "$out"
"$out"
