#!/bin/bash
# launch.sh PackageRelativeId(Family!App) PackageFullName     (also: sign user in first with PUT /ext/user if needed)
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
rid="$1"; full="$2"
dp_lock curl_dp "/api/taskmanager/app?appid=$(urlenc "$(b64 "$rid")")&package=$(urlenc "$(b64 "$full")")" -X POST -H 'Content-Length: 0' -w 'launch HTTP %{http_code}\n'
