#!/bin/bash
# build.sh [OUTDIR=./out] -> out/layout, out/k32.msix (signed with out/test.pfx), out/test.cer
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
here="$(cd "$(dirname "$0")" && pwd)"; cd "$here"; out="${1:-out}"; mkdir -p "$out/layout" "$out/obj"   # relative paths: clang-cl treats /abs/paths as options
python3 gen-mlp-map.py
CB="${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}"
case "${2:-uwph}" in
  uwph) manifest=AppxManifest.xml; lane_flags=() ;;
  wxl) manifest=AppxManifest.Wxl.xml; lane_flags=() ;;
  mlp) manifest=AppxManifest.Mlp.xml; lane_flags=() ;;
  hid) manifest=AppxManifest.Hid.xml; lane_flags=() ;;
  tls) manifest=AppxManifest.Tls.xml; lane_flags=(/DWXL_TLS_LANE) ;;
  cs32) manifest=AppxManifest.Cs32.xml; lane_flags=() ;;
  swc) manifest=AppxManifest.Swc.xml; lane_flags=() ;;
  *) echo "usage: $0 [OUTDIR] [uwph|wxl|hid|mlp|tls|swc]" >&2; exit 2 ;;
esac
main_link=()
if [[ ${2:-uwph} == tls ]]; then
  bash tests/build-tls-guests.sh "$out"
  main_link=(--link-arg "$out/layout/tls-carrier-a.lib" --link-arg "$out/layout/tls-carrier-b.lib")
fi
UWP_OBJ_DIR="$out/host-obj" "$CB/scripts/build.sh" --uwp --out "$out/layout/uwph-host.dll" --link-arg /dll --link-arg /export:__wine_unixlib_handle,DATA --link-arg /export:__wine_unix_call_dispatcher,DATA backend.cpp -- /MT /O2 /DNDEBUG
UWP_OBJ_DIR="$out/test-obj" "$CB/scripts/build.sh" --uwp --out "$out/layout/kernel-test.dll" --link-arg /dll kernel_test.cpp -- /MT /O2 /DNDEBUG
UWP_OBJ_DIR="$out/d3dt-obj" "$CB/scripts/build.sh" --uwp --out "$out/layout/d3dt-test.dll" --link-arg /dll d3dt_test.cpp -- /MT /O2 /DNDEBUG
UWP_OBJ_DIR="$out/obj" "$CB/scripts/build.sh" --uwp --out "$out/layout/k32.exe" "${main_link[@]}" --jobs 4 main.cpp -- /MT /O2 /DNDEBUG "${lane_flags[@]}"
bash "$here/../user32/build.sh"
cp "$here/../user32/out/layout/uwph-u32c.dll" "$out/layout/"
bash "$here/../hid/build.sh" "$out/layout"
rm -f "$out/layout/hid-test.exe"
# ABI-checked named failure/empty services and the existing CoreWindow driver.
x86_64-w64-mingw32-g++ -shared -nostdlib -std=c++17 -O2 -Wl,--entry,0 -Wl,--subsystem,windows:6.2 -Wl,--dynamicbase -Wl,--nxcompat -o "$out/layout/entry-stubs.dll" entry_stubs.cpp -lkernel32
python3 - "$out/layout/entry-stubs.dll" <<'PYCODE'
import struct,sys
p=sys.argv[1]; b=bytearray(open(p,'rb').read()); pe=struct.unpack_from('<I',b,0x3c)[0]; off=pe+24+70
struct.pack_into('<H',b,off,struct.unpack_from('<H',b,off)[0]|0x1000); open(p,'wb').write(b)
PYCODE
UWP_OBJ_DIR="$out/display-obj" "$CB/scripts/build.sh" --uwp --out "$out/layout/uwph-user32.dll" --link-arg /dll --link-arg /def:../display/display.def ../display/display.cpp -- /MT /O2 /DNDEBUG

x86_64-w64-mingw32-gcc -c -Os -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables wxl_test.c -o "$out/obj/wxl-test.obj"
lld-link-19 /nodefaultlib /entry:wxl_entry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/layout/wxl-test.exe" "$out/obj/wxl-test.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
if [[ ${2:-uwph} == tls ]]; then
  x86_64-w64-mingw32-gcc -O1 -c tests/lifecycle-dll.c -o "$out/obj/lifecycle.obj"
  lld-link-19 /dll /nodefaultlib /entry:test_dll_entry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/layout/lifecycle.dll" "$out/obj/lifecycle.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
fi
if [[ ${2:-uwph} == swc ]]; then
  x86_64-w64-mingw32-g++ -c -Os -fno-stack-protector -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -fno-unwind-tables swc_test.cpp -o "$out/obj/swc-test.obj"
  lld-link-19 /nodefaultlib /entry:swc_entry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/layout/swc-test.exe" "$out/obj/swc-test.obj" /libpath:/usr/x86_64-w64-mingw32/lib libkernel32.a libuser32.a libd3d11.a libdxgi.a libuuid.a
fi
cp -r "$here/Assets" "$out/layout/"
cp "$here/$manifest" "$out/layout/AppxManifest.xml"
# test certificate (subject must equal manifest Publisher); created once, never committed
if [ ! -f "$out/test.pfx" ]; then
  openssl req -x509 -newkey rsa:2048 -nodes -keyout "$out/test.key" -out "$out/test.pem" -days 825 -subj "/CN=grok-xbox-dev" \
    -addext "basicConstraints=critical,CA:FALSE" -addext "keyUsage=critical,digitalSignature" -addext "extendedKeyUsage=codeSigning,1.3.6.1.5.5.7.3.3" 2>/dev/null
  openssl pkcs12 -export -out "$out/test.pfx" -inkey "$out/test.key" -in "$out/test.pem" -passout pass:test
  openssl x509 -in "$out/test.pem" -outform DER -out "$out/test.cer"
fi
rm -f "$out/k32.msix" "$out/k32-unsigned.msix"
makemsix pack -d "$out/layout" -p "$out/k32-unsigned.msix"
osslsigncode sign -pkcs12 "$out/test.pfx" -pass test -h sha256 -in "$out/k32-unsigned.msix" -out "$out/k32.msix"
ls -l "$out/k32.msix"
