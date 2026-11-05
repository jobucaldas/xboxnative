#!/bin/bash
# verify-kit-offline.sh [KITDIR=/workspace/x64-run/kit/latest]
#
# Deeper off-device check of a kit built by build-kit.sh. For every package
# listed in manifest.json (kind "package"):
#   1. unpack the .msix with makemsix (to a temp dir, nothing touches the kit)
#   2. AppxManifest.xml is well-formed XML (python3 ElementTree; xmllint is
#      not required) and has an <Identity>
#   3. Identity Publisher equals the subject of the package's own .cer
#   4. Identity Name/Version equal manifest.json's identity/version
#   5. every <File> in AppxBlockMap.xml exists in the unpacked tree with the
#      listed Size
#   6. the Application Executable named in AppxManifest.xml exists
# Also fails if any .pfx/.key/.pem is present in the kit.
# Exits non-zero naming the failing package/file.
set -uo pipefail
export PATH="$HOME/.local/bin:$PATH"
here="$(cd "$(dirname "$0")" && pwd)"

kitdir="${1:-/workspace/x64-run/kit/latest}"
kitdir="$(cd "$kitdir" && pwd)" || { echo "verify-kit-offline: bad kit dir" >&2; exit 2; }
manifest="$kitdir/manifest.json"
[ -f "$manifest" ] || { echo "verify-kit-offline: no manifest.json in $kitdir" >&2; exit 2; }
command -v makemsix >/dev/null || { echo "verify-kit-offline: makemsix not found" >&2; exit 2; }

tmp="$(mktemp -d "${TMPDIR:-/tmp}/kit-offline.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

fail=0
bad() { echo "FAIL: $1"; fail=1; }
ok() { echo "PASS: $1"; }

keys="$(find "$kitdir" \( -iname '*.pfx' -o -iname '*.key' -o -iname '*.pem' \) -print)"
if [ -n "$keys" ]; then bad "private key material in kit: $keys"; else ok "no .pfx/.key/.pem in kit"; fi

python3 - "$manifest" > "$tmp/rows" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
for e in doc.get("packages", []):
    print("\x1f".join([e["name"], e.get("identity", ""), e.get("version", ""), e["file"]]))
PY

check_pkg() {  # name identity version file
    local name="$1" identity="$2" version="$3" file="$4"
    local msix="$kitdir/$file" cer="$kitdir/$name.cer" out="$tmp/$name"
    [ -f "$msix" ] || { bad "$name: $file missing"; return; }
    [ -f "$cer" ] || { bad "$name: $name.cer missing"; return; }
    if ! makemsix unpack -p "$msix" -d "$out" -ac > "$tmp/$name.unpack.log" 2>&1; then
        bad "$file: makemsix unpack failed"; sed 's/^/    /' "$tmp/$name.unpack.log"; return
    fi
    [ -f "$out/AppxManifest.xml" ] || { bad "$file: no AppxManifest.xml after unpack"; return; }
    [ -f "$out/AppxBlockMap.xml" ] || { bad "$file: no AppxBlockMap.xml after unpack"; return; }
    local subject
    subject="$(openssl x509 -inform DER -in "$cer" -noout -subject -nameopt RFC2253 2>/dev/null | sed 's/^subject=//')"
    [ -n "$subject" ] || { bad "$name.cer: not a valid DER certificate"; return; }
    python3 - "$name" "$out" "$subject" "$identity" "$version" <<'PY'
import os, sys, xml.etree.ElementTree as ET
name, out, subject, identity, version = sys.argv[1:6]
errs = []
def local(t): return t.rsplit('}', 1)[-1]
try:
    root = ET.parse(os.path.join(out, "AppxManifest.xml")).getroot()
except ET.ParseError as e:
    print(f"FAIL: {name}: AppxManifest.xml does not parse: {e}"); sys.exit(1)
ident = next((e for e in root.iter() if local(e.tag) == "Identity"), None)
if ident is None:
    print(f"FAIL: {name}: AppxManifest.xml has no <Identity>"); sys.exit(1)
pub = ident.get("Publisher", "")
if pub != subject:
    errs.append(f"Identity Publisher {pub!r} != cert subject {subject!r}")
if identity and ident.get("Name") != identity:
    errs.append(f"Identity Name {ident.get('Name')!r} != manifest.json {identity!r}")
if version and ident.get("Version") != version:
    errs.append(f"Identity Version {ident.get('Version')!r} != manifest.json {version!r}")
exes = [e.get("Executable") for e in root.iter() if local(e.tag) == "Application"]
if not exes: errs.append("no <Application Executable=...>")
for x in exes:
    if not x or not os.path.isfile(os.path.join(out, x.replace("\\", "/"))):
        errs.append(f"Application Executable {x!r} missing from package")
try:
    bm = ET.parse(os.path.join(out, "AppxBlockMap.xml")).getroot()
except ET.ParseError as e:
    print(f"FAIL: {name}: AppxBlockMap.xml does not parse: {e}"); sys.exit(1)
n = 0
for f in (e for e in bm.iter() if local(e.tag) == "File"):
    n += 1
    rel = f.get("Name", "").replace("\\", "/")
    p = os.path.join(out, rel)
    if not os.path.isfile(p):
        errs.append(f"BlockMap file missing: {f.get('Name')}")
    elif os.path.getsize(p) != int(f.get("Size", -1)):
        errs.append(f"BlockMap size mismatch: {f.get('Name')} (listed {f.get('Size')}, actual {os.path.getsize(p)})")
if n == 0: errs.append("AppxBlockMap.xml lists no files")
if errs:
    for e in errs: print(f"FAIL: {name}: {e}")
    sys.exit(1)
print(f"PASS: {name}: manifest parses, Publisher={pub} matches cert, {n} blockmap files present, exe={','.join(exes)}")
PY
    [ $? -eq 0 ] || fail=1
}

count=0
while IFS=$'\x1f' read -r name identity version file; do
    count=$((count + 1))
    check_pkg "$name" "$identity" "$version" "$file"
done < "$tmp/rows"
[ "$count" -gt 0 ] || bad "manifest.json lists no packages"

echo "=== verify-kit-offline: $count packages checked, kit=$kitdir ==="
if [ "$fail" -ne 0 ]; then echo "RESULT: FAIL"; exit 1; fi
echo "RESULT: PASS"
