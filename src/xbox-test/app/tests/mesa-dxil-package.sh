#!/usr/bin/env bash
# Offline contract: Mesa's d3d12 driver must have its pinned DXIL validator beside it.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
fetch="$root/src/tools/fetch-dxil.sh"
build="$root/src/xbox-test/app/build.sh"
pin=$(grep -E '^dxc-dxil[[:space:]]' "$root/deps.pin")
[[ $pin == *'dxc_2025_07_14.zip'*'9ad895a6b039e3a8f8c22a1009f866800b840a74b50db9218d13319e215ea8a4'* ]] || {
  echo "FAIL DXC archive is not pinned to the reviewed release" >&2; exit 1;
}
grep -Fq 'bin/x64/dxil.dll' "$fetch" || { echo "FAIL fetch does not select x64 dxil.dll" >&2; exit 1; }
grep -Fq 'LICENSE-MS.txt' "$fetch" || { echo "FAIL fetch does not retain the Microsoft license" >&2; exit 1; }
grep -Fq 'sha256sum -c -' "$fetch" || { echo "FAIL fetch does not verify the archive hash" >&2; exit 1; }
grep -Fq 'bash "$here/../../tools/fetch-dxil.sh" "$dxil"' "$build" || {
  echo "FAIL launcher build does not fetch the DXIL validator" >&2; exit 1;
}
grep -Fq 'cp "$dxil"/{dxil.dll,LICENSE-MS.txt,SOURCE.txt,SHA256SUMS-dxil} "$out/layout/runtime/"' "$build" || {
  echo "FAIL launcher does not package the validator with its license and provenance" >&2; exit 1;
}
echo "PASS Mesa DXIL validator is pinned, verified, licensed, and packaged"
