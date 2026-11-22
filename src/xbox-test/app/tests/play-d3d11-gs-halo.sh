#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Static contract for the d3d11 fixture's GS-HALO pass (off-device, no Wine, source only).
# Wine's GL-backed d3d11 cannot be trusted on a CI runner (see the wine-diag-suites
# note in ci.yml), so this pins what can be checked deterministically:
#  - the embedded halo_gs bytes in triangle_bytecode.h are exactly what embed.py
#    regenerates from the checked-in .dxbc files (no stale embed);
#  - gs_halo.hlsl keeps the declaration shape the Halo scan reported (maxvertexcount(4),
#    point input, TriangleStream, SV_RenderTargetArrayIndex);
#  - the fixture runs GS-HALO after the basic GS pass with the halo shader, one
#    DrawInstanced, its own exit codes, and still reaches it before the Wine verdict.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
f="$here/play-d3d11.cpp"
shaders="$here/../../uwph/host/shaders"
fail(){ echo "FAIL $*"; exit 1; }

work=$(mktemp -d "${TMPDIR:-/tmp}/gs-halo.XXXXXX")
trap 'rm -rf "$work"' EXIT
cp "$shaders"/embed.py "$shaders"/*.dxbc "$work"/
python3 -I "$work/embed.py"
cmp -s "$work/triangle_bytecode.h" "$shaders/triangle_bytecode.h" \
    || fail "triangle_bytecode.h differs from what embed.py generates from the checked-in .dxbc files"
[[ $(head -c4 "$shaders/halo_gs.dxbc") == DXBC ]] || fail "halo_gs.dxbc is not a DXBC container"

h="$shaders/gs_halo.hlsl"
grep -q '\[maxvertexcount(4)\]' "$h" || fail "gs_halo.hlsl lost maxvertexcount(4)"
grep -q 'point HaloGSIO vin\[1\]' "$h" || fail "gs_halo.hlsl lost its point input"
grep -q 'inout TriangleStream<HaloGSOut>' "$h" || fail "gs_halo.hlsl lost its TriangleStream output"
grep -q 'SV_RenderTargetArrayIndex' "$h" || fail "gs_halo.hlsl lost the SV_RenderTargetArrayIndex output"

line(){ grep -n -- "$1" "$f" | head -1 | cut -d: -f1 || true; }
gs_pass=$(line 'PLAY-D3D11 GS PASS')
create=$(line 'CreateGeometryShader(halo_gs,sizeof halo_gs')
set_gs=$(line 'GSSetShader(haloGs,')
draw=$(line 'DrawInstanced(1,1,0,0)')
unset_gs=$(awk -v s="$draw" 'NR>s && /GSSetShader\(nullptr,nullptr,0\)/ {print NR; exit}' "$f")
readback=$(line 'GS-HALO FAIL readback-map')
halo_pass=$(line 'PLAY-D3D11 GS-HALO PASS')
wine_verdict=$(line 'BLOCKED wine-host')
for v in gs_pass create set_gs draw unset_gs readback halo_pass wine_verdict; do
    [[ -n ${!v} ]] || fail "fixture is missing the GS-HALO step: $v"
done
prev=0
for v in gs_pass create set_gs draw unset_gs readback halo_pass wine_verdict; do
    (( ${!v} > prev )) || fail "GS-HALO step $v (line ${!v}) is out of order"
    prev=${!v}
done

grep -q 'PLAY-D3D11 GS-HALO FAIL create' "$f" && grep -q 'PLAY-D3D11 GS-HALO FAIL black-readback' "$f" \
    || fail "GS-HALO failure stages are not logged"
# Each failing stage keeps a code no other stage returns, so a verdict names the step.
codes=$(sed -n "${create},${halo_pass}p" "$f" | grep -o 'return [0-9]*;' | sort | uniq -c | awk '$1>1{print $3}')
[[ -z $codes ]] || fail "GS-HALO stages reuse an exit code: $codes"
echo "PASS gs_halo embed matches embed.py, HLSL keeps the Halo scan shape, fixture runs GS-HALO (create, DrawInstanced(1,1,0,0), readback, PASS) after GS PASS and before the Wine verdict"
