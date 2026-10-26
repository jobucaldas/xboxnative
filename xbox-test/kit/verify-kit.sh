#!/bin/bash
# verify-kit.sh [KITDIR=/workspace/x64-run/kit/latest]
#
# Checks a kit produced by build-kit.sh:
#   1. no .pfx (private key) anywhere in the kit
#   2. manifest.json's sha256 for every package/extra matches the file on disk
#   3. every .msix's Authenticode signature verifies with osslsigncode,
#      trusting only that package's own bundled .cer (self-signed test certs,
#      not a system CA)
# Exits 0 only if every check for every entry passes.
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
here="$(cd "$(dirname "$0")" && pwd)"

out_root="/workspace/x64-run/kit"
kitdir="${1:-$out_root/latest}"
kitdir="$(cd "$kitdir" && pwd)"
manifest="$kitdir/manifest.json"
[ -f "$manifest" ] || { echo "verify-kit: no manifest.json in $kitdir" >&2; exit 2; }

tmp="$(mktemp -d "${TMPDIR:-/tmp}/kit-verify.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

fail=0
note() { echo "$1"; }
bad() { echo "FAIL: $1"; fail=1; }

pfx_found="$(find "$kitdir" -iname '*.pfx' -print)"
if [ -n "$pfx_found" ]; then
    bad "private key(s) present in kit: $pfx_found"
else
    note "PASS: no .pfx in kit"
fi

rows="$tmp/rows.tsv"
# Unit separator (not tab): bash `read` treats a lone tab as "IFS whitespace"
# and silently collapses consecutive delimiters, dropping the empty
# identity/version fields that extras legitimately have.
python3 - "$manifest" > "$rows" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
sep = "\x1f"
for kind, entries in (("package", doc.get("packages", [])), ("extra", doc.get("extras", []))):
    for e in entries:
        print(sep.join([e['name'], e.get('identity', ''), e.get('version', ''), e['file'], e['sha256'], kind]))
PY

count=0
while IFS=$'\x1f' read -r name identity version file expect_sha kind; do
    count=$((count + 1))
    path="$kitdir/$file"
    if [ ! -f "$path" ]; then
        bad "$name: $file listed in manifest.json but missing from kit"
        continue
    fi
    actual_sha="$(python3 "$here/kitlib.py" sha256 "$path")"
    if [ "$actual_sha" != "$expect_sha" ]; then
        bad "$name: sha256 mismatch (manifest $expect_sha, actual $actual_sha)"
    else
        note "PASS: $name sha256 matches manifest"
    fi

    if [ "$kind" = "extra" ]; then
        continue
    fi

    cer="$kitdir/$name.cer"
    if [ ! -f "$cer" ]; then
        bad "$name: no matching $name.cer in kit"
        continue
    fi
    pem="$tmp/$name.pem"
    if ! openssl x509 -inform DER -in "$cer" -out "$pem" 2>"$tmp/$name.openssl.err"; then
        bad "$name: $name.cer is not a valid DER certificate ($(cat "$tmp/$name.openssl.err"))"
        continue
    fi
    sig_out="$tmp/$name.verify.log"
    if osslsigncode verify -in "$path" -CAfile "$pem" > "$sig_out" 2>&1 && grep -q 'Signature verification: ok' "$sig_out"; then
        note "PASS: $name.msix signature verifies against $name.cer"
    else
        bad "$name: osslsigncode verify failed (see below)"
        sed 's/^/    /' "$sig_out"
    fi
done < "$rows"

if [ "$count" -eq 0 ]; then
    bad "manifest.json lists no packages or extras"
fi

echo "=== verify-kit: $count entries checked, kit=$kitdir ==="
if [ "$fail" -ne 0 ]; then
    echo "RESULT: FAIL"
    exit 1
fi
echo "RESULT: PASS"
