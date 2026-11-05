#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-sqlite-lib.sh OUT.lib -> UWP static SQLite (pinned amalgamation via fetch-sqlite.sh) backing the
# uwph registry (reg_builtin.h). Shared by the launcher and the Play test runners; rebuilt only when missing.
# Callers add: --link-arg OUT.lib and /I<this dir>/third_party/sqlite.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; lib="$1"
bash "$here/fetch-sqlite.sh" >/dev/null
[ -s "$lib" ] && exit 0
CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
SQL_DEFS=(/DSQLITE_THREADSAFE=1 /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_OMIT_SHARED_CACHE /DSQLITE_TEMP_STORE=3 /DSQLITE_DEFAULT_MEMSTATUS=0 /DSQLITE_OMIT_DEPRECATED)
mkdir -p "$(dirname "$lib")"
# Relative paths only: clang-cl style tools read "/abs/path" as an option.
rel=$(realpath -m --relative-to="$here" "$lib")
( cd "$here" && UWP_OBJ_DIR="$(dirname "$rel")/sqlite-obj" "$CB/scripts/build.sh" --uwp --static-lib --out "$rel" --jobs 4 sqlite_uwp.c -- /Ithird_party/sqlite /MT /O2 /DNDEBUG /w "${SQL_DEFS[@]}" )
