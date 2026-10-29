#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Read-only readiness report for the Linux UWP/MSIX build prerequisites.
set -u
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
toolchain=${LLVM_MINGW_ROOT:-/data/OpenBot/Shared/toolchains/llvm-mingw-20260908-ucrt-ubuntu-22.04-x86_64}
wrapper=${UWP_CROSSBUILD:-/data/OpenBot/Agents/agent-498d2a81-ed9d-417c-9a4a-578282173ddc/uwp-crossbuild}
missing=0
check_tool() {
  local name=$1 override=${2:-} path
  if [[ -n $override ]]; then path=$override; elif [[ -x $toolchain/bin/$name ]]; then path=$toolchain/bin/$name; else path=$(command -v "$name" 2>/dev/null || true); fi
  if [[ -n $path && -x $path ]]; then printf 'FOUND %s=%s\n' "$name" "$path"; "$path" --version 2>&1 | head -n 1 | sed 's/^/  version: /'; else printf 'MISSING %s (set %s_PATH or install it)\n' "$name" "${name^^}"; missing=1; fi
}
check_tool python3 "${PYTHON3_PATH:-}"
check_tool clang "${CLANG_PATH:-}"
check_tool clang-cl "${CLANG_CL_PATH:-}"
check_tool makemsix "${MAKEMSIX_PATH:-}"
check_tool osslsigncode "${OSSLSIGNCODE_PATH:-}"
printf 'WRAPPER %s: ' "$wrapper"
if [[ -f $wrapper/CMakeLists.txt ]]; then echo present; else echo missing; missing=1; fi
echo 'SDK/CRT: LLVM-MinGW headers/libs are MinGW ABI and do not establish MSVC SDK/CRT compatibility.'
sdk=${UWP_SDK_ROOT:-/data/OpenBot/Agents/agent-498d2a81-ed9d-417c-9a4a-578282173ddc/toolchain-local/xwin/sdk}
crt=${UWP_CRT_ROOT:-/data/OpenBot/Agents/agent-498d2a81-ed9d-417c-9a4a-578282173ddc/toolchain-local/xwin/crt}
for item in "$sdk/include/um/windows.h" "$sdk/lib/um/x86_64/kernel32.lib" "$crt/lib/x86_64/libcmt.lib"; do if [[ -f $item ]]; then echo "FOUND $item"; else echo "MISSING $item (set UWP_SDK_ROOT/UWP_CRT_ROOT to verified roots)"; missing=1; fi; done
if ((missing)); then echo 'RESULT NOT READY: resolve listed prerequisites, then rerun.' >&2; exit 1; fi
echo 'RESULT PREREQUISITES PRESENT; this does not prove package or runtime acceptance.'
