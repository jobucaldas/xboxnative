#!/bin/bash
# build.sh [OUTDIR=./out] -> out/layout, out/xbtest.msix (signed with out/test.pfx), out/test.cer
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)   # repo root: defaults below must not depend on the cwd
export PATH="$HOME/.local/bin:$PATH"
here="$(cd "$(dirname "$0")" && pwd)"; cd "$here"; out="${1:-out}"; mkdir -p "$out/layout" "$out/obj"   # relative paths: clang-cl treats /abs/paths as options
python3 "$here/../../tools/xbox/gen-gap-routes.py"
# Donors are always built from deps.pin, never copied from a host Wine install.
donors="$here/../../../out/wine-donors"
bash "$here/../../tools/build-wine-donors.sh" "$donors"
python3 -I "$here/donors/inspect.py" --donor-dir "$donors" --output "$out/obj/donor_imports.generated.h"
CB="${UWP_CROSSBUILD:-$root/out/uwp-crossbuild}"
# UI toolkit (issue #51): stb_truetype.h for the launcher's compile, and the Selawik fonts it loads at runtime.
ui_assets="$here/../../../out/ui-assets"
bash "$here/../../tools/fetch-ui-assets.sh" "$ui_assets"
mkdir -p "$out/layout/fonts"
cp "$ui_assets"/{selawk.ttf,selawksb.ttf,selawkb.ttf,LICENSE-Selawik.txt,LICENSE-stb.txt,SOURCE.txt} "$out/layout/fonts/"
bash ../uwph/host/build-sqlite-lib.sh "$out/obj/sqlite3.lib"
# host/tls_slot0.cpp must stay the FIRST object: it reserves the start of the launcher's static TLS
# block so guest EXEs that assume TLS slot 0 (MSVC /GA code) get it. See host/tls_slot0.cpp.
UWP_OBJ_DIR="$out/obj" "$CB/scripts/build.sh" --uwp --link-arg "$out/obj/sqlite3.lib" --out "$out/layout/xbtest.exe" --jobs 4 host/tls_slot0.cpp main.cpp qrcodegen.cpp vendor/zlib/inflate.c vendor/zlib/inftrees.c vendor/zlib/inffast.c vendor/zlib/adler32.c vendor/zlib/crc32.c vendor/zlib/zutil.c vendor/lzma/LzmaDec.c vendor/zstd/zstddeclib.c --link-arg bcrypt.lib -- /MT /O2 /DNDEBUG "/I$out/obj" "/I$ui_assets" /I../uwph/host/third_party/sqlite
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
cp "$donors"/{SHA256SUMS,SOURCE.txt,0001-pe-rodata.patch,0002-d3dcompiler-static-vkd3d.patch} "$here/donors/COPYING.LIB" "$here/donors/LICENSE" "$out/layout/donors/"
# Wine's HLSL shader compiler (d3dcompiler_47, static vkd3d): xgameFinal.exe
# imports D3DCompile/D3DDisassemble/D3DGetBlobPart/D3DReflect and the d3d10
# shim's Compiler::get() loads the DLL by name. It is a probed Wine donor, so
# it lives in donors/ beside the others; the loader serves guest loads through
# donor_pe::exported after the probe gate. graphics/SHA256SUMS lists only the
# graphics donors: merge the two manifests so every donor beside the others
# verifies against the shipped checksums.
(cd "$donors" && cat SHA256SUMS graphics/SHA256SUMS) > "$out/layout/donors/SHA256SUMS"
cp "$donors"/graphics/*.dll "$out/layout/donors/"
# Wine's C++ runtime (msvcp140 family, concrt140) for games without their own copy.
mkdir -p "$out/layout/runtime"
cp "$donors"/runtime/*.dll "$donors"/runtime/SHA256SUMS "$out/layout/runtime/"
# Play's D3D10 shim (source in uwph/d3d10): the console has no d3d10.dll or
# d3d10_1.dll, and one PE answers both names plus d3d10core.dll.
bash ../uwph/d3d10/build.sh "$out/layout/runtime"
# Play's D3D9 shim (source in uwph/d3d9, issue #172): the console has
# no d3d9.dll. It serves the device, buffers, textures, states and drawing over
# D3D11, and translates shader model 1-3 bytecode with the DLL below.
bash ../uwph/d3d9/build.sh "$out/layout/runtime"
# The shim's shader translator (src/tools/build-d3d9-translator.sh): DXUP's
# dx9asm built from pinned source. The shim loads it by name, so it lives in the
# runtime directory beside the shims; Play's loader serves it from there.
bash ../../tools/build-d3d9-translator.sh "$out/layout/runtime"
cp "$donors"/SOURCE.txt "$here/donors/COPYING.LIB" "$here/donors/LICENSE" "$out/layout/runtime/"
# OpenGL for games that load OPENGL32.DLL (issue #176): Mesa's d3d12 driver, built from the pinned source.
# Play maps these from runtime/ after any copy the game ships. PLAY_MESA=0 builds a launcher without them (Play's
# failing WGL stubs then answer, as before); the Mesa build needs meson, ninja, python mako and pyyaml.
if [ "${PLAY_MESA:-1}" != 0 ]; then
  mesa="$here/../../../out/mesa-d3d12"
  bash "$here/../../tools/build-mesa-d3d12.sh" "$mesa" --build
  cp "$mesa"/{opengl32.dll,libgallium_wgl.dll,LICENSE-Mesa.rst,LICENSE-DirectX-Headers} "$out/layout/runtime/"
  mkdir -p "$out/layout/runtime/mesa-patches"; cp "$mesa"/patches/*.patch "$out/layout/runtime/mesa-patches/"
  cp "$mesa/SHA256SUMS" "$out/layout/runtime/SHA256SUMS-mesa"; cp "$mesa/SOURCE.txt" "$out/layout/runtime/SOURCE-mesa.txt"
fi
# Vulkan for games that load vulkan-1.dll (issue #177): the Khronos loader built from the pinned source,
# Mesa's dzn driver (Vulkan on D3D12) as the real ICD, and a null ICD that reports zero physical devices as the
# fallback when dzn cannot initialise. PLAY_VULKAN=0 builds a launcher without them (LoadLibrary("vulkan-1.dll") then
# fails as it did before). The dzn build needs the same UCRT mingw/meson toolchain as the Mesa OpenGL block.
if [ "${PLAY_VULKAN:-1}" != 0 ]; then
  vk="$here/../../../out/vulkan-loader"
  bash "$here/../../tools/build-vulkan-loader.sh" "$vk" --build
  cp "$vk"/{vulkan-1.dll,vulkan-null-icd.dll,vulkan-null-icd.json} "$out/layout/runtime/"
  cp "$vk"/{LICENSE-Vulkan-Loader.txt,LICENSE-Vulkan-Headers.md} "$out/layout/runtime/"
  cp "$vk/SHA256SUMS" "$out/layout/runtime/SHA256SUMS-vulkan"; cp "$vk/SOURCE.txt" "$out/layout/runtime/SOURCE-vulkan.txt"
  # dzn is part of the Vulkan stack: a build failure fails the launcher build rather than silently shipping the null
  # ICD alone. PLAY_DZN=0 is the explicit way to package without it (the loader then lists only the null ICD).
  if [ "${PLAY_DZN:-1}" != 0 ]; then
    dzn="$here/../../../out/mesa-dzn"
    bash "$here/../../tools/build-mesa-dzn.sh" "$dzn" --build
    cp "$dzn"/{vulkan_dzn.dll,vulkan-dzn.json,LICENSE-Mesa.rst,LICENSE-DirectX-Headers} "$out/layout/runtime/"
    mkdir -p "$out/layout/runtime/dzn-patches"; cp "$dzn"/patches/*.patch "$out/layout/runtime/dzn-patches/"
    cp "$dzn/SHA256SUMS" "$out/layout/runtime/SHA256SUMS-dzn"; cp "$dzn/SOURCE.txt" "$out/layout/runtime/SOURCE-dzn.txt"
  fi
fi
# Steam API provider for owned games (gbe_fork, pinned release; never committed).
gbe="$here/../../../out/gbe-fork"
bash "$here/../../tools/fetch-gbe-fork.sh" "$gbe"
mkdir -p "$out/layout/steam-provider/gbe"
cp "$gbe"/{steam_api64.dll,steamclient64.dll,LICENSE,SOURCE.txt} "$out/layout/steam-provider/gbe/"
# Console self-test fixtures (selftest.h): the same guest programs the off-device
# runner builds (tests/play-runtime.sh), packed into layout\diag so the installed
# package can exercise the Vulkan, D3D9, D3D10 and 32-bit paths on the console.
# Built from this repository's sources at package time; PLAY_SELFTEST=0 omits them.
if [ "${PLAY_SELFTEST:-1}" != 0 ]; then
  bash "$here/tests/build-diag-fixtures.sh" "$out/layout/diag"
fi
cp -r "$here/Assets" "$here/AppxManifest.xml" "$out/layout/"
# PACKAGE_VERSION=a.b.c.d (set by `xn console`) stamps only the packaged copy; the tracked manifest is untouched.
if [ -n "${PACKAGE_VERSION:-}" ]; then
  [[ $PACKAGE_VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "PACKAGE_VERSION must be a.b.c.d" >&2; exit 2; }
  sed -i -E "s/(<Identity [^>]*Version=\")[0-9.]+(\")/\1$PACKAGE_VERSION\2/" "$out/layout/AppxManifest.xml"
fi
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
