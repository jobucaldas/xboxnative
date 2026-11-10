#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Host compiler only: hid/pad_registry.h and hid/route.h carry no Windows
# dependency beyond the declarations in tests/fake/windows.h, so CI checks the
# registry state machine and the hid.dll answers without Wine or a cross build.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/pad-registry.XXXXXX")"
trap 'rm -rf "$work"' EXIT
out="$work/pad-registry-test"
# -fshort-wchar: Windows is LLP64 with a 16-bit wchar_t, and the registry reads
# the device path through wchar_t*. The host compiler's 32-bit wchar_t would
# misread it, so the test builds with the platform's width.
"${CXX:-c++}" -std=c++20 -fshort-wchar -Wall -Wextra -Werror -pedantic -I "$here/fake" "$here/pad-registry-test.cpp" -o "$out"
"$out"
