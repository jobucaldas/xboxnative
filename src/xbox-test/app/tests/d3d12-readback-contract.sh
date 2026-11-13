#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Static readback contract for the D3D12 guest fixture (no device or Wine).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
f="$here/play-d3d12.cpp"
fail(){ echo "FAIL $*"; exit 1; }

# D3D12_TEXTURE_DATA_PITCH_ALIGNMENT is 256 in the Windows SDK d3d12.h.
row_bytes=$((64 * 4))
pitch_alignment=256
expected_pitch=$((((row_bytes + pitch_alignment - 1) / pitch_alignment) * pitch_alignment))
expected_total=$((expected_pitch * 64))
[[ $expected_pitch == 256 && $expected_total == 16384 ]] || fail "64x64 RGBA footprint math changed"

for token in \
    'D3D12_HEAP_TYPE_DEFAULT' \
    'D3D12_RESOURCE_STATE_COMMON' \
    'D3D12_HEAP_TYPE_READBACK' \
    'D3D12_RESOURCE_STATE_COPY_DEST' \
    'GetCopyableFootprints(&targetDesc,0,1,0,&readbackFootprint,nullptr,nullptr,&readbackSize)' \
    'readbackDesc.Width=readbackSize' \
    'D3D12_RESOURCE_STATE_COMMON' \
    'D3D12_RESOURCE_STATE_RENDER_TARGET' \
    'D3D12_RESOURCE_STATE_COPY_SOURCE' \
    'list->CopyTextureRegion(&dst,0,0,0,&src,&copyBox)' \
    'readbackFootprint.Footprint.RowPitch' \
    'base+32*pitch+32*4' \
    'base+4*pitch+4*4' \
    'PLAY-D3D12 readback footprint' \
    'PLAY-D3D12 readback pixels inside='; do
    grep -Fq "$token" "$f" || fail "fixture is missing readback contract token: $token"
done

to_draw_before=$(grep -n 'toDraw.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON' "$f" | cut -d: -f1)
to_draw_after=$(grep -n 'toDraw.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET' "$f" | cut -d: -f1)
to_draw_call=$(grep -n 'list->ResourceBarrier(1,&toDraw)' "$f" | head -1 | cut -d: -f1)
to_copy_before=$(grep -n 'toCopy.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET' "$f" | cut -d: -f1)
to_copy_after=$(grep -n 'toCopy.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE' "$f" | cut -d: -f1)
to_copy_call=$(grep -n 'list->ResourceBarrier(1,&toCopy)' "$f" | cut -d: -f1)
pitch=$(grep -n 'GetCopyableFootprints(&targetDesc' "$f" | cut -d: -f1)
copy=$(grep -n 'list->CopyTextureRegion(&dst' "$f" | cut -d: -f1)
signal=$(grep -n 'queue->Signal(fence,fenceValue)' "$f" | head -1 | cut -d: -f1)
wait=$(grep -n 'WaitForSingleObject(event,5000)' "$f" | head -1 | cut -d: -f1)
map=$(grep -n 'readback->Map(0,&readWhole,&mappedRead)' "$f" | cut -d: -f1)
compare=$(grep -n 'if(!insideRed||!outsideBlack)return 43' "$f" | cut -d: -f1)
[[ -n $to_draw_before && -n $to_draw_after && -n $to_draw_call && -n $to_copy_before && -n $to_copy_after && -n $to_copy_call && -n $pitch && -n $copy && -n $signal && -n $wait && -n $map && -n $compare ]] \
    || fail "incomplete readback sequence"
(( to_draw_before < to_draw_after && to_draw_after < to_draw_call \
    && to_copy_before < to_copy_after && to_copy_after < to_copy_call \
    && to_draw_call < to_copy_before && to_copy_call < copy \
    && pitch < copy && copy < signal && signal < wait && wait < map && map < compare )) \
    || fail "barrier, footprint, copy, queue wait, Map, and pixel comparison are out of order"

[[ $(grep -c 'logHr("CreateCommittedResource target"' "$f") == 1 ]] || fail "target creation HRESULT is not logged once"
[[ $(grep -c 'logHr("CreateCommittedResource readback"' "$f") == 1 ]] || fail "readback creation HRESULT is not logged once"
[[ $(grep -c 'logHr("Map readback"' "$f") == 1 ]] || fail "Map HRESULT is not logged once"
[[ $(grep -c 'logHr("Signal"' "$f") == 1 ]] || fail "queue Signal HRESULT is not logged once"
[[ $(grep -c 'readback fence wait result=' "$f") == 1 ]] || fail "fence wait result is not logged"
echo "PASS D3D12 readback uses device footprint pitch, COPY_SOURCE/COPY_DEST states, a completed fence before Map, and reports HRESULTs plus sampled pixels"
