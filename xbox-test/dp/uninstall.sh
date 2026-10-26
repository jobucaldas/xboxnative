#!/bin/bash
# uninstall.sh PackageFullName
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
dp_lock curl_dp "/api/app/packagemanager/package?package=$(urlenc "$1")" -X DELETE -w 'HTTP %{http_code}\n'
