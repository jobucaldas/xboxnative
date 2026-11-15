#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root="$(cd "$(dirname "$0")/../../../.." && pwd)"
python3 -I - "$root" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
runtime = (root / "src/xbox-test/app/host/play_runtime.h").read_text()
pe_tail = (root / "src/xbox-test/shared/pe_tail.h").read_text()
catalog = (root / "src/xbox-test/app/wine_gap_catalog.h").read_text()

shim = re.search(r"static UINT WINAPI extractIconExW\([^)]*\)\s*\{([\s\S]*?)\n\}", runtime)
assert shim, "ExtractIconExW fallback is missing"
body = shim.group(1)
assert "large[i]=nullptr" in body and "smallOut[i]=nullptr" in body, "fallback must clear every requested output slot"
assert re.search(r"return\s+0\s*;", body), "fallback must report zero icons extracted"

route_start = runtime.find('if(name && key=="shell32.dll" && !strcmp(name,"ExtractIconExW")) {')
route_end = runtime.find("// Folder CSIDLs", route_start)
assert route_start >= 0 and route_end > route_start, "ExtractIconExW import route is missing"
route_body = runtime[route_start:route_end]
assert route_body.index("pe_tail::resolve") < route_body.index("&extractIconExW"), "real System32 export must be preferred"
assert '{"shell32.dll","ExtractIconExW"}' in pe_tail, "System32 allowlist entry is missing"
assert '{"shell32.dll", "ExtractIconExW", 0}' in catalog, "import-gap metadata is missing"
print("PASS ExtractIconExW: System32 first, otherwise zero icons and null outputs")
PY
