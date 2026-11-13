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
echo "PASS ${#resets[@]} ExecuteCommandList(FALSE) call(s) each followed by rebind_pipeline_state before the next draw; the helper sets all states the fixture uses"
