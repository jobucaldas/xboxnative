#!/usr/bin/env bash
# Compile main.cpp with the UWP compiler and SDK surface used by app/build.sh.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
here="$root/src/xbox-test/app"
cb=${UWP_CROSSBUILD:-$root/out/uwp-crossbuild}
xwin=${UWP_XWIN_ROOT:-$HOME/.cache/uwp-crossbuild/xwin}
out=${RUNNER_TEMP:-/tmp}/uwp-launcher-compile
assets="$out/ui-assets"

[[ -x "$cb/scripts/build.sh" ]] || { echo "missing pinned UWP crossbuild at $cb" >&2; exit 1; }
[[ -f "$xwin/crt/include/vcruntime.h" && -f "$xwin/sdk/include/um/windows.h" ]] || {
  echo "missing xwin CRT/SDK headers at $xwin" >&2
  exit 1
}

bash "$root/src/tools/fetch-ui-assets.sh" "$assets"
bash "$root/src/xbox-test/uwph/host/fetch-sqlite.sh"
mkdir -p "$out/obj"
# probe.h includes a build-generated table. One inert row keeps this compile-only
# check independent of the separately built Wine donor DLLs.
printf '{"compile-only", "", "", 0},\n' > "$out/obj/donor_imports.generated.h"

cd "$here"
"${CLANG_CL:-clang-cl}" -target x86_64-pc-windows-msvc /W3 \
  "/FI$cb/include/msvc-compat.h" \
  /imsvc "$xwin/crt/include" \
  /imsvc "$xwin/sdk/include/ucrt" \
  /imsvc "$xwin/sdk/include/um" \
  /imsvc "$xwin/sdk/include/shared" \
  /imsvc "$xwin/sdk/include/winrt" \
  /imsvc "$xwin/sdk/include/cppwinrt" \
  /std:c++20 /EHsc \
  /D__WRL_NO_DEFAULT_LIB__ /DWINAPI_FAMILY=WINAPI_FAMILY_APP \
  -mcx16 /MT /O2 /DNDEBUG \
  "/I$out/obj" "/I$assets" "/I$root/src/xbox-test/uwph/host/third_party/sqlite" \
  /Zs main.cpp
printf '%s\n' 'UWP launcher syntax check PASS: main.cpp'
