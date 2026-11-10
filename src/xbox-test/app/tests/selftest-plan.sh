#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The console self-test's planning logic, off-device: fixture list, request
# parsing, the restart queue and exit-code verdicts (selftest_plan.h). The Play
# run itself happens on the console (selftest.h); this is deterministic and
# console-free.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
bin=$(mktemp /tmp/selftest-plan.XXXXXX)
trap 'rm -f "$bin"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror "$here/selftest-plan-test.cpp" -o "$bin"
"$bin"
