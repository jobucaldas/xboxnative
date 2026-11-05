#!/bin/bash
# install.sh PACKAGE.msix [CERT.cer]   -- upload+install via Device Portal, then poll install state
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
pkg="$1"; cer="${2:-}"; [ -f "$pkg" ] || { echo "usage: $0 pkg.msix [cert.cer]"; exit 2; }
args=(-F "$(basename "$pkg")=@$pkg")
[ -n "$cer" ] && args+=(-F "$(basename "$cer")=@$cer")
dp_lock curl_dp "/api/app/packagemanager/package?package=$(basename "$pkg")" -X POST "${args[@]}" -o /tmp/dp-install.$$ -w 'upload HTTP %{http_code}\n'
cat /tmp/dp-install.$$ 2>/dev/null | head -c 400; echo; rm -f /tmp/dp-install.$$
for i in $(seq 1 60); do
  s=$(dp_lock curl_dp /api/app/packagemanager/state)
  echo "state: $s"
  case "$s" in *'"Code" : 0'*|*'"Code": 0'*) echo "INSTALL OK"; exit 0;; *'"Code"'*) case "$s" in *'"Code" : 80'*|*'"Code" : -'*) echo "INSTALL FAILED"; exit 1;; esac;; esac
  sleep 3
done; exit 1
