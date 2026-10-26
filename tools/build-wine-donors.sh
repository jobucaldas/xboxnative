#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-wine-donors.sh [outdir] [--dry-run]
# Builds the Wine "donor" PE DLLs (avrt, hid, setupapi, shcore, version) from the pinned Wine source tarball
# (`wine-src` in deps.pin, SHA-256 verified) with mingw-w64. Only those DLLs and the import libraries they need are built.
# Wine is LGPL-2.1-or-later. The DLLs are build outputs: they are git-ignored (out/ and *.dll) and must never be committed;
# pr-lane.sh refuses DLLs. Output: <outdir>/*.dll, SHA256SUMS, SOURCE.txt (default: out/wine-donors).
# Needs: x86_64-w64-mingw32-gcc, make, flex, bison, curl, xz, sha256sum (Debian: apt install gcc-mingw-w64-x86-64 flex bison make curl xz-utils).
# Cache/scratch dir: third_party/wine-donors-build (git-ignored). Env: JOBS (default nproc).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out=; dry=0
for a in "$@"; do case "$a" in --dry-run) dry=1 ;; *) out=$a ;; esac; done
out=${out:-$root/out/wine-donors}
DLLS=(avrt hid setupapi shcore version)
read -r _ url sha < <(grep -E '^wine-src[[:space:]]' "$root/deps.pin") || { echo "wine-src missing from deps.pin" >&2; exit 2; }
work=$root/third_party/wine-donors-build; tar=$work/$(basename "$url")
echo "wine-src url=$url sha256=$sha out=$out dlls=${DLLS[*]}"
(( dry )) && exit 0
for t in x86_64-w64-mingw32-gcc make flex bison curl xz sha256sum; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }
done
mkdir -p "$work" "$out"
if [[ ! -f $tar ]]; then curl -fsSL -o "$tar.part" "$url"; mv "$tar.part" "$tar"; fi
echo "$sha  $tar" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $tar; deleting" >&2; rm -f "$tar"; exit 1; }
src=$work/$(basename "$tar" .tar.xz)
[[ -d $src ]] || tar -C "$work" -xf "$tar"
bld=$work/build; mkdir -p "$bld"; cd "$bld"
[[ -f Makefile ]] || "$src/configure" --enable-win64 --without-x --disable-tests --without-freetype --without-gstreamer \
  --without-pulse --without-alsa --without-oss --without-cups --without-sane --without-usb --without-v4l2 \
  --without-wayland --without-vulkan >configure.log 2>&1 || { tail -20 configure.log >&2; exit 1; }
targets=(); for d in "${DLLS[@]}"; do targets+=("dlls/$d/all"); done
make -j"${JOBS:-$(nproc)}" "${targets[@]}" >make.log 2>&1 || { tail -30 make.log >&2; exit 1; }
for d in "${DLLS[@]}"; do cp "dlls/$d/x86_64-windows/$d.dll" "$out/$d.dll"; done
( cd "$out" && sha256sum -- *.dll >SHA256SUMS )
cat >"$out/SOURCE.txt" <<S
Built from Wine source $(basename "$url") (sha256 $sha), LGPL-2.1-or-later, source: $url
Build: $(x86_64-w64-mingw32-gcc --version | head -1); configure flags in $bld/configure.log. Modified: no.
These DLLs are not part of the xboxnative repository; rebuild them with tools/build-wine-donors.sh.
S
echo "ok: $out"; cat "$out/SHA256SUMS"
