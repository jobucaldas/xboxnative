#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds and runs the Linux x86-64 DBT proof of concept; console-free.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-dbt.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror \
  "$here/x86-dbt.cpp" -o "$tmp/x86-dbt"
"$tmp/x86-dbt" "$@"
