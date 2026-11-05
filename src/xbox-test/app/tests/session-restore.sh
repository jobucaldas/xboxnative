#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
bin=$(mktemp /tmp/session-restore.XXXXXX)
trap 'rm -f "$bin"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror "$here/session_restore_test.cpp" -o "$bin"
"$bin"
