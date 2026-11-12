#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-mesa-dzn.sh [outdir] [--dry-run] [--build]
# Vulkan for launcher Play (issue #177, milestone 2): Mesa's dzn driver (Vulkan on D3D12) cross-built for
# Windows x64 with mingw-w64 from the pinned source (`mesa-src` in deps.pin, SHA-256 verified; `directx-headers` for the
# only subproject Mesa needs). Result: vulkan_dzn.dll (a Vulkan ICD) and the ICD manifest that points at it. Mesa is
# MIT-licensed; the DLL is a build output, git-ignored (out/ and *.dll) and never committed.
# Why dzn: the console has D3D12 (Mesa's d3d12 GL driver already renders through Play's DXGI CoreWindow adapter) but no
# Vulkan driver. dzn is the same D3D12 device/queue work with a Vulkan front end, so a game that asks for Vulkan gets a
# real physical device instead of the null ICD's zero.
# Default: fetch, verify, extract and run `meson setup`. --build also compiles and copies the DLL to <outdir>.
# --dry-run only prints the recipe (offline; what CI runs).
# Patches: patches/mesa-src/*.patch (the dzn ones: optional DXIL signer, CoreWindow swapchain without DirectComposition,
# optional experimental shader models, idempotent meta cleanup on a failed device creation, the swapchain-path log and
# the HWND-only swapchain decision), applied with
# `patch --fuzz=0` after the SHA-256 check. Needs: a UCRT-flavoured x86_64-w64-mingw32-gcc/g++ (the console has no
# msvcrt.dll; on NixOS pkgsCross.ucrt64), meson, ninja, python3 with mako and pyyaml, bison, flex, curl, xz, sha256sum, git.
# Env: MCFGTHREAD_LIBDIR/MCFGTHREAD_INCDIR as for build-mesa-d3d12.sh. The result is checked: only DLLs the console
# supplies may be imported, no writable+executable section, and the three ICD entry points are exported.
# Cache/scratch dir: third_party/mesa-dzn-build/<pin hash>/ (git-ignored). Env: JOBS (default nproc).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=; dry=0; build=0
for a in "$@"; do case "$a" in --dry-run) dry=1 ;; --build) build=1 ;; *) out=$a ;; esac; done
out=${out:-$root/out/mesa-dzn}
patches=("$root"/patches/mesa-src/*.patch)
read -r _ url sha < <(grep -E '^mesa-src[[:space:]]' "$root/deps.pin") || { echo "mesa-src missing from deps.pin" >&2; exit 2; }
read -r _ dxurl dxrev dxtag < <(grep -E '^directx-headers[[:space:]]' "$root/deps.pin") || { echo "directx-headers missing from deps.pin" >&2; exit 2; }
# Only the Vulkan driver: no Gallium (no OpenGL, no GLES/EGL/GLX), no LLVM, no video, no CL, no tests. dzn is
# `microsoft-experimental` in Mesa's option (upstream marks it non-conformant); spirv-to-dxil is its shader compiler.
MESON_OPTIONS=(
  -Dplatforms=windows -Dvulkan-drivers=microsoft-experimental -Dspirv-to-dxil=true
  -Dgallium-drivers= -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Degl=disabled -Dglx=disabled
  -Dllvm=disabled -Dmicrosoft-clc=disabled -Dgallium-d3d12-graphics=disabled -Dgallium-d3d12-video=disabled
  -Dvideo-codecs= -Dgallium-rusticl=false -Dzlib=disabled -Dzstd=disabled -Dbuild-tests=false
  -Dbuildtype=release -Dstrip=true
)
echo "mesa-src url=$url sha256=$sha directx-headers=$dxurl@$dxrev out=$out"
echo "meson options: ${MESON_OPTIONS[*]}"
echo "patches: ${patches[*]##*/}"
echo "outputs: vulkan_dzn.dll vulkan-dzn.json"
(( dry )) && exit 0
for t in x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ meson ninja bison flex curl xz sha256sum git python3; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }
done
python3 -I -c 'import mako, yaml' 2>/dev/null || { echo "missing python modules: mako, pyyaml" >&2; exit 2; }
# The console has no msvcrt.dll, so the toolchain must be UCRT-flavoured (on NixOS pkgsCross.ucrt64). Both flavours
# report the same gcc version and both ship libucrt.a/libmsvcrt.a, so link a trivial program and read its imports.
objdump=${CROSS_OBJDUMP:-x86_64-w64-mingw32-objdump}
probe=$(mktemp -d); trap 'rm -rf "$probe"' EXIT
echo 'int main(void){return 0;}' >"$probe/p.c"
probe_link=(); [[ -n ${MCFGTHREAD_LIBDIR:-} ]] && probe_link=(-L"$MCFGTHREAD_LIBDIR")
x86_64-w64-mingw32-gcc "${probe_link[@]}" "$probe/p.c" -o "$probe/p.exe" 2>"$probe/err" \
  || { cat "$probe/err" >&2; echo "cannot link with x86_64-w64-mingw32-gcc" >&2; exit 2; }
if "$objdump" -p "$probe/p.exe" | grep -qi 'DLL Name: msvcrt\.dll'; then
  echo "x86_64-w64-mingw32-gcc $(command -v x86_64-w64-mingw32-gcc) links msvcrt.dll; the console has none. Use a UCRT-flavoured mingw-w64 (NixOS: pkgsCross.ucrt64.stdenv.cc, and make sure it comes first in PATH)." >&2
  exit 2
fi
base=$root/third_party/mesa-dzn-build; recipe=$(printf '%s\n' "${MESON_OPTIONS[@]}" "$dxrev" "static-ucrt-1" "$(cat "${patches[@]}" | sha256sum)" "$(readlink -f "$(command -v x86_64-w64-mingw32-gcc)")" "$(x86_64-w64-mingw32-gcc --version | head -1)" "${MCFGTHREAD_LIBDIR:-}" "${MCFGTHREAD_INCDIR:-}" | sha256sum | cut -c1-16)   # options, pins and toolchain, not the script text: packaging edits keep the build cache
work=$base/${sha:0:16}-$recipe
tar=$base/$(basename "$url")
mkdir -p "$base" "$work" "$out"
if [[ ! -f $tar ]]; then curl -fsSL --proto '=https' -o "$tar.part" "$url"; mv "$tar.part" "$tar"; fi
echo "$sha  $tar" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $tar; deleting" >&2; rm -f "$tar"; exit 1; }
src=$work/$(basename "$tar" .tar.xz)
if [[ ! -f $work/.extracted ]]; then   # marker written only after a complete extract; an interrupted one is redone
  rm -rf "$src" "$work/build"; tar -C "$work" -xf "$tar"
  for p in "${patches[@]}"; do patch --batch --fuzz=0 -d "$src" -p1 <"$p" >/dev/null; done
  touch "$work/.extracted"
fi
# The wrap in the verified tarball must still name the tag we pin; nothing else is downloaded by meson.
grep -q "^revision = $dxtag\$" "$src/subprojects/DirectX-Headers.wrap" || { echo "Mesa wrap and deps.pin disagree on DirectX-Headers" >&2; exit 1; }
dx=$src/subprojects/DirectX-Headers-1.0
if [[ ! -d $dx/.git ]]; then
  rm -rf "$dx"; git clone -q --depth 1 --branch "$dxtag" "$dxurl" "$dx"
fi
head=$(git -C "$dx" rev-parse HEAD)
[[ $dxrev =~ ^[0-9a-f]{40}$ && $head == "$dxrev" ]] || { echo "DirectX-Headers $dxtag is $head, deps.pin pins $dxrev" >&2; exit 1; }
cross=$work/mingw-x86_64.cross
link=; extra=
[[ -n ${MCFGTHREAD_LIBDIR:-} ]] && link=", '-L$MCFGTHREAD_LIBDIR'"
[[ -n ${MCFGTHREAD_INCDIR:-} ]] && extra=", '-isystem', '$MCFGTHREAD_INCDIR'"
# libstdc++ built for the mcf threading model includes <mcfgthread/gthr.h> from <thread>; Mesa's u_qsort.cpp uses it.
if [[ -n ${MCFGTHREAD_INCDIR:-} ]]; then
  printf '#include <thread>\nint main(){std::thread t([]{});t.join();}\n' >"$probe/t.cpp"
  x86_64-w64-mingw32-g++ -std=c++17 "${probe_link[@]}" -isystem "$MCFGTHREAD_INCDIR" -c "$probe/t.cpp" -o "$probe/t.o" 2>"$probe/terr" \
    || { cat "$probe/terr" >&2; echo "x86_64-w64-mingw32-g++ cannot compile <thread> with MCFGTHREAD_INCDIR=$MCFGTHREAD_INCDIR" >&2; exit 2; }
fi
cat >"$cross" <<C
[binaries]
c = 'x86_64-w64-mingw32-gcc'
cpp = 'x86_64-w64-mingw32-g++'
ar = 'x86_64-w64-mingw32-ar'
strip = 'x86_64-w64-mingw32-strip'
windres = 'x86_64-w64-mingw32-windres'
[built-in options]
c_args = ['-static-libgcc'$extra]
cpp_args = ['-static-libgcc', '-static-libstdc++'$extra]
c_link_args = ['-static'$link]
cpp_link_args = ['-static'$link]
[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
C
bld=$work/build
if [[ ! -f $bld/build.ninja ]]; then
  meson setup "$bld" "$src" --cross-file "$cross" --wrap-mode=nodownload "${MESON_OPTIONS[@]}" >"$work/setup.log" 2>&1 \
    || { tail -30 "$work/setup.log" >&2; exit 1; }
fi
echo "ok: configured $bld"
(( build )) || exit 0
ninja -C "$bld" -j"${JOBS:-$(nproc)}" src/microsoft/vulkan/vulkan_dzn.dll >"$work/build.log" 2>&1 \
  || { tail -30 "$work/build.log" >&2; exit 1; }
dll=$bld/src/microsoft/vulkan/vulkan_dzn.dll
# Gates on the result: console-safe imports only, no section that is both writable and executable, and the ICD entry
# points the loader looks up by name. dzn imports GDI32/USER32/SHELL32/VERSION for its GDI presentation path and the
# api-ms-win-core-synch/crt contracts; Play routes every one of those (gdi32/shell32 stubs, user32 uwph, version
# wine-pe, api sets system).
bad=$("$objdump" -p "$dll" | awk '/DLL Name:/{print tolower($3)}' | grep -Ev '^(kernel32|ntdll|user32|gdi32|shell32|version|advapi32|ws2_32|ole32)\.dll$|^api-ms-win-' || true)
[[ -z $bad ]] || { echo "vulkan_dzn.dll imports DLLs the console cannot supply: $bad" >&2; exit 1; }
rwx=$("$objdump" -h "$dll" | awk '/^ *[0-9]+ /{n=$2; getline; if ($0 ~ /CODE/ && $0 !~ /READONLY/) print n}')
[[ -z $rwx ]] || { echo "vulkan_dzn.dll has writable code section(s): $rwx" >&2; exit 1; }
exports=$("$objdump" -p "$dll")   # once: `grep -q` on a pipe would SIGPIPE objdump and trip pipefail
for n in vk_icdNegotiateLoaderICDInterfaceVersion vk_icdGetInstanceProcAddr vk_icdGetPhysicalDeviceProcAddr; do
  grep -q " $n\$" <<<"$exports" || { echo "vulkan_dzn.dll does not export $n" >&2; exit 1; }
done
rm -f "$out"/*.dll "$out"/*.json "$out"/SHA256SUMS "$out"/SOURCE.txt "$out"/LICENSE-*
cp "$dll" "$out/vulkan_dzn.dll"
# GNU ld leaves a few zero-length unwind entries that Play's loader (rightly) refuses; drop exactly those from our own output.
python3 -I "$root/src/tools/pe-drop-empty-pdata.py" "$out/vulkan_dzn.dll"
# The loader picks a driver from a JSON manifest; library_path without a directory symbol is used verbatim, so the
# loader's own LoadLibraryW("vulkan_dzn.dll") goes through Play's guest search (the runtime directory). Mesa's own
# generated manifest names a build-tree path, which is why this one is written here.
cat >"$out/vulkan-dzn.json" <<J
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "vulkan_dzn.dll",
    "api_version": "1.2.0",
    "library_arch": "64"
  }
}
J
# MIT requires the notice to travel with the binary; the launcher package carries these files next to it.
mkdir -p "$out/patches"; cp "${patches[@]}" "$out/patches/"
cp "$src/docs/license.rst" "$out/LICENSE-Mesa.rst"; cp "$dx/LICENSE" "$out/LICENSE-DirectX-Headers"
( cd "$out" && sha256sum -- *.dll >SHA256SUMS )
cat >"$out/SOURCE.txt" <<S
Built from Mesa source $(basename "$url") (sha256 $sha), MIT, source: $url
DirectX-Headers $dxrev ($head), MIT, $dxurl
Build: $(x86_64-w64-mingw32-gcc --version | head -1); meson options: ${MESON_OPTIONS[*]}
Modified: yes, patches ${patches[*]##*/} (sha256 $(cat "${patches[@]}" | sha256sum | cut -c1-16)); distributed in the launcher package as patches/
Recipe sha256 $recipe
This DLL is not part of the xboxnative repository; rebuild it with src/tools/build-mesa-dzn.sh.
S
echo "ok: $out"; cat "$out/SHA256SUMS"
