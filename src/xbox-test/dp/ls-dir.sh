#!/bin/bash
# ls-dir.sh PackageFullName REMOTE_DIR
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
dp_lock curl_dp "/api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=$(urlenc "$1")&path=$(urlenc "$2")" | python3 -c '
import json,sys
for i in json.load(sys.stdin)["Items"]: print(("d" if i["Type"]&16 else "-"),i.get("FileSize",""),i["Name"])'
