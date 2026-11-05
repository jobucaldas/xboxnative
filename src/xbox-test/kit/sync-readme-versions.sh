#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# sync-readme-versions.sh [--check] [MANIFEST] [OUTPUT]
# Render artifact-local VERSIONS.md. Never edits the tracked README.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
mode=write
if [ "${1:-}" = --check ]; then mode=check; shift; fi
if (( $# > 2 )); then echo "usage: $0 [--check] [MANIFEST] [OUTPUT]" >&2; exit 2; fi
manifest="${1:-${KIT_OUT_ROOT:-$here/../../out/kit}/latest/manifest.json}"
output="${2:-$(dirname "$manifest")/VERSIONS.md}"
python3 "$here/kitlib.py" versions "$mode" "$manifest" "$output"
