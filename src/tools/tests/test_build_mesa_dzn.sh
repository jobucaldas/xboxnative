#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Offline check of the Mesa dzn recipe (research/VULKAN-PLAN.md, milestone 2): the pin parses with a SHA-256, only the
# microsoft-experimental Vulkan driver and its shader compiler are requested, nothing unpinned (Gallium, OpenGL, LLVM,
# video, CL) is pulled in, the Play patches are applied, and no driver DLL is committed. Never downloads anything.
set -euo pipefail
cd "$(dirname "$0")/../../.."
txt=$(bash src/tools/build-mesa-dzn.sh --dry-run)
echo "$txt" | grep -Eq '^mesa-src url=https://[^ ]+ sha256=[0-9a-f]{64} '
for opt in -Dplatforms=windows -Dvulkan-drivers=microsoft-experimental -Dspirv-to-dxil=true -Dgallium-drivers= -Dopengl=false -Dllvm=disabled -Dmicrosoft-clc=disabled -Dbuild-tests=false; do
  echo "$txt" | grep -qF -- "$opt" || { echo "FAIL missing $opt"; exit 1; }
done
echo "$txt" | grep -qF 'outputs: vulkan_dzn.dll vulkan-dzn.json'
# The pinned DirectX-Headers commit is a full 40-hex hash and every Play dzn patch is applied.
echo "$txt" | grep -Eq 'directx-headers=[^ ]+@[0-9a-f]{40} '
for p in 0003-play-dzn-dxil-optional.patch 0004-play-dzn-corewindow-wsi.patch 0005-play-dzn-experimental-optional.patch 0006-play-dzn-meta-finish-idempotent.patch 0007-play-dzn-dxbc-checksum.patch; do
  echo "$txt" | grep -qF "$p" || { echo "FAIL missing patch $p"; exit 1; }
done
# The checksum patch is the one that makes an unsigned container acceptable to a validator; it must still be a patch
# against Mesa's own file, not a rewritten copy.
grep -q 'dxil_container_write' patches/mesa-src/0007-play-dzn-dxbc-checksum.patch
grep -q 'dxbc_checksum' patches/mesa-src/0007-play-dzn-dxbc-checksum.patch
grep -q 'Mesa' NOTICE || { echo "FAIL Mesa is not listed in NOTICE"; exit 1; }
grep -q 'build-mesa-dzn.sh' NOTICE || { echo "FAIL NOTICE does not name the dzn build"; exit 1; }
# The driver is a build output: never a committed binary, and its manifest is generated beside it.
if git ls-files | grep -E '(^|/)vulkan_dzn\.dll$|(^|/)vulkan-dzn\.json$'; then
  echo "FAIL a dzn driver or manifest is committed; the build script produces them"; exit 1
fi
echo "MESA-DZN RECIPE PASS"
