#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Static contract for the d3d12 debug layer (off-device, no Wine, source only):
#   1. the fixture calls ID3D12Debug::EnableDebugLayer exactly once, behind the
#      D3D12GetDebugInterface result, so the default path only enables it when
#      the host answered the interface;
#   2. that enable comes before the first device creation (the documented order);
#   3. the host answers the interface with SDK_COMPONENT_MISSING unless the
#      opt-in file LocalState\d3d12debug.txt exists (default off, so the default
#      console path never reaches EnableDebugLayer).
# This checks source only. It does not prove device behaviour: Wine reports no
# debug layer, so it cannot reproduce anything about the layer.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
app="$here/.."
f="$here/play-d3d12.cpp"
fail(){ echo "FAIL $*"; exit 1; }

calls=$(grep -c 'EnableDebugLayer()' "$f" || true)
[[ $calls == 1 ]] || fail "expected exactly one EnableDebugLayer() call in play-d3d12.cpp, found $calls"
enable=$(grep -n 'EnableDebugLayer()' "$f" | cut -d: -f1)
query=$(grep -n 'hrDebug=D3D12GetDebugInterface(' "$f" | head -1 | cut -d: -f1 || true)
guard=$(grep -n 'if(SUCCEEDED(hrDebug)&&debug)' "$f" | head -1 | cut -d: -f1 || true)
create=$(grep -n 'CreateDevice(nullptr' "$f" | head -1 | cut -d: -f1 || true)
[[ -n $query && -n $guard && -n $create ]] || fail "debug query, guard or first CreateDevice not found"
(( query < guard && guard < enable )) || fail "EnableDebugLayer (line $enable) is not behind the GetDebugInterface result (lines $query, $guard)"
(( enable < create )) || fail "EnableDebugLayer at line $enable comes after the first device creation at line $create"

grep -q 'inline std::atomic<bool> debugLayerOptIn{false};' "$app/host/d3d12_diag.h" \
    || fail "host flag debugLayerOptIn is not declared default-off in host/d3d12_diag.h"
grep -q 'd3d12debug.txt' "$app/main.cpp" || fail "launcher does not read LocalState\\d3d12debug.txt"
grep -q 'return (HRESULT)0x887a002d;' "$app/host/play_runtime.h" \
    || fail "host does not answer SDK_COMPONENT_MISSING when the layer is not opted in"
echo "PASS debug layer enabled only behind the GetDebugInterface result (line $guard), before the first device creation (line $create), host default-off"
