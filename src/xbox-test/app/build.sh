#!/bin/bash
# build.sh [OUTDIR=./out] -> out/layout, out/xbtest.msix (signed with out/test.pfx), out/test.cer
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
here="$(cd "$(dirname "$0")" && pwd)"; cd "$here"; out="${1:-out}"; mkdir -p "$out/layout" "$out/obj"   # relative paths: clang-cl treats /abs/paths as options
python3 "$here/../../tools/xbox/gen-gap-routes.py"
# Donors are always built from deps.pin, never copied from a host Wine install.
donors="$here/../../../out/wine-donors"
bash "$here/../../tools/build-wine-donors.sh" "$donors"
python3 -I "$here/donors/inspect.py" --donor-dir "$donors" --output "$out/obj/donor_imports.generated.h"
CB="${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}"
UWP_OBJ_DIR="$out/obj" "$CB/scripts/build.sh" --uwp --out "$out/layout/xbtest.exe" --jobs 4 main.cpp qrcodegen.cpp vendor/zlib/inflate.c vendor/zlib/inftrees.c vendor/zlib/inffast.c vendor/zlib/adler32.c vendor/zlib/crc32.c vendor/zlib/zutil.c vendor/lzma/LzmaDec.c vendor/zstd/zstddeclib.c --link-arg bcrypt.lib -- /MT /O2 /DNDEBUG "/I$out/obj"
bash ../audio/build.sh "$out/layout"
# Separate native TLS slots for mapped guest modules. Windows initializes each
# carrier's static storage on every native guest thread; never replace GS/TEB.
for slot in $(seq -w 0 15); do
  UWP_OBJ_DIR="$out/carrier-$slot-obj" "$CB/scripts/build.sh" --uwp --out "$out/layout/play-tls-$slot.dll" --link-arg /dll ../uwph/host/tls_carrier.cpp -- /MT /O2 /GS-
done
bash ../uwph/user32/build.sh
cp ../uwph/user32/out/layout/{uwph-u32c.dll,u32c-test.exe} "$out/layout/"
# Nested packaged DLL proves SetDllDirectory affects an actual load.
mkdir -p "$out/layout/k32t"
x86_64-w64-mingw32-gcc -c -Os -fno-stack-protector ../shared/tests/search-dll.c -o "$out/obj/search-dll.obj"
lld-link-19 /dll /noentry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/layout/k32t/directory-test.dll" "$out/obj/search-dll.obj"
mkdir -p "$out/layout/donors"
cp "$donors"/{avrt,hid,setupapi,shcore,version}.dll "$out/layout/donors/"
cp "$donors"/{SHA256SUMS,SOURCE.txt,0001-pe-rodata.patch} "$here/donors/COPYING.LIB" "$here/donors/LICENSE" "$out/layout/donors/"
# Steam API provider for owned games (gbe_fork, pinned release; never committed).
gbe="$here/../../../out/gbe-fork"
bash "$here/../../tools/fetch-gbe-fork.sh" "$gbe"
mkdir -p "$out/layout/steam-provider/gbe"
cp "$gbe"/{steam_api64.dll,LICENSE,SOURCE.txt} "$out/layout/steam-provider/gbe/"
cp -r "$here/Assets" "$here/AppxManifest.xml" "$out/layout/"
# test certificate (subject must equal manifest Publisher); created once, never committed
if [ ! -f "$out/test.pfx" ]; then
  openssl req -x509 -newkey rsa:2048 -nodes -keyout "$out/test.key" -out "$out/test.pem" -days 825 -subj "/CN=grok-xbox-dev" \
    -addext "basicConstraints=critical,CA:FALSE" -addext "keyUsage=critical,digitalSignature" -addext "extendedKeyUsage=codeSigning,1.3.6.1.5.5.7.3.3" 2>/dev/null
  openssl pkcs12 -export -out "$out/test.pfx" -inkey "$out/test.key" -in "$out/test.pem" -passout pass:test
  openssl x509 -in "$out/test.pem" -outform DER -out "$out/test.cer"
fi
rm -f "$out/xbtest.msix" "$out/xbtest-unsigned.msix"
makemsix pack -d "$out/layout" -p "$out/xbtest-unsigned.msix"
osslsigncode sign -pkcs12 "$out/test.pfx" -pass test -h sha256 -in "$out/xbtest-unsigned.msix" -out "$out/xbtest.msix"
ls -l "$out/xbtest.msix"
