#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-wine-donors.sh [outdir] [--dry-run]
# Builds the Wine "donor" PE DLLs (avrt, dinput8, hid, psapi, setupapi, shcore, version) from the pinned Wine source tarball
# (`wine-src` in deps.pin, SHA-256 verified) with mingw-w64. Only those DLLs and the import libraries they need are built.
# Also builds VC110/VC120 CRT donors for PE32 games into <outdir>/runtime/x86/ and Wine's C++ runtime DLLs
# (msvcp140 family, concrt140) into <outdir>/runtime/: launcher Play maps them for
# games that do not ship their own copy, as an installed VC++ redistributable serves them on Windows.
# d3dcompiler_47 goes into <outdir>/graphics/: the HLSL compiler xgameFinal.exe statically imports (D3DCompile,
# D3DDisassemble, D3DGetBlobPart, D3DReflect) and the d3d10 shim's Compiler::get() loads by name. patches/wine-src/
# 0002 links Wine's bundled libvkd3d.a into it, so it imports only kernel32/ntdll/ucrtbase.
# Wine is LGPL-2.1-or-later. The DLLs are build outputs: they are git-ignored (out/ and *.dll) and must never be committed;
# CI refuses committed DLLs. Output: <outdir>/*.dll, SHA256SUMS, SOURCE.txt, runtime/ and runtime/x86/ (default: out/wine-donors).
# Needs: x86_64-w64-mingw32-gcc, i686-w64-mingw32-gcc, make, flex, bison, curl, xz, sha256sum, patch.
# Cache/scratch dir: third_party/wine-donors-build/<pin hash>/ (git-ignored). Env: JOBS (default nproc).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=; dry=0
for a in "$@"; do case "$a" in --dry-run) dry=1 ;; *) out=$a ;; esac; done
out=${out:-$root/out/wine-donors}
DLLS=(avrt dinput8 hid psapi setupapi shcore version)
GRAPHICS=(d3dcompiler_47)
RUNTIME=(vcruntime140 msvcp140 msvcp140_1 msvcp140_2 msvcp140_atomic_wait msvcp140_codecvt_ids concrt140)
RUNTIME_X86=(msvcr110 msvcp110 msvcr120 msvcp120)
read -r _ url sha < <(grep -E '^wine-src[[:space:]]' "$root/deps.pin") || { echo "wine-src missing from deps.pin" >&2; exit 2; }
patchfile=$root/patches/wine-src/0001-pe-rodata.patch
patch3file=$root/patches/wine-src/0003-donor-critical-section-flags.patch
read -r patchsha _ < <(sha256sum "$patchfile")
read -r patch2sha _ < <(sha256sum "$root/patches/wine-src/0002-d3dcompiler-static-vkd3d.patch")
read -r patch3sha _ < <(sha256sum "$patch3file")
read -r recipe _ < <(sha256sum "$patchfile" "$root/patches/wine-src/0002-d3dcompiler-static-vkd3d.patch" "$patch3file" "${BASH_SOURCE[0]}" | sha256sum)
base=$root/third_party/wine-donors-build; work=$base/${sha:0:16}-${recipe:0:16}   # source and recipe changes invalidate the tree
tar=$work/$(basename "$url")
echo "wine-src url=$url sha256=$sha out=$out dlls=${DLLS[*]} runtime=${RUNTIME[*]} runtime-x86=${RUNTIME_X86[*]}"
(( dry )) && exit 0
for t in x86_64-w64-mingw32-gcc i686-w64-mingw32-gcc make flex bison curl xz sha256sum patch; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }
done
mkdir -p "$work" "$out" "$out/runtime" "$out/runtime/x86" "$out/graphics"
rm -f "$out"/*.dll "$out"/SHA256SUMS "$out"/SOURCE.txt "$out"/runtime/*.dll "$out"/runtime/SHA256SUMS "$out"/runtime/x86/*.dll "$out"/runtime/x86/SHA256SUMS "$out"/graphics/*.dll "$out"/graphics/SHA256SUMS   # never leave DLLs from an older pin next to new labels
if [[ ! -f $tar ]]; then curl -fsSL -o "$tar.part" "$url"; mv "$tar.part" "$tar"; fi
echo "$sha  $tar" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $tar; deleting" >&2; rm -f "$tar"; exit 1; }
src=$work/$(basename "$tar" .tar.xz)
if [[ ! -f $work/.extracted ]]; then   # marker written only after a complete extract; an interrupted one is redone
  rm -rf "$src" "$work/build"; tar -C "$work" -xf "$tar"
  patch --batch --fuzz=0 -d "$src" -p1 < "$patchfile"
  patch --batch --fuzz=0 -d "$src" -p1 < "$root/patches/wine-src/0002-d3dcompiler-static-vkd3d.patch"
  patch --batch --fuzz=0 -d "$src" -p1 < "$patch3file"
  touch "$work/.extracted"
fi
bld=$work/build; mkdir -p "$bld"; cd "$bld"
[[ -f $bld/.configured ]] || { "$src/configure" --enable-win64 --without-x --disable-tests --without-freetype --without-gstreamer \
  --without-pulse --without-alsa --without-oss --without-cups --without-sane --without-usb --without-v4l2 \
  --without-wayland --without-vulkan >configure.log 2>&1 && touch "$bld/.configured"; } || { tail -20 configure.log >&2; exit 1; }
targets=(); for d in "${DLLS[@]}" "${GRAPHICS[@]}" "${RUNTIME[@]}"; do targets+=("dlls/$d/all"); done
make -j"${JOBS:-$(nproc)}" "${targets[@]}" >make.log 2>&1 || { tail -30 make.log >&2; exit 1; }
for d in "${DLLS[@]}"; do cp "dlls/$d/x86_64-windows/$d.dll" "$out/$d.dll"; done
for d in "${GRAPHICS[@]}"; do cp "dlls/$d/x86_64-windows/$d.dll" "$out/graphics/$d.dll"; done
for d in "${RUNTIME[@]}"; do cp "dlls/$d/x86_64-windows/$d.dll" "$out/runtime/$d.dll"; done
# VC110/VC120 are built separately as i386 PE: Wine's configure output cannot
# mix the 32-bit target into the x86_64 build tree.
bld32=$work/build-i386; mkdir -p "$bld32"; cd "$bld32"
[[ -f $bld32/.configured ]] || { CROSSCC=i686-w64-mingw32-gcc "$src/configure" --enable-archs=i386 \
  --without-x --without-wayland --without-curses --without-usb --without-vulkan --without-gstreamer \
  --without-pcap --without-sane --without-osmesa --without-pulse --without-alsa --without-fontconfig \
  --without-freetype --without-dbus --without-udev --without-hal --without-gphoto --without-gnutls \
  --without-krb5 --without-netapi --without-opencl --without-unwind --without-gettext --without-gettextpo \
  >configure.log 2>&1 && touch "$bld32/.configured"; } || { tail -20 configure.log >&2; exit 1; }
targets32=(); for d in "${RUNTIME_X86[@]}"; do targets32+=("dlls/$d/all"); done
make -j"${JOBS:-$(nproc)}" "${targets32[@]}" >make.log 2>&1 || { tail -30 make.log >&2; exit 1; }
cd "$bld"
for d in "${RUNTIME_X86[@]}"; do cp "$bld32/dlls/$d/i386-windows/$d.dll" "$out/runtime/x86/$d.dll"; done
( cd "$out" && sha256sum -- *.dll >SHA256SUMS )
( cd "$out/graphics" && sha256sum -- *.dll >SHA256SUMS )
( cd "$out/runtime" && sha256sum -- *.dll >SHA256SUMS )
( cd "$out/runtime/x86" && sha256sum -- *.dll >SHA256SUMS )
cat >"$out/SOURCE.txt" <<S
Built from Wine source $(basename "$url") (sha256 $sha), LGPL-2.1-or-later, source: $url
Build: $(x86_64-w64-mingw32-gcc --version | head -1); i686: $(i686-w64-mingw32-gcc --version | head -1); configure flags in $bld/configure.log and $bld32/configure.log. Modified: yes, PE constant-data section flags and donor critical-section initialization.
Patch: 0001-pe-rodata.patch sha256 $patchsha
Patch: 0002-d3dcompiler-static-vkd3d.patch sha256 $patch2sha
Patch: 0003-donor-critical-section-flags.patch sha256 $patch3sha
Recipe sha256 $recipe
These DLLs are not part of the xboxnative repository; rebuild them with src/tools/build-wine-donors.sh.
S
cp "$patchfile" "$out/0001-pe-rodata.patch"
cp "$root/patches/wine-src/0002-d3dcompiler-static-vkd3d.patch" "$out/0002-d3dcompiler-static-vkd3d.patch"
cp "$patch3file" "$out/0003-donor-critical-section-flags.patch"
echo "ok: $out"; cat "$out/SHA256SUMS" "$out/graphics/SHA256SUMS" "$out/runtime/SHA256SUMS" "$out/runtime/x86/SHA256SUMS"
