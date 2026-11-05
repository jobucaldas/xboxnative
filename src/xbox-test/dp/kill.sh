#!/bin/bash
# kill.sh PackageFullName
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
dp_lock curl_dp "/api/taskmanager/app?package=$(urlenc "$(b64 "$1")")&forcestop=yes" -X DELETE -w 'kill HTTP %{http_code}\n'
