#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the pinned Linux MSIX packer, with packaging and XML validation enabled.
# Output stays in ignored directories; no third-party binary is committed.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
source_dir=${MSIX_SOURCE_DIR:-$root/third_party/msix-packaging}
build_dir=${MSIX_BUILD_DIR:-$root/out/msix-packaging}
jobs=${JOBS:-4}
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'JOBS must be a positive integer' >&2; exit 2; }
[[ $(uname -s) == Linux ]] || { echo 'This setup script builds the Linux host tool only' >&2; exit 2; }
for tool in git cmake ninja pkg-config python3 "${CC:-clang}" "${CXX:-clang++}"; do
    command -v "$tool" >/dev/null || { echo "Missing build prerequisite: $tool" >&2; exit 2; }
done
pkg-config --exists icu-uc || { echo 'Missing ICU development files (pkg-config icu-uc)' >&2; exit 2; }
icu_include=$(pkg-config --variable=includedir icu-uc)
icu_lib=$(pkg-config --variable=libdir icu-uc)
# Some split-output .pc files keep libdir in the dev output, but prefix in runtime.
if [[ ! -f $icu_lib/libicuuc.so ]]; then
    icu_lib="$(pkg-config --variable=prefix icu-uc)/lib"
fi
# Explicit paths also support split-output distributions such as NixOS.
for file in "$icu_include/unicode/utypes.h" "$icu_lib/libicuuc.so" "$icu_lib/libicudata.so"; do
    [[ -f $file ]] || { echo "Missing ICU development input: $file" >&2; exit 2; }
done
bash "$root/src/tools/fetch-dep.sh" msix-packaging "$source_dir"
# Apply only the known CMake compatibility edits, preserving upstream CRLF/LF.
# Accept an already-patched cache, but refuse other local edits to these files.
python3 - "$source_dir" <<'PY'
from pathlib import Path
import subprocess
import sys
source = Path(sys.argv[1])
comments = {
    'CMakeLists.txt': (
        b'# specify that this binary is to be built with C++14',
        b'# ICU 76 headers require C++17.'),
    'lib/xerces/CMakeLists.txt': (
        b'# Try C++14, then fall back to C++11 and C++98.  Used for feature tests\n# for optional features.',
        b'# ICU 76 headers require C++17, including feature tests.'),
}
for name, (old_comment, new_comment) in comments.items():
    original = subprocess.check_output(['git', '-C', str(source), 'show', f'HEAD:{name}'])
    if original.count(b'set(CMAKE_CXX_STANDARD 14)') != 1 or original.count(old_comment) != 1:
        sys.exit(f'Unexpected pinned CMake input: {name}')
    patched = original.replace(b'set(CMAKE_CXX_STANDARD 14)', b'set(CMAKE_CXX_STANDARD 17)').replace(old_comment, new_comment)
    path = source / name
    current = path.read_bytes()
    if current not in (original, patched):
        sys.exit(f'Refusing to overwrite local CMake edits: {name}')
    if current != patched:
        path.write_bytes(patched)
PY
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DLINUX=ON -DMSIX_PACK=ON -DUSE_VALIDATION_PARSER=ON \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
    -DICU_INCLUDE_DIR="$icu_include" \
    -DICU_UC_LIBRARY_RELEASE="$icu_lib/libicuuc.so" \
    -DICU_DATA_LIBRARY_RELEASE="$icu_lib/libicudata.so"
cmake --build "$build_dir" --target makemsix --parallel "$jobs"
[[ -x $build_dir/bin/makemsix ]] || { echo 'Build did not produce makemsix' >&2; exit 1; }
{
    printf 'msix-packaging %s\n' "$(git -C "$source_dir" rev-parse HEAD)"
    sha256sum "$root/src/tools/build-msix-packaging.sh"
} > "$build_dir/SOURCE.txt"
git -C "$source_dir" diff HEAD -- CMakeLists.txt lib/xerces/CMakeLists.txt > "$build_dir/compatibility.patch"
echo "Built pinned packer with XML validation; add $build_dir/bin to PATH"
