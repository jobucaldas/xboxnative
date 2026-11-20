#!/usr/bin/env bash
# Offline DXIL contract: pinned runtime download, verified ZIP extraction, no MSIX redistribution.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
app="$root/src/xbox-test/app"
build="$app/build.sh"
python3 -I - "$root" <<'PY'
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
pin = next(line.split() for line in (root / "deps.pin").read_text().splitlines() if line.startswith("dxc-dxil "))
header = (root / "src/xbox-test/app/dxil_runtime.h").read_text()
constants = {
    "kUrl": pin[1],
    "kArchiveSha256": pin[2],
    "kDllSha256": pin[3],
}
for name, value in constants.items():
    if not re.search(rf'{name}\[\] = "{re.escape(value)}"', header):
        raise SystemExit(f"FAIL deps.pin mismatch for {name}")
build_text = (root / "src/xbox-test/app/build.sh").read_text()
if "fetch-dxil.sh" in build_text or re.search(r"cp[^\n]*(dxil\.dll|LICENSE-MS\.txt)", build_text):
    raise SystemExit("FAIL launcher layout still packages Microsoft's DXIL files")
runtime = (root / "src/xbox-test/app/host/play_runtime.h").read_text()
if not re.search(r'static void prepare_dxil\(\)\s*\{\s*if\(.*?opengl32\.dll.*?\|\|.*?vulkan_dzn\.dll.*?dxil_runtime::ensure\(g_dir', runtime, re.S):
    raise SystemExit("FAIL Vulkan dzn does not share the DXIL download/cache trigger")
print("PASS DXIL URL and archive/DLL hashes match deps.pin")
print("PASS launcher packaging contract excludes dxil.dll and LICENSE-MS.txt")
print("PASS Mesa OpenGL and Vulkan dzn share the DXIL download/cache trigger")
PY

tmp="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/mesa-dxil-runtime-test-$$"
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT
python3 -I - "$tmp/fixture.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w", compression=zipfile.ZIP_DEFLATED) as z:
    z.writestr("bin/x64/dxil.dll", b"fixture DXIL bytes")
    z.writestr("LICENSE-MS.txt", b"fixture Microsoft license")
PY
for source in inflate inftrees inffast adler32 crc32 zutil; do
  cc -std=c11 -I"$app/vendor/zlib" -c "$app/vendor/zlib/$source.c" -o "$tmp/$source.o"
done
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$app/tests/mesa-dxil-zip.cpp" "$tmp"/*.o -o "$tmp/mesa-dxil-zip"
"$tmp/mesa-dxil-zip" "$tmp/fixture.zip"
