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
bash ../uwph/host/build-sqlite-lib.sh "$out/obj/sqlite3.lib"
# host/tls_slot0.cpp must stay the FIRST object: it reserves the start of the launcher's static TLS
# block so guest EXEs that assume TLS slot 0 (MSVC /GA code) get it. See host/tls_slot0.cpp.
UWP_OBJ_DIR="$out/obj" "$CB/scripts/build.sh" --uwp --link-arg "$out/obj/sqlite3.lib" --out "$out/layout/xbtest.exe" --jobs 4 host/tls_slot0.cpp main.cpp qrcodegen.cpp vendor/zlib/inflate.c vendor/zlib/inftrees.c vendor/zlib/inffast.c vendor/zlib/adler32.c vendor/zlib/crc32.c vendor/zlib/zutil.c vendor/lzma/LzmaDec.c vendor/zstd/zstddeclib.c --link-arg bcrypt.lib -- /MT /O2 /DNDEBUG "/I$out/obj" /I../uwph/host/third_party/sqlite
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
cp "$donors"/{avrt,dinput8,hid,setupapi,shcore,version}.dll "$out/layout/donors/"
cp "$donors"/{SHA256SUMS,SOURCE.txt,0001-pe-rodata.patch} "$here/donors/COPYING.LIB" "$here/donors/LICENSE" "$out/layout/donors/"
# Wine's C++ runtime (msvcp140 family, concrt140) for games without their own copy.
mkdir -p "$out/layout/runtime"
cp "$donors"/runtime/*.dll "$donors"/runtime/SHA256SUMS "$out/layout/runtime/"
# Play's D3D10 shim (source in uwph/d3d10): the console has no d3d10.dll or
# d3d10_1.dll, and one PE answers both names plus d3d10core.dll.
bash ../uwph/d3d10/build.sh "$out/layout/runtime"
# Play's D3D9 shim (source in uwph/d3d9, research/D3D9-PLAN.md): the console has
# no d3d9.dll. Milestone 1 serves the factory, device, swap chain and surfaces
# over D3D11; the rest of the API answers D3DERR_INVALIDCALL and says so.
bash ../uwph/d3d9/build.sh "$out/layout/runtime"
cp "$donors"/SOURCE.txt "$here/donors/COPYING.LIB" "$here/donors/LICENSE" "$out/layout/runtime/"
# OpenGL for games that load OPENGL32.DLL (research/OPENGL-PLAN.md): Mesa's d3d12 driver, built from the pinned source.
# Play maps these from runtime/ after any copy the game ships. PLAY_MESA=0 builds a launcher without them (Play's
# failing WGL stubs then answer, as before); the Mesa build needs meson, ninja, python mako and pyyaml.
if [ "${PLAY_MESA:-1}" != 0 ]; then
  mesa="$here/../../../out/mesa-d3d12"
  bash "$here/../../tools/build-mesa-d3d12.sh" "$mesa" --build
  cp "$mesa"/{opengl32.dll,libgallium_wgl.dll,LICENSE-Mesa.rst,LICENSE-DirectX-Headers} "$out/layout/runtime/"
  mkdir -p "$out/layout/runtime/mesa-patches"; cp "$mesa"/patches/*.patch "$out/layout/runtime/mesa-patches/"
  cp "$mesa/SHA256SUMS" "$out/layout/runtime/SHA256SUMS-mesa"; cp "$mesa/SOURCE.txt" "$out/layout/runtime/SOURCE-mesa.txt"
fi
# Vulkan for games that load vulkan-1.dll (research/VULKAN-PLAN.md): the Khronos loader built from the pinned source,
# plus a null ICD that reports zero physical devices so a Vulkan-optional engine takes its normal fallback path.
# PLAY_VULKAN=0 builds a launcher without them (LoadLibrary("vulkan-1.dll") then fails as it did before).
if [ "${PLAY_VULKAN:-1}" != 0 ]; then
  vk="$here/../../../out/vulkan-loader"
  bash "$here/../../tools/build-vulkan-loader.sh" "$vk" --build
  cp "$vk"/{vulkan-1.dll,vulkan-null-icd.dll,vulkan-null-icd.json} "$out/layout/runtime/"
  cp "$vk"/{LICENSE-Vulkan-Loader.txt,LICENSE-Vulkan-Headers.md} "$out/layout/runtime/"
  cp "$vk/SHA256SUMS" "$out/layout/runtime/SHA256SUMS-vulkan"; cp "$vk/SOURCE.txt" "$out/layout/runtime/SOURCE-vulkan.txt"
fi
# Steam API provider for owned games (gbe_fork, pinned release; never committed).
gbe="$here/../../../out/gbe-fork"
bash "$here/../../tools/fetch-gbe-fork.sh" "$gbe"
mkdir -p "$out/layout/steam-provider/gbe"
cp "$gbe"/{steam_api64.dll,steamclient64.dll,LICENSE,SOURCE.txt} "$out/layout/steam-provider/gbe/"
cp -r "$here/Assets" "$here/AppxManifest.xml" "$out/layout/"
# test certificate (subject must equal manifest Publisher); created once, never committed
# A cert from before the publisher rename (or any other subject) is replaced.
if [ ! -f "$out/test.pfx" ] || ! openssl x509 -in "$out/test.pem" -noout -subject 2>/dev/null | grep -q "CN *= *xboxnative-dev"; then
  openssl req -x509 -newkey rsa:2048 -nodes -keyout "$out/test.key" -out "$out/test.pem" -days 825 -subj "/CN=xboxnative-dev" \
    -addext "basicConstraints=critical,CA:FALSE" -addext "keyUsage=critical,digitalSignature" -addext "extendedKeyUsage=codeSigning,1.3.6.1.5.5.7.3.3" 2>/dev/null
  openssl pkcs12 -export -out "$out/test.pfx" -inkey "$out/test.key" -in "$out/test.pem" -passout pass:test
  openssl x509 -in "$out/test.pem" -outform DER -out "$out/test.cer"
fi
rm -f "$out/xbtest.msix" "$out/xbtest-unsigned.msix"
makemsix pack -d "$out/layout" -p "$out/xbtest-unsigned.msix"
osslsigncode sign -pkcs12 "$out/test.pfx" -pass test -h sha256 -in "$out/xbtest-unsigned.msix" -out "$out/xbtest.msix"
ls -l "$out/xbtest.msix"
