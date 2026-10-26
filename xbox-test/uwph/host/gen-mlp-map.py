#!/usr/bin/env python3
import json
from pathlib import Path
root = Path(__file__).resolve().parents[3]
data = json.loads((root / "tools/xbox/import_map.json").read_text())
out = Path(__file__).with_name("mlp_routes.h")
out.write_text("// Generated from tools/xbox/import_map.json by gen-mlp-map.py.\n" + "".join(
    '{"%s", "%s"},\n' % (dll, row["class"]) for dll, row in sorted(data["dlls"].items())))
