#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Static contract for the d3d11 fixture (off-device, no Wine, source only).
# ExecuteCommandList(list, FALSE) returns the immediate context to its defaults,
# so every ExecuteCommandList(..., FALSE) in the fixture must be followed by
# rebind_pipeline_state(...) before the next draw, and that helper must set
# every state the fixture uses. A Wine run cannot show the capture: the
# stream-out pass is gated off under Wine, so this checks source order only.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
f="$here/play-d3d11.cpp"
fail(){ echo "FAIL $*"; exit 1; }

mapfile -t resets < <(grep -n -- '->ExecuteCommandList(.*FALSE)' "$f" | cut -d: -f1)
(( ${#resets[@]} > 0 )) || fail "no ExecuteCommandList(..., FALSE) found; the contract has nothing to check"

def=$(grep -n '^static void rebind_pipeline_state(' "$f" | cut -d: -f1 || true)
[[ -n $def ]] || fail "rebind_pipeline_state helper is not defined"
end=$(awk -v s="$def" 'NR>s && /^}/ {print NR; exit}' "$f")
body=$(sed -n "${def},${end}p" "$f")
for call in IASetInputLayout IASetVertexBuffers IASetIndexBuffer IASetPrimitiveTopology \
            VSSetShader GSSetShader PSSetShader RSSetViewports RSSetState OMSetRenderTargets \
            OMSetBlendState OMSetDepthStencilState VSSetShaderResources PSSetShaderResources \
            VSSetSamplers PSSetSamplers VSSetConstantBuffers PSSetConstantBuffers; do
    grep -q "ctx->$call(" <<<"$body" || fail "rebind_pipeline_state does not set $call"
done

for r in "${resets[@]}"; do
    rebind=$(awk -v s="$r" 'NR>s && /rebind_pipeline_state\(/ && !/^static/ {print NR; exit}' "$f")
    draw=$(awk -v s="$r" 'NR>s && /(Draw|DrawIndexed|DrawInstanced|DrawInstancedIndirect|DrawIndexedInstanced)\(/ {print NR; exit}' "$f")
    [[ -n $rebind ]] || fail "ExecuteCommandList(FALSE) at line $r is never followed by rebind_pipeline_state"
    if [[ -n $draw ]] && (( draw < rebind )); then
        fail "a draw (line $draw) comes before rebind_pipeline_state (line $rebind) after the reset at line $r"
    fi
done

grep -q 'CreateGeometryShaderWithStreamOutput(bytecode,bytecodeSize' "$f" || fail "stream-out routes do not use the SO shader creation API"
grep -q 'soDecl,2,&soStride,1,D3D11_SO_NO_RASTERIZED_STREAM' "$f" || fail "stream-out routes do not provide an explicit output stride"
grep -q 'runSoRoute("vs-route",d3d10_vs,sizeof d3d10_vs,false' "$f" && grep -q 'runSoRoute("gs-route",d3d10_gs,sizeof d3d10_gs,true' "$f" || fail "stream-out VS-bytecode or real-GS route is missing"
grep -q 'D3D11_QUERY_SO_STATISTICS' "$f" || fail "stream-out fixture does not verify written primitives"
grep -q 'soStats.NumPrimitivesWritten!=1||soStats.PrimitivesStorageNeeded!=1' "$f" || fail "stream-out fixture does not require each route's output statistics"
grep -q 'written=%I64u needed=%I64u' "$f" || fail "stream-out diagnostics omit query counts"
grep -q 'context->DrawAuto();' "$f" || fail "stream-out fixture does not consume each captured vertex count with DrawAuto"
grep -q 'soDesc.BindFlags=D3D11_BIND_STREAM_OUTPUT|D3D11_BIND_VERTEX_BUFFER' "$f" || fail "stream-out buffer read back by DrawAuto lacks D3D11_BIND_VERTEX_BUFFER"
grep -q 'PLAY-D3D11 SO %s create PASS' "$f" && grep -q 'PLAY-D3D11 SO %s stats PASS' "$f" && grep -q 'PLAY-D3D11 SO %s capture ' "$f" || fail "stream-out stage PASS lines are missing"
echo "PASS ${#resets[@]} ExecuteCommandList(FALSE) call(s) each followed by rebind_pipeline_state before the next draw; both stream-out routes use explicit stride, SO statistics and capture checks"
