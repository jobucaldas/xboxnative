#!/bin/bash
# user-signin.sh [GAMERTAG-or-email]  -- sign a console user in via PUT /ext/user (default jobucaldas)
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
dp_lock curl_dp "/ext/user?user=$(urlenc "${1:-jobucaldas}")" -X PUT -H 'Content-Length: 0' -w 'signin HTTP %{http_code}\n'
