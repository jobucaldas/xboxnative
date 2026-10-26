#!/bin/bash
# logs.sh list|enable|disable|get PackageFullName [FILENAME LOCAL_OUT]
# ETW finding: /api/etw/providers, /api/etw/customproviders, /api/etw/session/realtime all
# return HTTP 404 on this console (Xbox Series S Dev Mode does not expose the Device Portal
# ETW REST/websocket API that exists on desktop/HoloLens/IoT builds). The real log options are:
#   1) crash dumps: /api/debug/dump/usermode/{dumps,crashcontrol} DO work (verified: `dumps`
#      lists real .dmp files already on the console, `crashcontrol` reads/sets per-package dump
#      capture). The dump *download* sub-route could not be found (every "download" path/param
#      guess returned 404); until it's found, pull a dump the slow way via ls-dir.sh/get-file.sh
#      against the package's crash-dump folder if one is exposed there.
#   2) app-written log files: still the most reliable path -- an app's own on-disk log (e.g.
#      ladder-native.log) read via get-file.sh/wait-log.sh. This script's `get` mode is just
#      that, so callers have one place for "give me this app's logs".
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
cmd="$1"; pkg="$2"
case "$cmd" in
  list)   dp_lock curl_dp "/api/debug/dump/usermode/dumps${pkg:+?packageFullName=$(urlenc "$pkg")}" -w '\nHTTP %{http_code}\n' ;;
  enable|disable)
    [ -n "$pkg" ] || { echo "usage: $0 $cmd PackageFullName"; exit 2; }
    v=$([ "$cmd" = enable ] && echo true || echo false)
    dp_lock curl_dp "/api/debug/dump/usermode/crashcontrol?packageFullName=$(urlenc "$pkg")&crashdumptype=$v" -X POST -H 'Content-Length: 0' -w '\nHTTP %{http_code}\n' ;;
  get)
    fname="$3"; out="$4"
    [ -n "$pkg" ] && [ -n "$fname" ] && [ -n "$out" ] || { echo "usage: $0 get PackageFullName FILENAME LOCAL_OUT"; exit 2; }
    exec "$HERE/get-file.sh" "$pkg" '\LocalState' "$fname" "$out" ;;
  *) echo "usage: $0 list [PackageFullName] | enable|disable PackageFullName | get PackageFullName FILENAME LOCAL_OUT"; exit 2 ;;
esac
