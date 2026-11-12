#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# http-concurrent-state.sh -- build and run the HTTP client concurrency stress
# (tests/http-concurrent-state.cpp): a shared HttpClient instance rejects an
# overlapping send with 0x8000000C; the per-thread client() never does.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
bin=$(mktemp /tmp/http-concurrent-state.XXXXXX)
trap 'rm -f "$bin"' EXIT
"${CXX:-c++}" -std=c++20 -O2 -Wall -Wextra -Werror -pthread "$here/http-concurrent-state.cpp" -o "$bin"
"$bin"
