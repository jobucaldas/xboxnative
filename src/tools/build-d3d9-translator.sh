#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-d3d9-translator.sh [outdir] [--dry-run]
# Builds d3d9translator.dll: DXUP's dx9asm (zlib) turning D3D9 shader model 1-3
# bytecode into SM5 DXBC, which is what the own D3D9->D3D11 shim
# (src/xbox-test/uwph/d3d9) needs before it can draw (research/D3D9-PLAN.md).
#
# The DLL is a build output: it is git-ignored and must never be committed. Its
# only import is kernel32.dll and its only export is d3d9Translate, because Play
# maps it inside the launcher's process: no CRT, no stdio, no threads. The
# donor's own config/log modules read an ini file and format with stdio, so this
# script supplies the two of them from src/tools/d3d9-translator/ instead.
#
# Output: <outdir>/d3d9translator.dll, SHA256SUMS, SOURCE.txt (default:
# out/d3d9-translator). Needs: x86_64-w64-mingw32-g++, lld-link, curl, tar,
# sha256sum, patch.
# Cache/scratch dir: third_party/dxup-build/<pin hash>/ (git-ignored).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=; dry=0
for a in "$@"; do case "$a" in --dry-run) dry=1 ;; *) out=$a ;; esac; done
out=${out:-$root/out/d3d9-translator}
read -r _ url sha < <(grep -E '^dxup-src[[:space:]]' "$root/deps.pin") || { echo "dxup-src missing from deps.pin" >&2; exit 2; }
patchfile=$root/patches/dxup-src/0001-play-crt-free.patch
read -r patchsha _ < <(sha256sum "$patchfile")
read -r recipe _ < <(sha256sum "$patchfile" "${BASH_SOURCE[0]}" \
    "$root/src/tools/d3d9-translator/entry.cpp" \
    "$root/src/tools/d3d9-translator/support.cpp" \
    "$root/src/tools/d3d9-translator/libc.cpp" | sha256sum)
base=$root/third_party/dxup-build; work=$base/${sha:0:16}-${recipe:0:16}   # source and recipe changes invalidate the tree
tar=$work/dxup-src.tar.gz
echo "dxup-src url=$url sha256=$sha out=$out"
(( dry )) && exit 0
for t in x86_64-w64-mingw32-g++ curl tar sha256sum patch; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }
done
# CI installs plain lld; development hosts here carry the versioned name.
link() { lld-link-19 "$@" 2>/dev/null || lld-link "$@"; }
command -v lld-link-19 >/dev/null || command -v lld-link >/dev/null || { echo "missing tool: lld-link" >&2; exit 2; }
mkdir -p "$work" "$out"
rm -f "$out"/d3d9translator.dll "$out"/SHA256SUMS "$out"/SOURCE.txt   # never leave a DLL from an older pin next to new labels
if [[ ! -f $tar ]]; then curl -fsSL -o "$tar.part" "$url"; mv "$tar.part" "$tar"; fi
echo "$sha  $tar" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $tar; deleting" >&2; rm -f "$tar"; exit 1; }
src=$work/dxup-7cce9ee7ddecadbf58f12f74547dc1bf1e09c5f7   # the tarball's own top directory
if [[ ! -f $work/.extracted ]]; then   # marker written only after a complete extract; an interrupted one is redone
  rm -rf "$src" "$work/obj"; tar -C "$work" -xf "$tar"
  patch --batch --fuzz=0 -d "$src" -p1 < "$patchfile"
  touch "$work/.extracted"
fi
obj=$work/obj; mkdir -p "$obj"
# -fno-builtin and -fno-tree-loop-distribute-patterns for the same reason as the
# shim's build.sh: this DLL supplies its own memset/memcpy, and without them the
# compiler turns those loops back into calls to themselves.
flags=(-c -O2 -std=c++17 -fno-builtin -fno-tree-loop-distribute-patterns
       -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-stack-protector
       -fno-asynchronous-unwind-tables -Wall -Wextra -Wno-unused-parameter
       -Wno-sign-compare -Wno-missing-field-initializers)
# The donor's headers include <memory> for std::shared_ptr. On the mcf thread
# model (NixOS's mingw) libstdc++ pulls that in through mcfgthread, whose
# headers sit outside the compiler's search path; on Ubuntu's win32 model
# nothing extra is needed. Probe rather than assume, so the same script builds
# on both.
mcfg_flags=()
if ! echo '#include <memory>' | x86_64-w64-mingw32-g++ -x c++ -fsyntax-only - >/dev/null 2>&1; then
  mcfg_inc=
  for d in /usr/x86_64-w64-mingw32/include /usr/share/mingw-w64/include \
           /nix/store/*mcfgthread*x86_64-w64-mingw32*/include; do
    [[ -f $d/mcfgthread/gthr.h ]] && mcfg_inc=$d && break
  done
  [[ -n $mcfg_inc ]] || { echo "mcfgthread headers not found (needed by <memory> in the donor's headers)" >&2; exit 2; }
  mcfg_flags=(-I"$mcfg_inc")
fi
for f in "$src"/src/dx9asm/dx9asm_*.cpp "$src"/src/dx9asm/dxbc_*.cpp \
         "$src"/src/util/shared_conversions.cpp "$src"/src/extern/gpuopen/DXBCChecksum.cpp; do
  x86_64-w64-mingw32-g++ "${flags[@]}" -I"$src/src" "${mcfg_flags[@]}" -o "$obj/$(basename "${f%.cpp}").o" "$f"
done
for f in entry support libc; do
  x86_64-w64-mingw32-g++ "${flags[@]}" -I"$src/src" "${mcfg_flags[@]}" \
      -o "$obj/$f.o" "$root/src/tools/d3d9-translator/$f.cpp"
done
mingw_lib=$(dirname "$(x86_64-w64-mingw32-g++ -print-file-name=libkernel32.a)")
link /dll /noentry /nodefaultlib /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat \
    /def:"$root/src/tools/d3d9-translator/d3d9translator.def" /implib:"$obj/d3d9translator.lib" \
    /out:"$out/d3d9translator.dll" "$obj"/*.o /libpath:"$mingw_lib" libkernel32.a
# The image is only usable if Play can route everything it imports: the app
# partition supplies the kernel and nothing else. A writable code section would
# fail W^X.
bad=$(llvm-readobj --coff-imports "$out/d3d9translator.dll" | awk '/^  Name: /{print tolower($2)}' | sort -u \
      | grep -Ev '^(kernel32)\.dll$' || true)
[[ -z $bad ]] || { echo "d3d9translator.dll imports DLLs Play does not route: $bad" >&2; exit 1; }
rwx=$(llvm-readobj --sections "$out/d3d9translator.dll" \
      | awk '/Name: \./{n=$2;w=0;x=0} /IMAGE_SCN_MEM_WRITE/{w=1} /IMAGE_SCN_MEM_EXECUTE/{x=1} \
             /^ *\]$/{if(w&&x)print n}')
[[ -z $rwx ]] || { echo "d3d9translator.dll has writable code section(s): $rwx" >&2; exit 1; }
llvm-readobj --coff-exports "$out/d3d9translator.dll" | grep -q 'Name: d3d9Translate$' \
    || { echo "d3d9translator.dll does not export d3d9Translate" >&2; exit 1; }
( cd "$out" && sha256sum -- d3d9translator.dll >SHA256SUMS )
cat >"$out/SOURCE.txt" <<S
Built from DXUP $(basename "$url") (sha256 $sha), zlib licence, Copyright (c) 2018 Joshua Ashton,
source: https://github.com/Joshua-Ashton/dxup at 7cce9ee7ddecadbf58f12f74547dc1bf1e09c5f7
Build: $(x86_64-w64-mingw32-g++ --version | head -1); mingw-w64, CRT-free, kernel32 only.
Patch: 0001-play-crt-free.patch sha256 $patchsha
Recipe sha256 $recipe
This DLL is not part of the xboxnative repository; rebuild it with src/tools/build-d3d9-translator.sh.
S
cp "$patchfile" "$out/0001-play-crt-free.patch"
echo "ok: $out"; cat "$out/SHA256SUMS"
