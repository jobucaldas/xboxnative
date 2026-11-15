#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# fetch-mono.sh [outdir] -> Mono's x64 runtime, .NET 4.5 BCL and MIT licence.
# The installer and extracted runtime are build inputs only; never commit them.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=${1:-$root/out/mono}
read -r _ url sha < <(grep -E '^mono-win-x64[[:space:]]' "$root/deps.pin") || { echo "mono-win-x64 missing from deps.pin" >&2; exit 2; }
read -r _ lurl lsha < <(grep -E '^mono-license[[:space:]]' "$root/deps.pin") || { echo "mono-license missing from deps.pin" >&2; exit 2; }
for t in curl sha256sum msiextract; do command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }; done
cache="$root/third_party/mono/${sha:0:16}"
mkdir -p "$cache" "$out"
fetch() { # url sha file
  if [[ ! -f $3 ]]; then curl -fsSL --retry 2 -o "$3.part" "$1"; mv "$3.part" "$3"; fi
  echo "$2  $3" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $3; deleting" >&2; rm -f "$3"; exit 1; }
}
archive="$cache/$(basename "$url")"
fetch "$url" "$sha" "$archive"
fetch "$lurl" "$lsha" "$cache/LICENSE"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/mono-extract.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
msiextract --directory "$tmp" "$archive" >/dev/null
mono="$tmp/Mono"
[[ -f "$mono/bin/mono-2.0-sgen.dll" && -d "$mono/lib/mono/4.5" && -d "$mono/etc/mono" ]] || {
  echo "Mono MSI has an unexpected runtime layout" >&2; exit 1;
}
mkdir -p "$out/lib/mono" "$out/etc/mono"
cp "$mono/bin/mono-2.0-sgen.dll" "$mono/bin/MonoPosixHelper.dll" "$mono/bin/libmono-btls-shared.dll" "$out/"
cp -R "$mono/lib/mono/4.5" "$out/lib/mono/"
cp "$mono/etc/mono/config" "$out/etc/mono/config"
cp -R "$mono/etc/mono/4.5" "$out/etc/mono/"
cp "$cache/LICENSE" "$out/LICENSE"
printf 'Mono 6.12.0.206 Windows x64\nsource %s\nsha256 %s\nlicense MIT (LICENSE)\n' "$url" "$sha" > "$out/SOURCE.txt"
echo "Mono 6.12.0.206 x64 runtime and .NET 4.5 BCL -> $out"
