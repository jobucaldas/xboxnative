#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../../../.." && pwd)"
source_file="$root/src/xbox-test/app/host/d3d11_diag.h"
needles=(
    'context->PSGetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,ps);'
    'context->GSGetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,gs);'
    'context->SOGetTargets(4,so);'
    'view=%s resource-dim=%u'
    'bytes=%u bind=0x%x stride=%u'
    'ps-srv={'
    'gs-srv={'
    'so={'
)
for needle in "${needles[@]}"; do
    if ! grep -Fq "$needle" "$source_file"; then
        printf 'FAIL: D3D11 deep draw snapshot is missing: %s\n' "$needle" >&2
        exit 1
    fi
done
printf 'PASS: d3d11-deep-state (effective PS/GS SRV slots and SO targets are captured with resource metadata)\n'
