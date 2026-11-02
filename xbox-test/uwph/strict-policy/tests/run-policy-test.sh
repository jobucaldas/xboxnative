#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/wxl-strict-policy-test.XXXXXX")"
trap 'rm -rf "$work"' EXIT
out="$work/policy-test"
cxx="${CXX:-c++}"
"$cxx" -std=c++20 -Wall -Wextra -Werror -pedantic "$here/policy-test.cpp" -o "$out"
"$out"
