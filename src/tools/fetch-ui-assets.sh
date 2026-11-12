#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# fetch-ui-assets.sh [outdir] -> <outdir>/stb_truetype.h, selawk.ttf, selawksb.ttf, selawkb.ttf,
# LICENSE-stb.txt, LICENSE-Selawik.txt, SOURCE.txt (default: out/ui-assets)
# Fetches the pinned UI toolkit assets (`stb-truetype`, `stb-license`, `selawik-release`, `selawik-license`
# in deps.pin, SHA-256 verified): stb_truetype.h (public domain / MIT) that ui_font.h rasterises through,
# and Microsoft Selawik regular/semibold/bold (SIL OFL 1.1), the metric-compatible Segoe UI substitute the
# mockups use. build.sh copies the fonts and licences into the package's layout/fonts/. Outputs are
# git-ignored and must never be committed. Needs: curl, sha256sum, python3.
# Cache: third_party/ui-assets/<sha prefix>/ (git-ignored).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=${1:-$root/out/ui-assets}
for t in curl sha256sum python3; do command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }; done
mkdir -p "$out"
fetch() { # url sha file
  if [[ ! -f $3 ]]; then curl -fsSL --proto '=https' -o "$3.part" "$1"; mv "$3.part" "$3"; fi
  echo "$2  $3" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $3; deleting" >&2; rm -f "$3"; exit 1; }
}
pin() { read -r _ url sha < <(grep -E "^$1[[:space:]]" "$root/deps.pin") || { echo "$1 missing from deps.pin" >&2; exit 2; }; }

pin stb-truetype; work=$root/third_party/ui-assets/${sha:0:16}; mkdir -p "$work"
fetch "$url" "$sha" "$work/stb_truetype.h"; cp "$work/stb_truetype.h" "$out/"
pin stb-license; work=$root/third_party/ui-assets/${sha:0:16}; mkdir -p "$work"
fetch "$url" "$sha" "$work/LICENSE-stb.txt"; cp "$work/LICENSE-stb.txt" "$out/"

pin selawik-release; work=$root/third_party/ui-assets/${sha:0:16}; mkdir -p "$work"
fetch "$url" "$sha" "$work/Selawik_Release.zip"
if [[ ! -f $work/x/selawk.ttf ]]; then
  rm -rf "$work/x"; mkdir -p "$work/x"
  # Untrusted archive: refuse absolute or parent-escaping members before extracting into an empty directory.
  python3 -I -c '
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    for n in z.namelist():
        if n.startswith("/") or ".." in n.split("/"): sys.exit("unsafe member: " + n)
    z.extractall(sys.argv[2])
' "$work/Selawik_Release.zip" "$work/x"
fi
pin selawik-license; lwork=$root/third_party/ui-assets/${sha:0:16}; mkdir -p "$lwork"
fetch "$url" "$sha" "$lwork/LICENSE-Selawik.txt"
cp "$work/x/selawk.ttf" "$work/x/selawksb.ttf" "$work/x/selawkb.ttf" "$lwork/LICENSE-Selawik.txt" "$out/"

printf 'stb_truetype %s\nsha256 %s\nlicense public domain / MIT (LICENSE-stb.txt)\n\n' \
  "$(grep -E '^stb-truetype[[:space:]]' "$root/deps.pin" | awk '{print $2}')" \
  "$(grep -E '^stb-truetype[[:space:]]' "$root/deps.pin" | awk '{print $3}')" > "$out/SOURCE.txt"
printf 'Selawik 1.01 %s\nsha256 %s\nlicense SIL OFL 1.1 (LICENSE-Selawik.txt)\nsource https://github.com/microsoft/Selawik\n' \
  "$(grep -E '^selawik-release[[:space:]]' "$root/deps.pin" | awk '{print $2}')" \
  "$(grep -E '^selawik-release[[:space:]]' "$root/deps.pin" | awk '{print $3}')" >> "$out/SOURCE.txt"
echo "stb_truetype.h, selawk{,sb,b}.ttf, licences -> $out"
