#!/usr/bin/env bash
# fetch-dep.sh <name> [dest] [--dry-run]  -- clone a pinned dependency from deps.pin at its exact commit.
# Applies patches/<name>/*.patch (if any) with `git apply --3way`. Never copies upstream code into this repo.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
name=${1:?usage: fetch-dep.sh <boxedwine|uwp-crossbuild> [dest] [--dry-run]}; dest=${2:-}; dry=0
[[ "${2:-}" == --dry-run || "${3:-}" == --dry-run ]] && dry=1
[[ "$dest" == --dry-run ]] && dest=
read -r _ url sha < <(grep -E "^$name[[:space:]]" "$root/deps.pin") || { echo "unknown dependency: $name" >&2; exit 2; }
if [[ -z "$dest" ]]; then
  case "$name" in
    uwp-crossbuild) dest=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild} ;;
    *) dest=$root/third_party/$name ;;
  esac
fi
echo "dep=$name url=$url commit=$sha dest=$dest"
(( dry )) && { ls "$root/patches/$name" 2>/dev/null | sed "s/^/would apply patch: /"; exit 0; }
if [[ ! -d "$dest/.git" ]]; then git init -q "$dest"; git -C "$dest" remote add origin "$url"; fi
git -C "$dest" fetch -q --depth 1 origin "$sha"
git -C "$dest" checkout -q --detach FETCH_HEAD
[[ "$(git -C "$dest" rev-parse HEAD)" == "$sha" ]] || { echo "pin mismatch" >&2; exit 1; }
for p in "$root/patches/$name"/*.patch; do
  [[ -e "$p" ]] || continue
  git -C "$dest" apply --3way "$p"; echo "applied $(basename "$p")"
done
echo "ok: $name @ $sha"
