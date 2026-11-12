#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-vulkan-loader.sh [outdir] [--dry-run] [--build]
# Vulkan for launcher Play (issue #177, milestone 1): the Khronos Vulkan-Loader cross-built for Windows x64
# from the pinned source (`vulkan-loader-src` in deps.pin, SHA-256 verified) against the pinned `vulkan-headers-src`, plus
# a null ICD (src/tools/vulkan/null-icd.c) that reports zero physical devices. Result: vulkan-1.dll (the loader a game
# loads by name), vulkan-null-icd.dll and the ICD manifest that points at it. Both are Apache-2.0; the DLLs are build
# outputs, git-ignored (out/ and *.dll) and never committed.
# Why a null ICD: a Vulkan-optional engine probes LoadLibrary("vulkan-1.dll") and falls back to D3D11 when it fails. On
# Windows without a driver that load fails with ERROR_MOD_NOT_FOUND. With a real loader and no ICD the instance cannot be
# created at all (VK_ERROR_INCOMPATIBLE_DRIVER, "Found no drivers!"); with this ICD vkCreateInstance succeeds and
# vkEnumeratePhysicalDevices answers VK_ERROR_INITIALIZATION_FAILED with a count of zero, which is exactly what a desktop
# Vulkan loader returns on a machine with no GPU, so the engine takes its normal "no Vulkan" path.
# Default: fetch, verify, extract and compile the loader objects. --build also links and copies the DLLs to <outdir>.
# --dry-run only prints the recipe (offline; what CI runs).
# Needs: clang-cl and lld-link (LLVM), llvm-readobj, curl, xz, sha256sum, and the xwin SDK/CRT headers and import
# libraries (XWIN_ROOT, default ~/.cache/uwp-crossbuild/xwin) — the same toolchain the launcher's own build uses.
# The loader is compiled with the desktop API family: its sources call CreateFileW and the cfgmgr32 device-tree CM_*
# family, which WINAPI_FAMILY_APP hides from the headers. The image is still checked to import only DLLs the app
# partition provides (KERNEL32, ADVAPI32, SETUPAPI), and Play maps it itself, so no OS loader involvement is implied.
# The optional unknown-extension trampolines (dev_ext_trampoline.c, phys_dev_ext.c) need a MASM assembler; like the
# upstream CMake build without one, they are left out. Cache/scratch dir: third_party/vulkan-loader-build/<pin hash>/.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=; dry=0; build=0
for a in "$@"; do case "$a" in --dry-run) dry=1 ;; --build) build=1 ;; *) out=$a ;; esac; done
out=${out:-$root/out/vulkan-loader}
xwin=${XWIN_ROOT:-$HOME/.cache/uwp-crossbuild/xwin}
read -r _ url sha < <(grep -E '^vulkan-loader-src[[:space:]]' "$root/deps.pin") || { echo "vulkan-loader-src missing from deps.pin" >&2; exit 2; }
read -r _ hurl hsha < <(grep -E '^vulkan-headers-src[[:space:]]' "$root/deps.pin") || { echo "vulkan-headers-src missing from deps.pin" >&2; exit 2; }
icd=$root/src/tools/vulkan/null-icd.c
icddef=$root/src/tools/vulkan/null-icd.def
# Loader sources: upstream NORMAL_LOADER_SRCS for Windows, minus the OPT_LOADER_SRCS assembly trampolines (see header).
SOURCES=(allocation cJSON debug_utils extension_manual loader_environment gpa_helper loader log loader_json settings
         terminator trampoline unknown_function_handling wsi loader_windows dirent_on_windows)
DEFS=(/DWIN32_LEAN_AND_MEAN /DNDEBUG /DVK_USE_PLATFORM_WIN32_KHR /DVK_ENABLE_BETA_EXTENSIONS)
echo "vulkan-loader-src url=$url sha256=$sha"
echo "vulkan-headers-src url=$hurl sha256=$hsha"
echo "compile: clang-cl /c /MT /O2 /w ${DEFS[*]} sources=${SOURCES[*]}"
echo "link: lld-link /dll /nodefaultlib libcmt.lib libvcruntime.lib libucrt.lib onecoreuap.lib"
echo "outputs: vulkan-1.dll vulkan-null-icd.dll vulkan-null-icd.json"
(( dry )) && exit 0
for t in clang-cl lld-link llvm-readobj curl xz sha256sum; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }
done
[[ -d $xwin/crt/include && -d $xwin/sdk/include/um ]] || { echo "no xwin SDK/CRT at $xwin (set XWIN_ROOT)" >&2; exit 2; }
base=$root/third_party/vulkan-loader-build
# Source pins, the ICD sources and the toolchain invalidate the tree; packaging edits to this script do not.
recipe=$(printf '%s\n' "$sha" "$hsha" "$(sha256sum "$icd" "$icddef" | sha256sum)" "${DEFS[*]}" "${SOURCES[*]}" \
  "$(clang-cl --version | head -1)" "$(lld-link --version | head -1)" | sha256sum | cut -c1-16)
work=$base/${sha:0:16}-$recipe
mkdir -p "$base" "$work" "$out"
# Both tarballs are named after the same release tag, so each is stored under its pin name.
fetch() {   # fetch <pin> <url> <sha256> -> <base>/<pin>.tar.gz, SHA-256 verified before anything extracts it
  local pin=$1 u=$2 s=$3 tar=$base/$1.tar.gz
  if [[ ! -f $tar ]]; then curl -fsSL --proto '=https' -o "$tar.part" "$u"; mv "$tar.part" "$tar"; fi
  echo "$s  $tar" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $tar; deleting" >&2; rm -f "$tar"; exit 1; }
  printf '%s\n' "$tar"
}
ltar=$(fetch vulkan-loader-src "$url" "$sha")
htar=$(fetch vulkan-headers-src "$hurl" "$hsha")
# The extracted directory is named after the release tag inside each tarball (sed, not head: no SIGPIPE under pipefail).
top() { tar -tzf "$1" | sed -n '1s,/.*,,p'; }
lsrc=$work/$(top "$ltar")
hsrc=$work/$(top "$htar")
if [[ ! -f $work/.extracted ]]; then   # marker written only after both complete extracts; an interrupted one is redone
  rm -rf "$lsrc" "$hsrc" "$work/obj"
  tar -C "$work" -xf "$ltar"
  tar -C "$work" -xf "$htar"
  touch "$work/.extracted"
fi
inc=(/I"$xwin/crt/include" /I"$xwin/sdk/include/ucrt" /I"$xwin/sdk/include/um" /I"$xwin/sdk/include/shared")
libs=(/libpath:"$xwin/crt/lib/x86_64" /libpath:"$xwin/sdk/lib/um/x86_64" /libpath:"$xwin/sdk/lib/ucrt/x86_64")
mkdir -p "$work/obj"
for f in "${SOURCES[@]}"; do
  clang-cl --target=x86_64-pc-windows-msvc /c /MT /O2 /w "${DEFS[@]}" "${inc[@]}" \
    -I"$lsrc/loader" -I"$lsrc/loader/generated" -I"$hsrc/include" \
    "$lsrc/loader/$f.c" /Fo"$work/obj/$f.obj" || { echo "compile failed: $f" >&2; exit 1; }
done
echo "ok: compiled ${#SOURCES[@]} loader objects in $work/obj"
(( build )) || exit 0
link() { lld-link-19 "$@" 2>/dev/null || lld-link "$@"; }
rm -rf "$work/bin"; mkdir -p "$work/bin"
link /dll /nodefaultlib /out:"$work/bin/vulkan-1.dll" /def:"$lsrc/loader/vulkan-1.def" "${libs[@]}" \
  "$work"/obj/*.obj libcmt.lib libvcruntime.lib libucrt.lib onecoreuap.lib \
  || { echo "link failed: vulkan-1.dll" >&2; exit 1; }
clang-cl --target=x86_64-pc-windows-msvc /c /MT /O2 /w "${inc[@]}" -I"$hsrc/include" "$icd" /Fo"$work/obj/null-icd.obj" \
  || { echo "compile failed: null-icd.c" >&2; exit 1; }
link /dll /nodefaultlib /out:"$work/bin/vulkan-null-icd.dll" /def:"$icddef" "${libs[@]}" \
  "$work/obj/null-icd.obj" libcmt.lib libvcruntime.lib libucrt.lib onecoreuap.lib \
  || { echo "link failed: vulkan-null-icd.dll" >&2; exit 1; }
# Gates on the result: only DLLs the app partition provides, no section that is both writable and executable, and the
# ICD exports the four entry points the loader looks up by name.
for f in "$work/bin/vulkan-1.dll" "$work/bin/vulkan-null-icd.dll"; do
  bad=$(llvm-readobj --coff-imports "$f" | awk '/^  Name: /{print tolower($2)}' | sort -u | grep -Ev '^(kernel32|advapi32|setupapi)\.dll$' || true)
  [[ -z $bad ]] || { echo "$(basename "$f") imports DLLs the app partition cannot supply: $bad" >&2; exit 1; }
  rwx=$(llvm-readobj --sections "$f" | awk '/^  Name: /{n=$2} /Characteristics \[/{if ($0 ~ /IMAGE_SCN_MEM_WRITE/ && $0 ~ /IMAGE_SCN_MEM_EXECUTE/) print n}')
  [[ -z $rwx ]] || { echo "$(basename "$f") has writable code section(s): $rwx" >&2; exit 1; }
done
for n in vk_icdNegotiateLoaderICDInterfaceVersion vk_icdGetInstanceProcAddr vk_icdGetPhysicalDeviceProcAddr \
         vk_icdEnumerateAdapterPhysicalDevices; do
  llvm-readobj --coff-exports "$work/bin/vulkan-null-icd.dll" | grep -q "Name: $n\$" \
    || { echo "vulkan-null-icd.dll does not export $n" >&2; exit 1; }
done
rm -f "$out"/*.dll "$out"/*.json "$out"/SHA256SUMS "$out"/SOURCE.txt "$out"/LICENSE-*
cp "$work/bin/vulkan-1.dll" "$work/bin/vulkan-null-icd.dll" "$out/"
# The loader picks a driver from a JSON manifest; library_path without a directory symbol is used verbatim, so the
# loader's own LoadLibraryW("vulkan-null-icd.dll") goes through Play's guest search (the runtime directory).
cat >"$out/vulkan-null-icd.json" <<J
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "vulkan-null-icd.dll",
    "api_version": "1.0.0",
    "library_arch": "64"
  }
}
J
# Apache-2.0 requires the notice to travel with the binaries; the launcher package carries these files beside them.
cp "$lsrc/LICENSE.txt" "$out/LICENSE-Vulkan-Loader.txt"
cp "$hsrc/LICENSE.md" "$out/LICENSE-Vulkan-Headers.md"
( cd "$out" && sha256sum -- *.dll >SHA256SUMS )
cat >"$out/SOURCE.txt" <<S
Built from Vulkan-Loader source $lsrc (sha256 $sha), Apache-2.0, source: $url
Vulkan-Headers $hsrc (sha256 $hsha), Apache-2.0, source: $hurl
Null ICD: src/tools/vulkan/null-icd.c (GPL-3.0-or-later, this repository), reports zero physical devices.
Build: $(clang-cl --version | head -1); sources: ${SOURCES[*]}; defines: ${DEFS[*]}
Modified: no (upstream sources unpatched; the ICD is our own driver)
Recipe sha256 $recipe
These DLLs are not part of the xboxnative repository; rebuild them with src/tools/build-vulkan-loader.sh.
S
echo "ok: $out"; cat "$out/SHA256SUMS"
