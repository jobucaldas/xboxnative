#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Read-only prerequisite check for xbox-test/app/build.sh.
set -u
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
toolchain=${LLVM_MINGW_ROOT:-/data/OpenBot/Shared/toolchains/llvm-mingw-20260908-ucrt-ubuntu-22.04-x86_64}
wrapper=${UWP_CROSSBUILD:-}
missing=0
check_tool() {
  local name=$1 override=$2 path
  if [[ -n $override ]]; then path=$override; elif [[ -x $toolchain/bin/$name ]]; then path=$toolchain/bin/$name; else path=$(command -v "$name" 2>/dev/null || true); fi
  if [[ -n $path && -x $path ]]; then printf 'FOUND %s=%s\n' "$name" "$path"; else printf 'MISSING %s\n' "$name"; missing=1; fi
}
check_tool python3 "${PYTHON3_PATH:-}"
check_tool x86_64-w64-mingw32-gcc "${MINGW_GCC_PATH:-}"
check_tool x86_64-w64-mingw32-g++ "${MINGW_GXX_PATH:-}"
check_tool x86_64-w64-mingw32-dlltool "${MINGW_DLLTOOL_PATH:-}"
check_tool lld-link-19 "${LLD_LINK_PATH:-}"
check_tool openssl "${OPENSSL_PATH:-}"
check_tool makemsix "${MAKEMSIX_PATH:-}"
check_tool osslsigncode "${OSSLSIGNCODE_PATH:-}"
# Resolve the selected wrapper as app/build.sh does; its script is the compiler/linker provider.
if [[ -z $wrapper ]]; then
  for candidate in "$root/../uwp-crossbuild" /workspace/ref/uwp-crossbuild; do
    if [[ -x $candidate/scripts/build.sh ]]; then wrapper=$candidate; break; fi
  done
fi
if [[ -n $wrapper && -x $wrapper/scripts/build.sh ]]; then echo "FOUND UWP_CROSSBUILD=$wrapper (scripts/build.sh)"; else echo "MISSING UWP_CROSSBUILD: set UWP_CROSSBUILD to a checkout containing executable scripts/build.sh"; missing=1; fi
echo 'SDK/CRT: the wrapper must validate/use its SDK/CRT inputs at build time; discovered headers or libraries alone do not prove usability or MSVC ABI compatibility.'
if ((missing)); then echo 'RESULT NOT READY: app build/package prerequisites are missing.' >&2; exit 1; fi
echo 'RESULT TOOLS PRESENT ONLY; this does not prove build, package, or Xbox runtime acceptance.'
