#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-mesa-d3d12.sh [outdir] [--dry-run] [--build]
# OpenGL for launcher Play (research/OPENGL-PLAN.md): Mesa's Gallium d3d12 driver (OpenGL on D3D12) cross-built for
# Windows x64 with mingw-w64 from the pinned source (`mesa-src` in deps.pin, SHA-256 verified; `directx-headers` for the
# only subproject Mesa needs). Result: opengl32.dll (WGL + GL entry points) and libgallium_wgl.dll (the d3d12 driver it
# imports). Mesa is MIT-licensed; the DLLs are build outputs, git-ignored (out/ and *.dll) and never committed.
# Default: fetch, verify, extract and run `meson setup` (milestone 1: the configuration is reproducible and accepted by Mesa).
# --build also compiles and copies the DLLs to <outdir>. --dry-run only prints the recipe (offline; what CI runs).
# Needs: x86_64-w64-mingw32-gcc/g++, meson, ninja, python3 with mako and pyyaml, bison, flex, curl, xz, sha256sum, git.
# Cache/scratch dir: third_party/mesa-d3d12-build/<pin hash>/ (git-ignored). Env: JOBS (default nproc).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=; dry=0; build=0
for a in "$@"; do case "$a" in --dry-run) dry=1 ;; --build) build=1 ;; *) out=$a ;; esac; done
out=${out:-$root/out/mesa-d3d12}
read -r _ url sha < <(grep -E '^mesa-src[[:space:]]' "$root/deps.pin") || { echo "mesa-src missing from deps.pin" >&2; exit 2; }
read -r _ dxurl dxrev < <(grep -E '^directx-headers[[:space:]]' "$root/deps.pin") || { echo "directx-headers missing from deps.pin" >&2; exit 2; }
# Only the d3d12 driver and its WGL target: no LLVM, no Vulkan, no GLES/EGL/GLX, no video, no tests, no software rasterizer
# beyond what Mesa's WGL target always links (softpipe is built in; it needs no LLVM).
MESON_OPTIONS=(
  -Dplatforms=windows -Dopengl=true -Dgles1=disabled -Dgles2=disabled -Degl=disabled -Dglx=disabled
  -Dgallium-drivers=d3d12,softpipe -Dvulkan-drivers= -Dllvm=disabled
  -Dgallium-d3d12-graphics=enabled -Dgallium-d3d12-video=disabled -Dmicrosoft-clc=disabled
  -Dgallium-va=disabled -Dgallium-mediafoundation=disabled -Dgallium-d3d10umd=false -Dvideo-codecs=
  -Dgallium-rusticl=false -Dzstd=disabled -Dbuild-tests=false -Dbuildtype=release -Dstrip=true
)
echo "mesa-src url=$url sha256=$sha directx-headers=$dxurl@$dxrev out=$out"
echo "meson options: ${MESON_OPTIONS[*]}"
echo "outputs: opengl32.dll libgallium_wgl.dll"
(( dry )) && exit 0
for t in x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ meson ninja bison flex curl xz sha256sum git python3; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }
done
python3 -I -c 'import mako, yaml' 2>/dev/null || { echo "missing python modules: mako, pyyaml" >&2; exit 2; }
base=$root/third_party/mesa-d3d12-build; recipe=$(sha256sum "${BASH_SOURCE[0]}" | cut -c1-16)
work=$base/${sha:0:16}-$recipe
tar=$base/$(basename "$url")
mkdir -p "$base" "$work" "$out"
if [[ ! -f $tar ]]; then curl -fsSL --proto '=https' -o "$tar.part" "$url"; mv "$tar.part" "$tar"; fi
echo "$sha  $tar" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $tar; deleting" >&2; rm -f "$tar"; exit 1; }
src=$work/$(basename "$tar" .tar.xz)
if [[ ! -f $work/.extracted ]]; then   # marker written only after a complete extract; an interrupted one is redone
  rm -rf "$src" "$work/build"; tar -C "$work" -xf "$tar"
  touch "$work/.extracted"
fi
# The wrap in the verified tarball must still name the tag we pin; nothing else is downloaded by meson.
grep -q "^revision = $dxrev\$" "$src/subprojects/DirectX-Headers.wrap" || { echo "Mesa wrap and deps.pin disagree on DirectX-Headers" >&2; exit 1; }
dx=$src/subprojects/DirectX-Headers-1.0
if [[ ! -d $dx/.git ]]; then
  rm -rf "$dx"; git clone -q --depth 1 --branch "$dxrev" "$dxurl" "$dx"
fi
head=$(git -C "$dx" rev-parse HEAD)
if [[ $dxrev =~ ^[0-9a-f]{40}$ ]]; then [[ $head == "$dxrev" ]] || { echo "DirectX-Headers commit mismatch" >&2; exit 1; }
else echo "note: DirectX-Headers $dxrev resolved to $head (record it in deps.pin)"; fi
cross=$work/mingw-x86_64.cross
cat >"$cross" <<C
[binaries]
c = 'x86_64-w64-mingw32-gcc'
cpp = 'x86_64-w64-mingw32-g++'
ar = 'x86_64-w64-mingw32-ar'
strip = 'x86_64-w64-mingw32-strip'
windres = 'x86_64-w64-mingw32-windres'
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
rm -f "$out"/*.dll "$out"/SHA256SUMS "$out"/SOURCE.txt
cp "$bld/src/gallium/targets/libgl-gdi/opengl32.dll" "$bld/src/gallium/targets/wgl/libgallium_wgl.dll" "$out/"
( cd "$out" && sha256sum -- *.dll >SHA256SUMS )
cat >"$out/SOURCE.txt" <<S
Built from Mesa source $(basename "$url") (sha256 $sha), MIT, source: $url
DirectX-Headers $dxrev ($head), MIT, $dxurl
Build: $(x86_64-w64-mingw32-gcc --version | head -1); meson options: ${MESON_OPTIONS[*]}
Recipe sha256 $recipe
These DLLs are not part of the xboxnative repository; rebuild them with src/tools/build-mesa-d3d12.sh.
S
echo "ok: $out"; cat "$out/SHA256SUMS"
