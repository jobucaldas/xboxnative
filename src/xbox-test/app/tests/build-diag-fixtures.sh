#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-diag-fixtures.sh OUTDIR -> OUTDIR/{fixture-crt.exe,play-vulkan-dzn.exe,
# play-d3d9.exe,play-d3d9-triangle.exe,play-d3d9-triangle-variants.exe,
# play-d3d9-textured.exe,play-d3d10.exe,play-d3d11.exe,play-d3d12.exe,
# play-opengl.exe} - the console self-test's fixtures (selftest_plan.h).
#
# These are the same guest programs tests/play-runtime.sh builds for the
# off-device runner, compiled from this repository's sources so the package
# carries no third-party binary. Each is a few KB. The 32-bit one is built with
# the i686 mingw CRT, as x86-guest.sh builds it; the 64-bit ones link
# /nodefaultlib and import only what Play routes.
#
# Toolchain: the mingw import libraries are found through the compiler itself
# (`-print-file-name`), never a hardcoded prefix; the versioned llvm tools fall
# back to their unversioned names, as the shim builds do.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?output directory required}"
mkdir -p "$out"
work=$(mktemp -d "${TMPDIR:-/tmp}/diag-fixtures.XXXXXX")
trap 'rm -rf "$work"' EXIT

cc64="${MINGW64_CC:-$(command -v x86_64-w64-mingw32-g++ || true)}"
[[ -n $cc64 ]] || { echo "build-diag-fixtures: no x86_64-w64-mingw32-g++" >&2; exit 1; }
mingw_lib=$(dirname "$("$cc64" -print-file-name=libkernel32.a)")
lld() { lld-link-19 "$@" 2>/dev/null || lld-link "$@"; }
clangxx() { clang++-19 "$@" 2>/dev/null || clang++ "$@"; }

# The 64-bit fixtures: no CRT, no exceptions, but WITH unwind tables. Play maps
# these into the launcher and runs them on the guest thread, so a fault inside
# one has to be unwindable to that thread's __try/__except or the launcher dies
# with no crash record (see src/xbox-test/uwph/d3d9/build.sh). A fixture that
# dies silently would hide the very thing the self-test is there to show.
fixture_flags=(-O1 -fno-stack-protector -fno-exceptions -fno-rtti)
common=(/nodefaultlib /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat)
libs=("$mingw_lib/libkernel32.a" "$mingw_lib/libuser32.a")

clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -fno-builtin -c "$here/play-vulkan-dzn.cpp" -o "$work/vulkan-dzn.obj"
lld "${common[@]}" /entry:vk_dzn_entry /include:vk_relocation /out:"$out/play-vulkan-dzn.exe" \
  "$work/vulkan-dzn.obj" "${libs[@]}"

# The draw fixture (research/VULKAN-COVERAGE.md): pipeline + draw + compute +
# push constants + timeline semaphore + synchronization2 + dynamic rendering,
# with pixel and buffer readbacks. The SPIR-V is checked in beside the sources
# (tests/spirv, regenerated with embed-spirv.py) and embedded through
# tests/vulkan_spirv.h.
clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -fno-builtin -c "$here/play-vulkan-draw.cpp" -o "$work/vulkan-draw.obj"
lld "${common[@]}" /entry:vk_draw_entry /include:vk_draw_relocation /out:"$out/play-vulkan-draw.exe" \
  "$work/vulkan-draw.obj" "${libs[@]}"

clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -c "$here/play-d3d9.cpp" -o "$work/d3d9.obj"
lld "${common[@]}" /entry:d3d9_entry /include:d3d9_relocation /out:"$out/play-d3d9.exe" \
  "$work/d3d9.obj" "$mingw_lib/libd3d9.a" "${libs[@]}"

clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -c "$here/play-d3d9-triangle.cpp" -o "$work/d3d9-triangle.obj"
lld "${common[@]}" /entry:d3d9_triangle_entry /include:d3d9_triangle_relocation /out:"$out/play-d3d9-triangle.exe" \
  "$work/d3d9-triangle.obj" "$mingw_lib/libd3d9.a" "${libs[@]}"

# The triangle drawn several ways (the dxilconv sampler-fault variants): one
# guest, the variants ordered so the crash-shaped one runs first and every
# later one's SELFTEST RESULT line is already out if it still faults.
clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -c "$here/play-d3d9-triangle-variants.cpp" -o "$work/d3d9-triangle-variants.obj"
lld "${common[@]}" /entry:d3d9_triangle_variants_entry /include:d3d9_triangle_variants_relocation /out:"$out/play-d3d9-triangle-variants.exe" \
  "$work/d3d9-triangle-variants.obj" "$mingw_lib/libd3d9.a" "${libs[@]}"

clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -c "$here/play-d3d9-textured.cpp" -o "$work/d3d9-textured.obj"
lld "${common[@]}" /entry:d3d9_textured_entry /include:d3d9_textured_relocation /out:"$out/play-d3d9-textured.exe" \
  "$work/d3d9-textured.obj" "$mingw_lib/libd3d9.a" "${libs[@]}"

clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -c "$here/play-d3d10.cpp" -o "$work/d3d10.obj"
lld "${common[@]}" /entry:d3d10_entry /include:d3d10_relocation /out:"$out/play-d3d10.exe" \
  "$work/d3d10.obj" "$mingw_lib/libd3d10.a" "$mingw_lib/libdxgi.a" "$mingw_lib/libuuid.a" "${libs[@]}"

# The OpenGL fixture runs the startup path an SDL3 GL game takes (research/
# OPENGL-PLAN.md): opengl32.dll from the runtime directory (Mesa's d3d12 WGL
# driver), a pixel format through gdi32's forwarded wgl*, a context, a cleared
# frame presented with SwapBuffers and a glReadPixels readback. Exit 42 only
# when the rendered pixel came back; 43 is BLOCKED (never a pass).
clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -c "$here/play-opengl.cpp" -o "$work/opengl.obj"
lld "${common[@]}" /entry:opengl_entry /include:opengl_relocation /out:"$out/play-opengl.exe" \
  "$work/opengl.obj" "${libs[@]}"

# The D3D11 fixture draws with the committed DXBC and reads the pixels back; it
# statically imports d3d11.dll, which the console has natively and Play routes.
# The include path carries the shaders beside the fixture's sources.
clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -I"$here" -c "$here/play-d3d11.cpp" -o "$work/d3d11.obj"
lld "${common[@]}" /entry:d3d_entry /include:d3d_relocation /out:"$out/play-d3d11.exe" \
  "$work/d3d11.obj" "$mingw_lib/libd3d11.a" "$mingw_lib/libdxgi.a" "$mingw_lib/libdxguid.a" "${libs[@]}"

# The D3D12 fixture draws the same triangle through a minimal PSO and reads it
# back; it statically imports d3d12.dll, which the console has natively.
clangxx --target=x86_64-w64-windows-gnu "${fixture_flags[@]}" -I"$here" -c "$here/play-d3d12.cpp" -o "$work/d3d12.obj"
lld "${common[@]}" /entry:d3d12_entry /include:d3d12_relocation /out:"$out/play-d3d12.exe" \
  "$work/d3d12.obj" "$mingw_lib/libd3d12.a" "$mingw_lib/libdxgi.a" "$mingw_lib/libdxguid.a" "${libs[@]}"

# The 32-bit fixture: a mingw-CRT PE32 program the interpreter runs to exit 42
# (x86-guest.sh asserts the same binary's stdout and exit code under Wine).
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then
  # NixOS: the i686 mingw gcc wrapper lives in the store but is not on PATH.
  cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true)
fi
[[ -n $cc32 ]] || { echo "build-diag-fixtures: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
extra=()
for d in /nix/store/*-mcfgthread-i686-w64-mingw32-*/lib; do [[ -d $d ]] && extra+=("-L$d"); done
rc32="$(dirname "$cc32")/i686-w64-mingw32-windres"
[[ -x $rc32 ]] || { echo "build-diag-fixtures: no i686-w64-mingw32-windres" >&2; exit 1; }
# The .rc is preprocessed, and the wrapper's preprocessor is the cross gcc.
PATH="$(dirname "$cc32"):$PATH" "$rc32" -O coff -I "$here/x86-guest" -i "$here/x86-guest/fixture-crt.rc" -o "$work/fixture-crt.res"
"$cc32" -O1 -Wall -Wextra -Werror "${extra[@]}" -o "$out/fixture-crt.exe" "$here/x86-guest/fixture-crt.c" "$work/fixture-crt.res"

# A fixture that does not fit the package's budget is a build failure, not a
# surprise on the console.
for f in "$out"/*.exe; do
  size=$(stat -c%s "$f")
  [[ $size -lt 5242880 ]] || { echo "build-diag-fixtures: $(basename "$f") is $size bytes (limit 5 MB)" >&2; exit 1; }
done
# A 64-bit fixture with no exception directory could not be unwound out of if it
# faults, so the launcher would die with no crash record and the self-test would
# report nothing at all. Gate the images the guest thread runs.
for f in "$out"/play-*.exe; do
  python3 -I "$here/../../uwph/d3d9/check-unwind-data.py" "$f"
done
ls -l "$out"
