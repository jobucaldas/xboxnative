#!/bin/bash
# screenshot.sh OUT.png  -- GET /ext/screenshot (PNG)
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
dp_lock curl_dp "/ext/screenshot" -f -o "${1:-shot.png}" -w "shot HTTP %{http_code} bytes=%{size_download}\n"
