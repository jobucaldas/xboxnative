#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# fetch-sdl.sh [outdir] -> <outdir>/SDL3/{include,lib/x64}, <outdir>/SDL2/{include,lib/x64} (default: out/sdl)
# Fetches the pinned official SDL3 and SDL2 Visual C++ development packages (`sdl3-devel-vc`, `sdl2-devel-vc`
# in deps.pin, SHA-256 verified): the same MSVC-built SDL3.dll/SDL2.dll games ship, plus headers and import
# libraries. Used only by off-device Play tests (app/tests/play-sdl.sh); SDL is zlib-licensed. Outputs are
# git-ignored and must never be committed or packaged. Needs: curl, sha256sum, python3.
# Cache: third_party/sdl/<sha prefix>/ (git-ignored).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=${1:-$root/out/sdl}
for t in curl sha256sum python3; do command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }; done
mkdir -p "$out"
for name in sdl3-devel-vc sdl2-devel-vc; do
  read -r _ url sha < <(grep -E "^$name[[:space:]]" "$root/deps.pin") || { echo "$name missing from deps.pin" >&2; exit 2; }
  work=$root/third_party/sdl/${sha:0:16}; mkdir -p "$work"
  zip=$work/$(basename "$url")
  if [[ ! -f $zip ]]; then curl -fsSL --proto '=https' -o "$zip.part" "$url"; mv "$zip.part" "$zip"; fi
  echo "$sha  $zip" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $zip; deleting" >&2; rm -f "$zip"; exit 1; }
  major=SDL${name:3:1}
  rm -rf "$work/x" "$out/$major"; mkdir -p "$work/x"
  # Untrusted archive: refuse absolute or parent-escaping members before extracting into an empty directory.
  python3 -I -c '
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    for n in z.namelist():
        if n.startswith("/") or ".." in n.split("/"): sys.exit("unsafe member: " + n)
    z.extractall(sys.argv[2])
' "$zip" "$work/x"
  top=$(find "$work/x" -mindepth 1 -maxdepth 1 -type d | head -1)
  mkdir -p "$out/$major/lib"
  cp -r "$top/include" "$out/$major/"
  cp -r "$top/lib/x64" "$out/$major/lib/"
  cp "$top/LICENSE.txt" "$out/$major/" 2>/dev/null || true
  printf '%s %s\nsha256 %s\nlicense zlib\n' "$major" "$url" "$sha" > "$out/$major/SOURCE.txt"
  echo "$major -> $out/$major"
done
