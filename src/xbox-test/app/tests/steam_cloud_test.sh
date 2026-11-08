#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Native regression for the Auto-Cloud planner; links the vendored zlib inflate
# the launcher uses for compressed cloud copies.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
zlib="$here/../vendor/zlib"
dir=$(mktemp -d /tmp/steam-cloud-test-build.XXXXXX)
trap 'rm -rf "$dir"' EXIT
for f in inflate inftrees inffast adler32 crc32 zutil; do
  "${CC:-cc}" -w -O2 -c "$zlib/$f.c" -o "$dir/$f.o"
done
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror "$here/steam_cloud_test.cpp" "$dir"/*.o -o "$dir/steam-cloud-test"
"$dir/steam-cloud-test"
