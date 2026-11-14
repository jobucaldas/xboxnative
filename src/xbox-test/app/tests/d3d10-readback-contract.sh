#!/usr/bin/env bash
set -euo pipefail
source_file="$(dirname "$0")/play-d3d10-triangle.cpp"
for token in \
  'CreateTexture2D-staging' 'CreateRenderTargetView' \
  'logTextureDesc(stage,"staging",desc)' 'logReadbackHr(stage,"Map",hr)' \
  'm.RowPitch' 'logCopy(stage,"begin")' 'logCopy(stage,"returned")' \
  'static void logPixel(' 'OutputDebugStringA(line)' \
  'readPixel("draw-center"' 'readPixel("indexed-center"' \
  'readPixel("geometry-center"' 'readStaged("mip-generated"' \
  'readStaged("msaa-resolve"' 'readPixel("compiled-center"' \
  'logSoStage("create",true)' 'logSoStage("stats",true)' \
  'logSoStage("capture",true)' 'logSoStage("create",false)' \
  'logSoStage("stats",false)' 'logSoStage("capture",false)' \
  'logSoStats(stats)' 'logSoFloats(written)' \
  'return 51;' 'return 52;' 'return 53;' \
  'stats.NumPrimitivesWritten!=1||stats.PrimitivesStorageNeeded!=1' \
  'written[v*8+c]!=expected[v][c]' 'return 43;   // BLOCKED' 'return 42;'; do
  if ! grep -Fq -- "$token" "$source_file"; then
    printf 'FAIL missing D3D10 readback contract: %s\n' "$token" >&2
    exit 1
  fi
done
printf 'PASS D3D10 readback diagnostics and preserved pass conditions\n'
