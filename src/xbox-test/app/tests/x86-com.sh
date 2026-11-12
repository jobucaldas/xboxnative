#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds and runs the guest COM thunk layer tests (host/x86/x86com.h): guest
# vtable stubs, stdcall pops, struct translators, guest callbacks, refcounts and
# fault-by-name, against a mock host COM object. Deterministic and console-free.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tmp=$(mktemp -d /tmp/x86-com.XXXXXX); trap 'rm -rf "$tmp"' EXIT
"${CXX:-c++}" -std=c++17 -O1 -mno-red-zone -Wall -Wextra -Werror "$here/x86-com.cpp" -o "$tmp/x86-com"
"$tmp/x86-com"
