#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Static contract for the d3d12 fixture (off-device, no Wine): the debug layer
# is enabled before the first device creation. ID3D12Debug::EnableDebugLayer
# must be called before the device exists; calling it after removes the device
# (Microsoft docs, ID3D12Debug::EnableDebugLayer remarks). The console run that
# enabled it after creation failed every later call with DXGI_ERROR_DEVICE_REMOVED.
# This checks source order only; it does not prove device behaviour (Wine
# reports no debug layer, so it cannot reproduce the removal).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
f="$here/play-d3d12.cpp"
enable=$(grep -n 'EnableDebugLayer()' "$f" | head -1 | cut -d: -f1 || true)
create=$(grep -n 'CreateDevice(nullptr' "$f" | head -1 | cut -d: -f1 || true)
if [[ -z $enable || -z $create ]]; then
    echo "FAIL play-d3d12.cpp: EnableDebugLayer or the first CreateDevice call not found"
    exit 1
fi
if (( enable > create )); then
    echo "FAIL EnableDebugLayer at line $enable comes after the first device creation at line $create"
    exit 1
fi
echo "PASS EnableDebugLayer at line $enable precedes the first device creation at line $create"
