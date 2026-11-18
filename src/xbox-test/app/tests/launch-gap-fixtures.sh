#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Test the oracle's launch GAP lines against the x64/PE32 console fixtures.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?fixture directory required}"
[[ -f "$out/play-d3d11.exe" ]] || { echo "launch-gap-fixtures: missing x64 fixture" >&2; exit 1; }
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then
  cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true)
fi
[[ -n $cc32 ]] || { echo "launch-gap-fixtures: no i686-w64-mingw32-gcc" >&2; exit 1; }
printf 'LIBRARY xinput1_3.dll\nEXPORTS\n XInputGetState\n' > "$out/xinput.def"
printf 'LIBRARY api-ms-win-crt-heap-l1-1-0.dll\nEXPORTS\n _aligned_free\n' > "$out/crtheap.def"
i686-w64-mingw32-dlltool -m i386 -d "$out/xinput.def" -l "$out/libxinput.a"
i686-w64-mingw32-dlltool -m i386 -d "$out/crtheap.def" -l "$out/libcrtheap.a"
"$cc32" -O1 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables \
  -mno-sse -mno-mmx -mno-80387 -nostdlib -Wl,--entry,_gap_start@0 -Wl,--subsystem,console -Wl,--dynamicbase \
  -Wl,--no-seh -Wl,--image-base,0x400000 -o "$out/gap.exe" "$here/x86-fixture/gap.c" \
  -L"$out" -lxinput -lcrtheap -lkernel32
GAP_FIXTURE_DIR="$out" python3 -I "$here/../../../tools/xbox/tests/test_gap_lines.py"
"${CXX:-c++}" -std=c++17 -O1 -Wall -Wextra \
  -o "$out/wine-gap-x86" "$here/wine-gap-x86.cpp"
"$out/wine-gap-x86" "$out" --assert-fixture
