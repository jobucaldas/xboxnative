#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
bin=$(mktemp /tmp/launch-status.XXXXXX)
trap 'rm -f "$bin"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror "$here/launch-status.cpp" -o "$bin"
"$bin"
python3 -I "$here/launch-result-test.py"
