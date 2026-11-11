#!/bin/bash
# user-signin.sh GAMERTAG-or-email  -- sign a console user in via PUT /ext/user
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
[ -n "${1:-}" ] || { echo "usage: $0 GAMERTAG-or-email" >&2; exit 2; }
dp_lock curl_dp "/ext/user?user=$(urlenc "$1")" -X PUT -H 'Content-Length: 0' -w 'signin HTTP %{http_code}\n'
