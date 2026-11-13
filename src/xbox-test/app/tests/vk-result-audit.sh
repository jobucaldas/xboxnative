#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# VkResult passthrough audit for the Vulkan fixtures. The console run of
# vulkan-draw once failed with "vkEndCommandBuffer result=0" — result 0 is
# VK_SUCCESS, which read as an inverted check but was the fixture reporting a
# literal 0 instead of the VkResult it had just received, destroying the only
# evidence of what the driver returned. Every VK_SUCCESS comparison in these
# fixtures must therefore guard a captured result that reaches fail(); this
# audit turns a discarded one into a build failure. Nothing runs: a scan that
# passes is the pass.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
status=0

for f in "$here"/play-vulkan.cpp "$here"/play-vulkan-dzn.cpp "$here"/play-vulkan-draw.cpp; do
  # 1. A VK_SUCCESS comparison whose guarded fail() passes a literal 0: the
  #    real VkResult is discarded (the exact shape that produced the console's
  #    "result=0" on a genuine failure).
  if grep -nE 'VK_SUCCESS' "$f" | grep 'fail(' | grep -E 'fail\([0-9]+, *"[^"]*", *0\)'; then
    echo "FAIL: $f discards the VkResult at the line(s) above" >&2
    status=1
  fi
  # 2. A multi-line form: the comparison on one line, the guarded fail(...) 0
  #    on the next. Catch any fail(..., 0) whose line sits inside two lines of
  #    a VK_SUCCESS comparison.
  python3 -I - "$f" <<'PY'
import re, sys
path = sys.argv[1]
lines = open(path).read().splitlines()
bad = []
for i, line in enumerate(lines):
    if "fail(" in line and re.search(r'fail\(\s*\d+\s*,\s*"[^"]*"\s*,\s*0\s*\)', line):
        window = "\n".join(lines[max(0, i - 3):i + 1])
        if "VK_SUCCESS" in window:
            bad.append(f"{path}:{i + 1}: {line.strip()}")
if bad:
    print("\n".join(bad), file=sys.stderr)
    print(f"FAIL: {path} discards the VkResult near the line(s) above", file=sys.stderr)
    sys.exit(1)
PY
  [ $? -eq 0 ] || status=1
done

if [ "$status" -ne 0 ]; then
  echo "FAIL: vk-result-audit (a VkResult was discarded; pass it to fail())" >&2
  exit 1
fi
echo "PASS: vk-result-audit (every VK_SUCCESS check reports the real VkResult)"
