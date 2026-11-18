#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds and runs the focused Winsock/XInput ordinal contract test.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tmp=$(mktemp -d /tmp/x86-socket-input.XXXXXX); trap 'rm -rf "$tmp"' EXIT
"${CXX:-c++}" -std=c++17 -O1 -mno-red-zone -Wall -Wextra -Werror "$here/x86-socket-input.cpp" -o "$tmp/x86-socket-input"
"$tmp/x86-socket-input"
