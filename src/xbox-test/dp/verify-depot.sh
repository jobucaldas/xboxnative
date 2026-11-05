#!/bin/bash
# Read-only depot verification. Console helpers acquire CONSOLE.lock themselves.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$HERE/verify-depot.py" "$@"
