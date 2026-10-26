#!/bin/bash
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
cd "$(dirname "$0")"
out="${1:-out}";mkdir -p "$out/layout"
CB="${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}"
python3 generate_routes.py "$out/routes.generated.h"
UWP_OBJ_DIR="$out/driver-obj" "$CB/scripts/build.sh" --uwp --out "$out/layout/uwph-user32.dll" --link-arg /dll --link-arg /def:display.def display.cpp dll_map.cpp -- /MT /O2 /I"$out"
x86_64-w64-mingw32-dlltool -d imports.def -l "$out/libuwph-user32.a"
x86_64-w64-mingw32-g++ -shared -nostdlib -O2 -o "$out/layout/window-test.dll" window_test.cpp "$out/libuwph-user32.a" -ld3d11 -ldxgi -luuid -lws2_32 -lbcrypt -lole32 -lversion -lkernel32 -lgcc -Wl,--entry,0,--subsystem,windows:6.2,--nxcompat,--dynamicbase,--no-insert-timestamp -Wl,--major-os-version,6,--minor-os-version,2
# GNU ld has no --appcontainer flag; set the documented PE characteristic.
python3 - "$out/layout/window-test.dll" <<'PY'
import struct,sys
p=sys.argv[1];b=bytearray(open(p,'rb').read());pe=struct.unpack_from('<I',b,0x3c)[0];off=pe+24+70
v=struct.unpack_from('<H',b,off)[0];struct.pack_into('<H',b,off,v|0x1000);open(p,'wb').write(b)
PY
UWP_OBJ_DIR="$out/app-obj" "$CB/scripts/build.sh" --uwp --out "$out/layout/w32u.exe" main.cpp -- /MT /O2
cp -r ../host/Assets AppxManifest.xml "$out/layout/"
if [ ! -f "$out/test.pfx" ];then
 openssl req -x509 -newkey rsa:2048 -nodes -keyout "$out/test.key" -out "$out/test.pem" -days 825 -subj /CN=grok-xbox-dev -addext basicConstraints=critical,CA:FALSE -addext keyUsage=critical,digitalSignature -addext extendedKeyUsage=codeSigning,1.3.6.1.5.5.7.3.3 2>/dev/null
 openssl pkcs12 -export -out "$out/test.pfx" -inkey "$out/test.key" -in "$out/test.pem" -passout pass:test
 openssl x509 -in "$out/test.pem" -outform DER -out "$out/test.cer"
fi
rm -f "$out/w32u.msix" "$out/unsigned.msix"
makemsix pack -d "$out/layout" -p "$out/unsigned.msix"
osslsigncode sign -pkcs12 "$out/test.pfx" -pass test -h sha256 -in "$out/unsigned.msix" -out "$out/w32u.msix"
