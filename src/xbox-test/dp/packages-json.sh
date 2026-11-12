#!/bin/bash
# packages-json.sh -- print the console's installed-package list as JSON
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
dp_lock curl_dp "/api/app/packagemanager/packages"
