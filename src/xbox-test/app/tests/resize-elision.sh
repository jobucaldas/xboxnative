#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Unit tests for the redundant-resize elision decision (host/resize_elide.h):
# same-mode requests elide, real differences forward, BufferCount 0 and
# DXGI_FORMAT_UNKNOWN normalise to "keep", the waitable flag bit is stripped,
# and the resizeelide.txt opt-out parses. Pure functions, no Windows headers:
# the host toolchain runs it directly (like the other offline unit tests).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/resize-elision.cpp -o out/resize-elision-test
out/resize-elision-test
