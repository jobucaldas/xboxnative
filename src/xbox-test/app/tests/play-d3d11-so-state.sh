#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Static contract for the d3d11 fixture (off-device, no Wine): between the
# deferred pass's ExecuteCommandList(list,FALSE), which resets the immediate
# context to its defaults, and the stream-out draw, the fixture rebinds the
# input layout, the vertex buffer, topology and the vertex and pixel shaders the
# later draws read. Without them the SO buffer stays empty and the pass reports BLOCKED for a fixture
# reason. This checks source order only; the capture itself is console-only
# (Wine's d3d11 captures nothing here, so the stream-out pass is gated off).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
f="$here/play-d3d11.cpp"
reset=$(grep -n 'ExecuteCommandList(list,FALSE)' "$f" | head -1 | cut -d: -f1 || true)
draw=$(grep -n 'SOSetTargets(1,soBinds' "$f" | head -1 | cut -d: -f1 || true)
if [[ -z $reset || -z $draw ]]; then
    echo "FAIL play-d3d11.cpp: the context reset or the stream-out draw was not found"
    exit 1
fi
window=$(sed -n "${reset},${draw}p" "$f")
for call in IASetInputLayout IASetVertexBuffers IASetPrimitiveTopology VSSetShader PSSetShader; do
    if ! grep -q "context->$call(" <<<"$window"; then
        echo "FAIL $call is not rebound between the reset (line $reset) and the stream-out draw (line $draw)"
        exit 1
    fi
done
echo "PASS input layout, vertex buffer, topology and VS are rebound between the reset (line $reset) and the stream-out draw (line $draw)"
