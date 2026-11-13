#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-wine-runtime-i386.sh [outdir] [--dry-run]
# Builds the 32-bit (i386 PE) VC runtime DLLs a PE32 game imports — ucrtbase,
# vcruntime140, msvcp140, concrt140 — from the pinned Wine source tarball
# (`wine-src` in deps.pin, SHA-256 verified) with the i686 mingw-w64 cross
# compiler. The api-ms-win-crt-* import names resolve to ucrtbase through
# Wine's own apisetschema mapping (see research/X86-STAGE3-COVERAGE.md).
# Wine is LGPL-2.1-or-later. The DLLs are build outputs: they are git-ignored
# (out/ and *.dll) and must never be committed; CI refuses committed DLLs.
# Output: <outdir>/*.dll, SHA256SUMS, SOURCE.txt (default: out/wine-runtime-i386).
# Needs: i686-w64-mingw32-gcc, make, flex, bison, curl, xz, sha256sum, patch.
# Cache/scratch dir: third_party/wine-runtime-i386-build/<pin hash>/ (git-ignored).
# Env: JOBS (default nproc).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=; dry=0
for a in "$@"; do case "$a" in --dry-run) dry=1 ;; *) out=$a ;; esac; done
out=${out:-$root/out/wine-runtime-i386}
DLLS=(ucrtbase vcruntime140 msvcp140 concrt140)
read -r _ url sha < <(grep -E '^wine-src[[:space:]]' "$root/deps.pin") || { echo "wine-src missing from deps.pin" >&2; exit 2; }
read -r patchsha _ < <(sha256sum "$root/patches/wine-src/0001-pe-rodata.patch")
read -r recipe _ < <(sha256sum "$root/patches/wine-src/0001-pe-rodata.patch" "${BASH_SOURCE[0]}" | sha256sum)
base=$root/third_party/wine-runtime-i386-build; work=$base/${sha:0:16}-${recipe:0:16}
tar=$work/$(basename "$url")
echo "wine-src url=$url sha256=$sha out=$out dlls=${DLLS[*]}"
(( dry )) && exit 0
for t in i686-w64-mingw32-gcc make flex bison curl xz sha256sum patch; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }
done
mkdir -p "$work" "$out"
rm -f "$out"/*.dll "$out"/SHA256SUMS "$out"/SOURCE.txt   # never leave DLLs from an older pin next to new labels
if [[ ! -f $tar ]]; then curl -fsSL -o "$tar.part" "$url"; mv "$tar.part" "$tar"; fi
echo "$sha  $tar" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $tar; deleting" >&2; rm -f "$tar"; exit 1; }
src=$work/$(basename "$tar" .tar.xz)
if [[ ! -f $work/.extracted ]]; then   # marker written only after a complete extract; an interrupted one is redone
  rm -rf "$src" "$work/build"; tar -C "$work" -xf "$tar"
  patch --batch --fuzz=0 -d "$src" -p1 < "$root/patches/wine-src/0001-pe-rodata.patch"
  touch "$work/.extracted"
fi
bld=$work/build; mkdir -p "$bld"; cd "$bld"
[[ -f $bld/.configured ]] || { CROSSCC=i686-w64-mingw32-gcc "$src/configure" --enable-archs=i386 \
  --without-x --without-wayland --without-curses --without-usb --without-vulkan --without-gstreamer \
  --without-pcap --without-sane --without-osmesa --without-pulse --without-alsa --without-fontconfig \
  --without-freetype --without-dbus --without-udev --without-hal --without-gphoto --without-gnutls \
  --without-krb5 --without-netapi --without-opencl --without-unwind --without-gettext --without-gettextpo \
  >configure.log 2>&1 && touch "$bld/.configured"; } || { tail -20 configure.log >&2; exit 1; }
targets=(); for d in "${DLLS[@]}"; do targets+=("dlls/$d/all"); done
make -j"${JOBS:-$(nproc)}" "${targets[@]}" >make.log 2>&1 || { tail -30 make.log >&2; exit 1; }
for d in "${DLLS[@]}"; do cp "dlls/$d/i386-windows/$d.dll" "$out/$d.dll"; done
( cd "$out" && sha256sum -- *.dll >SHA256SUMS )
cat >"$out/SOURCE.txt" <<S
Built from Wine source $(basename "$url") (sha256 $sha), LGPL-2.1-or-later, source: $url
Build: $(i686-w64-mingw32-gcc --version | head -1); configure flags in $bld/configure.log. Modified: yes, PE constant-data section flags.
Patch: 0001-pe-rodata.patch sha256 $patchsha
Recipe sha256 $recipe
These DLLs are not part of the xboxnative repository; rebuild them with src/tools/build-wine-runtime-i386.sh.
S
cp "$root/patches/wine-src/0001-pe-rodata.patch" "$out/0001-pe-rodata.patch"
echo "ok: $out"; cat "$out/SHA256SUMS"
