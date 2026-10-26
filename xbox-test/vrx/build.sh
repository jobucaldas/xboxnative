#!/bin/bash
# build.sh [OUTDIR=./out] -> out/layout, out/vrx.msix (signed with out/test.pfx), out/test.cer
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
here="$(cd "$(dirname "$0")" && pwd)"; cd "$here"; out="${1:-out}"; mkdir -p "$out/layout" "$out/obj"   # relative paths: clang-cl treats /abs/paths as options
CB="${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}"
UWP_OBJ_DIR="$out/obj" "$CB/scripts/build.sh" --uwp --out "$out/layout/vrx.exe" --jobs 4 main.cpp -- /MT /O2 /DNDEBUG
cp -r "$here/Assets" "$here/AppxManifest.xml" "$out/layout/"
# test certificate (subject must equal manifest Publisher); created once, never committed
if [ ! -f "$out/test.pfx" ]; then
  openssl req -x509 -newkey rsa:2048 -nodes -keyout "$out/test.key" -out "$out/test.pem" -days 825 -subj "/CN=grok-xbox-dev" \
    -addext "basicConstraints=critical,CA:FALSE" -addext "keyUsage=critical,digitalSignature" -addext "extendedKeyUsage=codeSigning,1.3.6.1.5.5.7.3.3" 2>/dev/null
  openssl pkcs12 -export -out "$out/test.pfx" -inkey "$out/test.key" -in "$out/test.pem" -passout pass:test
  openssl x509 -in "$out/test.pem" -outform DER -out "$out/test.cer"
fi
rm -f "$out/vrx.msix" "$out/vrx-unsigned.msix"
makemsix pack -d "$out/layout" -p "$out/vrx-unsigned.msix"
osslsigncode sign -pkcs12 "$out/test.pfx" -pass test -h sha256 -in "$out/vrx-unsigned.msix" -out "$out/vrx.msix"
ls -l "$out/vrx.msix"
