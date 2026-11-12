#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Offline check of the Mesa dzn recipe (issue #177, milestone 2): the pin parses with a SHA-256, only the
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
for p in 0003-play-dzn-dxil-optional.patch 0004-play-dzn-corewindow-wsi.patch 0005-play-dzn-experimental-optional.patch 0006-play-dzn-meta-finish-idempotent.patch 0007-play-dzn-dxbc-checksum.patch 0008-play-dzn-wsi-logging.patch 0009-play-dzn-wsi-hwnd-only.patch; do
  echo "$txt" | grep -qF "$p" || { echo "FAIL missing patch $p"; exit 1; }
done
# The swapchain path is decided by whether a DirectComposition device is kept, and that decision was invisible from the
# guest. 0008 logs every step of it; 0009 makes the HWND path the only one, so dzn's swapchain always reaches Play's
# CoreWindow adapter (a composition swapchain is bound to a visual, not to a window).
grep -q 'Play WSI: CreateSwapChainForHwnd' patches/mesa-src/0008-play-dzn-wsi-logging.patch \
  || { echo "FAIL 0008 does not log the HWND swapchain branch"; exit 1; }
grep -q 'DCompositionCreateDevice failed hr=' patches/mesa-src/0008-play-dzn-wsi-logging.patch \
  || { echo "FAIL 0008 does not log the composition device outcome"; exit 1; }
grep -q 'wsi->dxgi.dcomp = NULL;' patches/mesa-src/0009-play-dzn-wsi-hwnd-only.patch \
  || { echo "FAIL 0009 keeps the composition device"; exit 1; }
! grep -q '^+ *wsi->dxgi.dcomp = dcomp_get_device();' patches/mesa-src/0009-play-dzn-wsi-hwnd-only.patch \
  || { echo "FAIL 0009 still stores the composition device"; exit 1; }
# The checksum patch is the one that makes an unsigned container acceptable to a validator; it must still be a patch
# against Mesa's own file, not a rewritten copy.
grep -q 'dxil_container_write' patches/mesa-src/0007-play-dzn-dxbc-checksum.patch
grep -q 'dxbc_checksum' patches/mesa-src/0007-play-dzn-dxbc-checksum.patch
# A tail that does not fit in one block is 56 or 60 bytes (the payload is 4-byte aligned), so the first block is
# padded as a plain block with the whole remainder; copying a fixed 56 bytes drops data when it is 60.
grep -q 'memcpy(last, data + i, rem);' patches/mesa-src/0007-play-dzn-dxbc-checksum.patch \
  && grep -q 'last\[rem\] = 0x80;' patches/mesa-src/0007-play-dzn-dxbc-checksum.patch \
  || { echo "FAIL 0007 two-block padding does not cover the whole remainder"; exit 1; }
! grep -q 'last\[60\] = 0x80;' patches/mesa-src/0007-play-dzn-dxbc-checksum.patch \
  || { echo "FAIL 0007 two-block padding still hardcodes the 56-byte case"; exit 1; }
# The meta cleanup runs twice on a failed device creation, so the mutexes must be initialised/destroyed once.
grep -q 'locks_initialized' patches/mesa-src/0006-play-dzn-meta-finish-idempotent.patch \
  || { echo "FAIL 0006 destroys the blit locks unconditionally"; exit 1; }
grep -q 'Mesa' NOTICE || { echo "FAIL Mesa is not listed in NOTICE"; exit 1; }
grep -q 'build-mesa-dzn.sh' NOTICE || { echo "FAIL NOTICE does not name the dzn build"; exit 1; }
# The driver is a build output: never a committed binary, and its manifest is generated beside it.
if git ls-files | grep -E '(^|/)vulkan_dzn\.dll$|(^|/)vulkan-dzn\.json$'; then
  echo "FAIL a dzn driver or manifest is committed; the build script produces them"; exit 1
fi
echo "MESA-DZN RECIPE PASS"
