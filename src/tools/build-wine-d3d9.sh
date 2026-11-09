#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-wine-d3d9.sh [outdir] [--dry-run]
# Builds Wine's D3D9 implementation — d3d9.dll (the D3D9 API) and wined3d.dll (the renderer under it) — from the
# pinned Wine source tarball (`wine-src` in deps.pin, SHA-256 verified) with mingw-w64. Play maps both under those
# names for a guest that imports d3d9.dll, and wined3d renders through the Mesa GL driver Play already ships as
# opengl32.dll + libgallium_wgl.dll (research/D3D9-PLAN.md, option A). Only these two DLLs are built.
# Wine is LGPL-2.1-or-later. The DLLs are build outputs: they are git-ignored (out/ and *.dll) and must never be
# committed; pr-lane.sh refuses DLLs. Output: <outdir>/d3d9.dll, <outdir>/wined3d.dll, SHA256SUMS, SOURCE.txt
# (default: out/wine-d3d9).
# Needs: x86_64-w64-mingw32-gcc, make, flex, bison, curl, xz, sha256sum, patch.
# Cache/scratch dir: third_party/wine-d3d9-build/<pin hash>/ (git-ignored). Env: JOBS (default nproc).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=; dry=0
for a in "$@"; do case "$a" in --dry-run) dry=1 ;; *) out=$a ;; esac; done
out=${out:-$root/out/wine-d3d9}
DLLS=(d3d9 wined3d)
read -r _ url sha < <(grep -E '^wine-src[[:space:]]' "$root/deps.pin") || { echo "wine-src missing from deps.pin" >&2; exit 2; }
patchfile=$root/patches/wine-src/0001-pe-rodata.patch
read -r patchsha _ < <(sha256sum "$patchfile")
read -r recipe _ < <(sha256sum "$patchfile" "${BASH_SOURCE[0]}" | sha256sum)
base=$root/third_party/wine-d3d9-build; work=$base/${sha:0:16}-${recipe:0:16}   # source and recipe changes invalidate the tree
tar=$work/$(basename "$url")
echo "wine-src url=$url sha256=$sha out=$out dlls=${DLLS[*]}"
(( dry )) && exit 0
for t in x86_64-w64-mingw32-gcc make flex bison curl xz sha256sum patch; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }
done
mkdir -p "$work" "$out"
rm -f "$out"/*.dll "$out"/SHA256SUMS "$out"/SOURCE.txt   # never leave DLLs from an older pin next to new labels
if [[ ! -f $tar ]]; then curl -fsSL -o "$tar.part" "$url"; mv "$tar.part" "$tar"; fi
echo "$sha  $tar" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $tar; deleting" >&2; rm -f "$tar"; exit 1; }
src=$work/$(basename "$tar" .tar.xz)
if [[ ! -f $work/.extracted ]]; then   # marker written only after a complete extract; an interrupted one is redone
  rm -rf "$src" "$work/build"; tar -C "$work" -xf "$tar"
  patch --batch --fuzz=0 -d "$src" -p1 < "$patchfile"
  touch "$work/.extracted"
fi
bld=$work/build; mkdir -p "$bld"; cd "$bld"
[[ -f $bld/.configured ]] || { "$src/configure" --enable-win64 --without-x --disable-tests --without-freetype --without-gstreamer \
  --without-pulse --without-alsa --without-oss --without-cups --without-sane --without-usb --without-v4l2 \
  --without-wayland --without-vulkan >configure.log 2>&1 && touch "$bld/.configured"; } || { tail -20 configure.log >&2; exit 1; }
targets=(); for d in "${DLLS[@]}"; do targets+=("dlls/$d/all"); done
make -j"${JOBS:-$(nproc)}" "${targets[@]}" >make.log 2>&1 || { tail -30 make.log >&2; exit 1; }
for d in "${DLLS[@]}"; do cp "dlls/$d/x86_64-windows/$d.dll" "$out/$d.dll"; done
# The images are only usable if Play can route everything they import. d3d9.dll imports wined3d.dll (Play maps it
# beside d3d9.dll); wined3d imports opengl32.dll (Mesa, shipped as a redistributable) and the Win32 layers Play
# replaces. Anything else — a host-only import — would fail at map time on the console.
bad=$(for d in "${DLLS[@]}"; do llvm-readobj --coff-imports "$out/$d.dll"; done \
      | awk '/^  Name: /{print tolower($2)}' | sort -u \
      | grep -Ev '^(kernel32|ntdll|ucrtbase|advapi32|gdi32|user32|opengl32|wined3d)\.dll$' || true)
[[ -z $bad ]] || { echo "Wine D3D9 DLLs import DLLs Play does not route: $bad" >&2; exit 1; }
rwx=$(for d in "${DLLS[@]}"; do llvm-readobj --sections "$out/$d.dll"; done \
      | awk '/Name: \./{n=$2;w=0;x=0} /IMAGE_SCN_MEM_WRITE/{w=1} /IMAGE_SCN_MEM_EXECUTE/{x=1} \
             /^ *\]$/{if(w&&x)print n}')
[[ -z $rwx ]] || { echo "Wine D3D9 DLLs have writable code section(s): $rwx" >&2; exit 1; }
# Read each export table once into a variable: piping a large table into
# `grep -q` makes grep exit early, llvm-readobj die of SIGPIPE, and pipefail
# report the pipeline as failed even when the export is there.
d3d9_exports=$(llvm-readobj --coff-exports "$out/d3d9.dll")
wined3d_exports=$(llvm-readobj --coff-exports "$out/wined3d.dll")
for name in Direct3DCreate9 Direct3DCreate9Ex; do
    grep -q "Name: $name\$" <<<"$d3d9_exports" \
        || { echo "d3d9.dll does not export $name" >&2; exit 1; }
done
grep -q "Name: wined3d_create\$" <<<"$wined3d_exports" \
    || { echo "wined3d.dll does not export wined3d_create" >&2; exit 1; }
( cd "$out" && sha256sum -- *.dll >SHA256SUMS )
cat >"$out/SOURCE.txt" <<S
Built from Wine source $(basename "$url") (sha256 $sha), LGPL-2.1-or-later, source: $url
Build: $(x86_64-w64-mingw32-gcc --version | head -1); configure flags in $bld/configure.log. Modified: yes, PE constant-data section flags.
Patch: 0001-pe-rodata.patch sha256 $patchsha
Recipe sha256 $recipe
These DLLs are not part of the xboxnative repository; rebuild them with src/tools/build-wine-d3d9.sh.
S
cp "$patchfile" "$out/0001-pe-rodata.patch"
echo "ok: $out"; cat "$out/SHA256SUMS"
