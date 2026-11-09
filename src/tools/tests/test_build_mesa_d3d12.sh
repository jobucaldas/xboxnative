#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Offline check of the Mesa d3d12 recipe: the pin parses with a SHA-256, the d3d12 driver and WGL target are
# requested, and nothing unpinned (LLVM, GLES/EGL, Vulkan, video) is pulled in. Never downloads anything.
set -euo pipefail
cd "$(dirname "$0")/../../.."
txt=$(bash src/tools/build-mesa-d3d12.sh --dry-run)
echo "$txt" | grep -Eq '^mesa-src url=https://[^ ]+ sha256=[0-9a-f]{64} '
for opt in -Dplatforms=windows -Dgallium-drivers=d3d12,softpipe -Dgallium-d3d12-graphics=enabled -Dllvm=disabled -Dvulkan-drivers= -Degl=disabled -Dglx=disabled; do
  echo "$txt" | grep -qF -- "$opt" || { echo "FAIL missing $opt"; exit 1; }
done
echo "$txt" | grep -qF 'outputs: opengl32.dll libgallium_wgl.dll'
# The pinned DirectX-Headers commit is a full 40-hex hash and both Play patches are applied.
echo "$txt" | grep -Eq 'directx-headers=[^ ]+@[0-9a-f]{40} '
echo "$txt" | grep -qF 'patches: 0001-play-corewindow-wgl.patch 0002-play-d3d12-command-queue-fallback.patch'
for opt in -Dzlib=disabled; do echo "$txt" | grep -qF -- "$opt" || { echo "FAIL missing $opt"; exit 1; }; done
grep -q 'Mesa' NOTICE || { echo "FAIL Mesa is not listed in NOTICE"; exit 1; }
echo "MESA-D3D12 RECIPE PASS"
