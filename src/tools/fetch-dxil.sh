#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# fetch-dxil.sh [outdir] -> dxil.dll, LICENSE-MS.txt, SOURCE.txt, SHA256SUMS-dxil
# Fetches only the x64 DXIL validator from the pinned Microsoft DXC release. The archive is
# SHA-256 verified; the Microsoft binary is never committed. build.sh packages it only with Mesa.
# Needs: curl, sha256sum, python3. Cache: third_party/dxc-dxil/<sha prefix>/ (git-ignored).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=${1:-$root/out/dxil}
for t in curl sha256sum python3; do command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }; done
read -r _ url sha < <(grep -E '^dxc-dxil[[:space:]]' "$root/deps.pin") || { echo "dxc-dxil missing from deps.pin" >&2; exit 2; }
work=$root/third_party/dxc-dxil/${sha:0:16}
archive=$work/$(basename "$url")
mkdir -p "$work" "$out"
if [[ ! -f $archive ]]; then curl -fsSL --proto '=https' -o "$archive.part" "$url"; mv "$archive.part" "$archive"; fi
echo "$sha  $archive" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch for $archive; deleting" >&2; rm -f "$archive"; exit 1; }
python3 -I - "$archive" "$out" <<'PY'
import pathlib, sys, zipfile
archive, output = sys.argv[1:]
with zipfile.ZipFile(archive) as package:
    for member, name in (("bin/x64/dxil.dll", "dxil.dll"),
                         ("LICENSE-MS.txt", "LICENSE-MS.txt")):
        info = package.getinfo(member)
        if info.file_size > 16 * 1024 * 1024:
            raise SystemExit("unexpectedly large DXC package member: " + member)
        pathlib.Path(output, name).write_bytes(package.read(member))
PY
( cd "$out" && sha256sum -- dxil.dll >SHA256SUMS-dxil )
printf 'DXC v1.8.2505.1 %s\nsha256 %s\nlicense Microsoft software license terms (LICENSE-MS.txt)\nsource https://github.com/microsoft/DirectXShaderCompiler\n' \
  "$url" "$sha" > "$out/SOURCE.txt"
echo "DXIL validator dxil.dll, LICENSE-MS.txt -> $out"
