#!/bin/bash
# fetch-sqlite.sh -> third_party/sqlite/{sqlite3.c,sqlite3.h}  (SQLite amalgamation, public domain)
# Pinned: SQLite 3.54.1 (3540100). Verifies BOTH the SHA-256 of the zip (recorded by us) and the
# SHA3-256 that sqlite.org publishes on https://sqlite.org/download.html. Idempotent.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; dst="$here/third_party/sqlite"
ver=3540100; url="https://sqlite.org/2026/sqlite-amalgamation-$ver.zip"
sha256=0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9
sha3=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
[ -f "$dst/sqlite3.c" ] && [ -f "$dst/sqlite3.h" ] && [ -f "$dst/.pinned-$ver" ] && exit 0
mkdir -p "$dst"; tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
src=${SQLITE_ZIP:-}; if [ -n "$src" ]; then cp "$src" "$tmp/s.zip"; else curl -fsSL "$url" -o "$tmp/s.zip"; fi
[ "$(sha256sum "$tmp/s.zip" | cut -d' ' -f1)" = "$sha256" ] || { echo "sqlite zip SHA-256 mismatch" >&2; exit 1; }
[ "$(python3 -c 'import hashlib,sys;print(hashlib.sha3_256(open(sys.argv[1],"rb").read()).hexdigest())' "$tmp/s.zip")" = "$sha3" ] || { echo "sqlite zip SHA3-256 mismatch" >&2; exit 1; }
unzip -oq "$tmp/s.zip" -d "$tmp"; cp "$tmp/sqlite-amalgamation-$ver/sqlite3.c" "$tmp/sqlite-amalgamation-$ver/sqlite3.h" "$dst/"
touch "$dst/.pinned-$ver"; echo "sqlite $ver ready in $dst"
