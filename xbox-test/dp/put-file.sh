#!/bin/bash
# put-file.sh PackageFullName REMOTE_DIR LOCAL_FILE   (uploads into LocalAppData\<pkg>\REMOTE_DIR)
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
dp_lock curl_dp "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$(urlenc "$1")&path=$(urlenc "$2")" -X POST -F "$(basename "$3")=@$3" -w "put HTTP %{http_code}\n"
