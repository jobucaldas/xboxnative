#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Fetch the UWP cross-compile toolchain wrapper (MIT) at the pin in deps.pin. Default dest: $UWP_CROSSBUILD or /workspace/ref/uwp-crossbuild.
exec "$(dirname "${BASH_SOURCE[0]}")/fetch-dep.sh" uwp-crossbuild "$@"
