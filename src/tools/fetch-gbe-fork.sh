#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# fetch-gbe-fork.sh [outdir] -> <outdir>/steam_api64.dll, steamclient64.dll, LICENSE, SOURCE.txt (default: out/gbe-fork)
# Fetches the pinned gbe_fork release (`gbe-fork-win`, `gbe-fork-license` in deps.pin, SHA-256 verified) and
# extracts only the regular x64 steam_api64.dll (the Steam API provider Play maps for games the signed-in
# account owns, AGENTS.md rule 3) and the x64 emulated steamclient64.dll (the client behind Valve's signed
# one, for SteamStub-wrapped games). gbe_fork is LGPL-3.0. Outputs are git-ignored and must never be committed.
# Needs: curl, 7z, sha256sum. Cache: third_party/gbe-fork/<sha prefix>/ (git-ignored).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=${1:-$root/out/gbe-fork}
read -r _ url sha < <(grep -E '^gbe-fork-win[[:space:]]' "$root/deps.pin") || { echo "gbe-fork-win missing from deps.pin" >&2; exit 2; }
read -r _ lurl lsha < <(grep -E '^gbe-fork-license[[:space:]]' "$root/deps.pin") || { echo "gbe-fork-license missing from deps.pin" >&2; exit 2; }
for t in curl 7z sha256sum; do command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }; done
work=$root/third_party/gbe-fork/${sha:0:16}; mkdir -p "$work" "$out"
fetch() { # url sha file
  if [[ ! -f $3 ]]; then curl -fsSL -o "$3.part" "$1"; mv "$3.part" "$3"; fi
  echo "$2  $3" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $3; deleting" >&2; rm -f "$3"; exit 1; }
}
fetch "$url" "$sha" "$work/$(basename "$url")"
fetch "$lurl" "$lsha" "$work/LICENSE"
rm -rf "$work/x"; 7z x -y -o"$work/x" "$work/$(basename "$url")" 'release/regular/x64/steam_api64.dll' \
  'release/steamclient_experimental/steamclient64.dll' >/dev/null
cp "$work/x/release/regular/x64/steam_api64.dll" "$work/x/release/steamclient_experimental/steamclient64.dll" "$work/LICENSE" "$out/"
printf 'gbe_fork %s\nsha256 %s\nlicense LGPL-3.0 (LICENSE)\nsource https://github.com/Detanup01/gbe_fork/tree/%s\n' \
  "$url" "$sha" "$(basename "$(dirname "$url")")" > "$out/SOURCE.txt"
echo "gbe_fork steam_api64.dll, steamclient64.dll -> $out"
