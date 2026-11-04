#!/bin/bash
# fetch-sqlite.sh -> third_party/sqlite/{sqlite3.c,sqlite3.h}  (SQLite amalgamation, public domain)
# Pinned: SQLite 3.54.1 (3540100). Verifies BOTH the SHA-256 of the zip (recorded by us) and the
# SHA3-256 that sqlite.org publishes on https://sqlite.org/download.html. Idempotent.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; dst="$here/third_party/sqlite"
ver=3540100
read -r _ url sha256 < <(grep -E '^sqlite-src[[:space:]]' "$here/../../../deps.pin")
[[ $url == "https://sqlite.org/2026/sqlite-amalgamation-$ver.zip" && $sha256 =~ ^[0-9a-f]{64}$ ]] || { echo 'invalid SQLite pin' >&2; exit 1; }
sha3=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
marker="$dst/.pinned-$sha256"
[ -f "$marker" ] && ( cd "$dst" && sha256sum --check --status "$(basename "$marker")" ) && exit 0
mkdir -p "$dst"; tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
src=${SQLITE_ZIP:-}; if [ -n "$src" ]; then cp "$src" "$tmp/s.zip"; else curl -fsSL "$url" -o "$tmp/s.zip"; fi
[ "$(sha256sum "$tmp/s.zip" | cut -d' ' -f1)" = "$sha256" ] || { echo "sqlite zip SHA-256 mismatch" >&2; exit 1; }
[ "$(python3 -I -c 'import hashlib,sys;print(hashlib.sha3_256(open(sys.argv[1],"rb").read()).hexdigest())' "$tmp/s.zip")" = "$sha3" ] || { echo "sqlite zip SHA3-256 mismatch" >&2; exit 1; }
unzip -oq "$tmp/s.zip" -d "$tmp"; cp "$tmp/sqlite-amalgamation-$ver/sqlite3.c" "$tmp/sqlite-amalgamation-$ver/sqlite3.h" "$dst/"
( cd "$dst" && sha256sum sqlite3.c sqlite3.h ) > "$tmp/marker"
mv "$tmp/marker" "$marker"
echo "sqlite $ver ready in $dst"
