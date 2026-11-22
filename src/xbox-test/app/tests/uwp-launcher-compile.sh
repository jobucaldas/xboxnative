#!/usr/bin/env bash
# Compile the launcher and off-device runner translation units with the UWP
# compiler and SDK surface used by app/build.sh and tests/play-runtime.sh.
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
# Windows headers are case-insensitive, but xwin's C++/WinRT projection has
# lowercase filenames for some headers. Add temporary aliases for this host's
# case-sensitive filesystem; the symlinks point to the verified SDK files.
cppwinrt="$xwin/sdk/include/cppwinrt/winrt"
[[ -d "$cppwinrt" ]] || { echo "missing C++/WinRT headers at $cppwinrt" >&2; exit 1; }
while IFS= read -r line; do
  case "$line" in
    '#include <winrt/'*'>')
      header=${line#'#include <winrt/'}
      header=${header%>}
      if [[ ! -e "$cppwinrt/$header" && -f "$cppwinrt/${header,,}" ]]; then
        mkdir -p "$out/obj/winrt"
        ln -sf "$cppwinrt/${header,,}" "$out/obj/winrt/$header"
      fi
      ;;
  esac
done < <(cat "$here/main.cpp" "$here/tests/play-runner.cpp" "$here/tests/play-runner-launch.cpp")
# probe.h includes a build-generated table. One inert row keeps this compile-only
# check independent of the separately built Wine donor DLLs.
printf '{"compile-only", "", "", 0},\n' > "$out/obj/donor_imports.generated.h"

cd "$here"
# build.sh compiles main.cpp with /DNDEBUG; play-runtime.sh builds the runner
# without it, so each unit is checked with the flags it is built with.
check_unit() {
  local ndebug=$1 unit=$2
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
    -mcx16 /MT /O2 ${ndebug:+/DNDEBUG} \
    "/I$out/obj" "/I$assets" "/I$root/src/xbox-test/uwph/host/third_party/sqlite" \
    /Zs "$unit"
  printf '%s\n' "UWP launcher syntax check PASS: $unit"
}
check_unit 1 main.cpp
check_unit 1 qrcodegen.cpp
check_unit 1 host/tls_slot0.cpp
check_unit "" tests/play-runner.cpp
check_unit "" tests/play-runner-launch.cpp
