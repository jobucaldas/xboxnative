#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Offline check of the Vulkan loader recipe: both pins parse with a SHA-256, the loader is compiled from the pinned
# sources with the console's static CRT, the null ICD is built from this repository, and nothing is committed as a
# binary. Never downloads anything.
set -euo pipefail
cd "$(dirname "$0")/../../.."
txt=$(bash src/tools/build-vulkan-loader.sh --dry-run)
echo "$txt" | grep -Eq '^vulkan-loader-src url=https://[^ ]+ sha256=[0-9a-f]{64}$'
echo "$txt" | grep -Eq '^vulkan-headers-src url=https://[^ ]+ sha256=[0-9a-f]{64}$'
echo "$txt" | grep -qF '/MT'
echo "$txt" | grep -qF '/DVK_USE_PLATFORM_WIN32_KHR'
echo "$txt" | grep -qF 'onecoreuap.lib'
echo "$txt" | grep -qF 'outputs: vulkan-1.dll vulkan-null-icd.dll vulkan-null-icd.json'
# The null ICD is our own source, never a fetched binary, and the loader has no committed DLL.
test -f src/tools/vulkan/null-icd.c
test -f src/tools/vulkan/null-icd.def
grep -q 'vk_icdNegotiateLoaderICDInterfaceVersion' src/tools/vulkan/null-icd.def
if git ls-files | grep -E '(^|/)vulkan-1\.dll$|vulkan-null-icd\.dll$'; then
  echo "FAIL a Vulkan DLL is committed; the build script produces it"; exit 1
fi
grep -q 'Vulkan-Loader' NOTICE || { echo "FAIL Vulkan-Loader is not listed in NOTICE"; exit 1; }
grep -q 'Vulkan-Headers' NOTICE || { echo "FAIL Vulkan-Headers is not listed in NOTICE"; exit 1; }
echo "VULKAN LOADER RECIPE PASS"
