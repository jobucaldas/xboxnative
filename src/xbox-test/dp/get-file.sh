#!/bin/bash
# get-file.sh PackageFullName REMOTE_DIR(e.g. \LocalState) FILENAME LOCAL_OUT
# Resumable: the console's file API honors Range requests (verified: GET with
# "Range: bytes=N-" on a 54 MB file returns HTTP 206 + Content-Range). On a dropped
# connection curl's -C - picks up from the local partial file's size instead of
# restarting, so multi-hundred-MB transfers survive flaky links. Set GET_RETRIES to
# change the attempt count (default 8).
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
pkg="$1"; dir="$2"; fname="$3"; out="$4"
url="/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$(urlenc "$pkg")&filename=$(urlenc "$fname")&path=$(urlenc "$dir")"
code=000
for i in $(seq 1 "${GET_RETRIES:-8}"); do
  code=$(dp_lock curl_dp "$url" -C - -o "$out" -w '%{http_code}'); rc=$?
  [ "$code" = "416" ] && { code=200; break; }  # -C - past EOF: already fully downloaded
  [ $rc -eq 0 ] && case "$code" in 2??) break;; esac
  sleep 2
done
sz=$(stat -c%s "$out" 2>/dev/null || echo 0)
echo "get HTTP $code bytes=$sz file=$out"
case "$code" in 2??) exit 0;; *) exit 1;; esac
