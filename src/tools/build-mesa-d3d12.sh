#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-mesa-d3d12.sh [outdir] [--dry-run] [--build]
# OpenGL for launcher Play (research/OPENGL-PLAN.md): Mesa's Gallium d3d12 driver (OpenGL on D3D12) cross-built for
# Windows x64 with mingw-w64 from the pinned source (`mesa-src` in deps.pin, SHA-256 verified; `directx-headers` for the
# only subproject Mesa needs). Result: opengl32.dll (WGL + GL entry points) and libgallium_wgl.dll (the d3d12 driver it
# imports). Mesa is MIT-licensed; the DLLs are build outputs, git-ignored (out/ and *.dll) and never committed.
# Default: fetch, verify, extract and run `meson setup` (milestone 1: the configuration is reproducible and accepted by Mesa).
# --build also compiles and copies the DLLs to <outdir>. --dry-run only prints the recipe (offline; what CI runs).
# Patches: patches/mesa-src/*.patch (window handling and CoreWindow presentation for a launcher guest; a command-queue
# fallback for D3D12 implementations without CreateCommandQueue1), applied with
# `patch --fuzz=0` after the SHA-256 check. Needs: a UCRT-flavoured x86_64-w64-mingw32-gcc/g++ (the console has no msvcrt.dll; on NixOS pkgsCross.ucrt64), meson, ninja, python3 with mako and pyyaml, bison, flex, curl, xz, sha256sum, git.
# Env: MCFGTHREAD_LIBDIR = directory holding libmcfgthread.a when the toolchain's threading runtime is mcfgthread
# (linked statically: there is no libmcfgthread-2.dll to map); MCFGTHREAD_INCDIR = its headers (mcfgthread/gthr.h,
# which libstdc++'s gthr-default.h includes). The result is checked: only OS DLLs may be imported.
# Cache/scratch dir: third_party/mesa-d3d12-build/<pin hash>/ (git-ignored). Env: JOBS (default nproc).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=; dry=0; build=0
for a in "$@"; do case "$a" in --dry-run) dry=1 ;; --build) build=1 ;; *) out=$a ;; esac; done
out=${out:-$root/out/mesa-d3d12}
patches=("$root"/patches/mesa-src/*.patch)
read -r _ url sha < <(grep -E '^mesa-src[[:space:]]' "$root/deps.pin") || { echo "mesa-src missing from deps.pin" >&2; exit 2; }
read -r _ dxurl dxrev dxtag < <(grep -E '^directx-headers[[:space:]]' "$root/deps.pin") || { echo "directx-headers missing from deps.pin" >&2; exit 2; }
# The d3d12 driver and its WGL target: no LLVM, no Vulkan, no GLES/EGL/GLX, no video, no tests. softpipe (CPU, no LLVM)
# is kept for the off-device test only (GALLIUM_DRIVER=softpipe); the patch makes d3d12 the sole default.
MESON_OPTIONS=(
  -Dplatforms=windows -Dopengl=true -Dgles1=disabled -Dgles2=disabled -Degl=disabled -Dglx=disabled
  -Dgallium-drivers=d3d12,softpipe -Dvulkan-drivers= -Dllvm=disabled
  -Dgallium-d3d12-graphics=enabled -Dgallium-d3d12-video=disabled -Dmicrosoft-clc=disabled
  -Dgallium-va=disabled -Dgallium-mediafoundation=disabled -Dgallium-d3d10umd=false -Dvideo-codecs=
  -Dgallium-rusticl=false -Dzlib=disabled -Dzstd=disabled -Dbuild-tests=false -Dbuildtype=release -Dstrip=true
)
echo "mesa-src url=$url sha256=$sha directx-headers=$dxurl@$dxrev out=$out"
echo "meson options: ${MESON_OPTIONS[*]}"
echo "patches: ${patches[*]##*/}"
echo "outputs: opengl32.dll libgallium_wgl.dll"
(( dry )) && exit 0
for t in x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ meson ninja bison flex curl xz sha256sum git python3; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }
done
python3 -I -c 'import mako, yaml' 2>/dev/null || { echo "missing python modules: mako, pyyaml" >&2; exit 2; }
# The console has no msvcrt.dll, so the toolchain must be UCRT-flavoured (on NixOS pkgsCross.ucrt64). Both flavours
# report the same gcc version and both ship libucrt.a/libmsvcrt.a, so link a trivial program and read its imports.
objdump=${CROSS_OBJDUMP:-x86_64-w64-mingw32-objdump}
probe=$(mktemp -d); trap 'rm -rf "$probe"' EXIT
echo 'int main(void){return 0;}' >"$probe/p.c"
probe_link=(); [[ -n ${MCFGTHREAD_LIBDIR:-} ]] && probe_link=(-L"$MCFGTHREAD_LIBDIR")
x86_64-w64-mingw32-gcc "${probe_link[@]}" "$probe/p.c" -o "$probe/p.exe" 2>"$probe/err" \
  || { cat "$probe/err" >&2; echo "cannot link with x86_64-w64-mingw32-gcc" >&2; exit 2; }
if "$objdump" -p "$probe/p.exe" | grep -qi 'DLL Name: msvcrt\.dll'; then
  echo "x86_64-w64-mingw32-gcc $(command -v x86_64-w64-mingw32-gcc) links msvcrt.dll; the console has none. Use a UCRT-flavoured mingw-w64 (NixOS: pkgsCross.ucrt64.stdenv.cc, and make sure it comes first in PATH)." >&2
  exit 2
fi
base=$root/third_party/mesa-d3d12-build; recipe=$(printf '%s\n' "${MESON_OPTIONS[@]}" "$dxrev" "static-ucrt-1" "$(cat "${patches[@]}" | sha256sum)" "$(readlink -f "$(command -v x86_64-w64-mingw32-gcc)")" "$(x86_64-w64-mingw32-gcc --version | head -1)" "${MCFGTHREAD_LIBDIR:-}" "${MCFGTHREAD_INCDIR:-}" | sha256sum | cut -c1-16)   # options, pins and toolchain, not the script text: packaging edits keep the build cache
work=$base/${sha:0:16}-$recipe
tar=$base/$(basename "$url")
mkdir -p "$base" "$work" "$out"
if [[ ! -f $tar ]]; then curl -fsSL --proto '=https' -o "$tar.part" "$url"; mv "$tar.part" "$tar"; fi
echo "$sha  $tar" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $tar; deleting" >&2; rm -f "$tar"; exit 1; }
src=$work/$(basename "$tar" .tar.xz)
if [[ ! -f $work/.extracted ]]; then   # marker written only after a complete extract; an interrupted one is redone
  rm -rf "$src" "$work/build"; tar -C "$work" -xf "$tar"
  for p in "${patches[@]}"; do patch --batch --fuzz=0 -d "$src" -p1 <"$p" >/dev/null; done
  touch "$work/.extracted"
fi
# The wrap in the verified tarball must still name the tag we pin; nothing else is downloaded by meson.
grep -q "^revision = $dxtag\$" "$src/subprojects/DirectX-Headers.wrap" || { echo "Mesa wrap and deps.pin disagree on DirectX-Headers" >&2; exit 1; }
dx=$src/subprojects/DirectX-Headers-1.0
if [[ ! -d $dx/.git ]]; then
  rm -rf "$dx"; git clone -q --depth 1 --branch "$dxtag" "$dxurl" "$dx"
fi
head=$(git -C "$dx" rev-parse HEAD)
[[ $dxrev =~ ^[0-9a-f]{40}$ && $head == "$dxrev" ]] || { echo "DirectX-Headers $dxtag is $head, deps.pin pins $dxrev" >&2; exit 1; }
cross=$work/mingw-x86_64.cross
link=; extra=
[[ -n ${MCFGTHREAD_LIBDIR:-} ]] && link=", '-L$MCFGTHREAD_LIBDIR'"
[[ -n ${MCFGTHREAD_INCDIR:-} ]] && extra=", '-isystem', '$MCFGTHREAD_INCDIR'"
# libstdc++ built for the mcf threading model includes <mcfgthread/gthr.h> from <thread>; Mesa's u_qsort.cpp uses it.
if [[ -n ${MCFGTHREAD_INCDIR:-} ]]; then
  printf '#include <thread>\nint main(){std::thread t([]{});t.join();}\n' >"$probe/t.cpp"
  x86_64-w64-mingw32-g++ -std=c++17 "${probe_link[@]}" -isystem "$MCFGTHREAD_INCDIR" -c "$probe/t.cpp" -o "$probe/t.o" 2>"$probe/terr" \
    || { cat "$probe/terr" >&2; echo "x86_64-w64-mingw32-g++ cannot compile <thread> with MCFGTHREAD_INCDIR=$MCFGTHREAD_INCDIR" >&2; exit 2; }
fi
cat >"$cross" <<C
[binaries]
c = 'x86_64-w64-mingw32-gcc'
cpp = 'x86_64-w64-mingw32-g++'
ar = 'x86_64-w64-mingw32-ar'
strip = 'x86_64-w64-mingw32-strip'
windres = 'x86_64-w64-mingw32-windres'
[built-in options]
c_args = ['-static-libgcc', '-DSTW_NO_DESKTOP_HOOKS'$extra]
cpp_args = ['-static-libgcc', '-static-libstdc++', '-DSTW_NO_DESKTOP_HOOKS'$extra]
c_link_args = ['-static'$link]
cpp_link_args = ['-static'$link]
[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
C
bld=$work/build
if [[ ! -f $bld/build.ninja ]]; then
  meson setup "$bld" "$src" --cross-file "$cross" --wrap-mode=nodownload "${MESON_OPTIONS[@]}" >"$work/setup.log" 2>&1 \
    || { tail -30 "$work/setup.log" >&2; exit 1; }
fi
echo "ok: configured $bld"
(( build )) || exit 0
ninja -C "$bld" -j"${JOBS:-$(nproc)}" src/gallium/targets/libgl-gdi/opengl32.dll src/gallium/targets/wgl/libgallium_wgl.dll >"$work/build.log" 2>&1 \
  || { tail -30 "$work/build.log" >&2; exit 1; }
# Gates on the result: console-safe imports only, and no section that is both writable and executable.
for f in "$bld/src/gallium/targets/libgl-gdi/opengl32.dll" "$bld/src/gallium/targets/wgl/libgallium_wgl.dll"; do
  bad=$("$objdump" -p "$f" | awk '/DLL Name:/{print tolower($3)}' | grep -Ev '^(kernel32|ntdll|user32|gdi32|shell32|version|advapi32|ws2_32|ole32|libgallium_wgl)\.dll$|^api-ms-win-' || true)
  [[ -z $bad ]] || { echo "$(basename "$f") imports DLLs the console cannot supply: $bad" >&2; exit 1; }
  rwx=$("$objdump" -h "$f" | awk '/^ *[0-9]+ /{n=$2; getline; if ($0 ~ /CODE/ && $0 !~ /READONLY/) print n}')
  [[ -z $rwx ]] || { echo "$(basename "$f") has writable code section(s): $rwx" >&2; exit 1; }
done
rm -f "$out"/*.dll "$out"/SHA256SUMS "$out"/SOURCE.txt "$out"/LICENSE-*
cp "$bld/src/gallium/targets/libgl-gdi/opengl32.dll" "$bld/src/gallium/targets/wgl/libgallium_wgl.dll" "$out/"
# GNU ld leaves a few zero-length unwind entries that Play's loader (rightly) refuses; drop exactly those from our own output.
python3 -I "$root/src/tools/pe-drop-empty-pdata.py" "$out/opengl32.dll" "$out/libgallium_wgl.dll"
# MIT requires the notice to travel with the binaries; the launcher package carries these files next to them.
mkdir -p "$out/patches"; cp "${patches[@]}" "$out/patches/"
cp "$src/docs/license.rst" "$out/LICENSE-Mesa.rst"; cp "$dx/LICENSE" "$out/LICENSE-DirectX-Headers"
( cd "$out" && sha256sum -- *.dll >SHA256SUMS )
cat >"$out/SOURCE.txt" <<S
Built from Mesa source $(basename "$url") (sha256 $sha), MIT, source: $url
DirectX-Headers $dxrev ($head), MIT, $dxurl
Build: $(x86_64-w64-mingw32-gcc --version | head -1); meson options: ${MESON_OPTIONS[*]}
Modified: yes, patches ${patches[*]##*/} (sha256 $(cat "${patches[@]}" | sha256sum | cut -c1-16)); distributed in the launcher package as patches/
Recipe sha256 $recipe
These DLLs are not part of the xboxnative repository; rebuild them with src/tools/build-mesa-d3d12.sh.
S
echo "ok: $out"; cat "$out/SHA256SUMS"
