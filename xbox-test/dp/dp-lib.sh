# shared helpers; DP_PASS from env, never printed. Override: DP_HOST, DP_SOCKS (default tailscale socks5 localhost:1055)
DP_HOST="${DP_HOST:-192.168.15.96}"; DP_SOCKS="${DP_SOCKS:-localhost:1055}"
DP_LOCK="${DP_LOCK:-/workspace/x64-run/CONSOLE.lock}"
DP_JAR="${DP_JAR:-/tmp/dp-cookies.$$}"
urlenc() { python3 -c 'import urllib.parse,sys;print(urllib.parse.quote(sys.argv[1],safe=""))' "$1"; }
b64() { printf %s "$1" | base64 -w0; }
# curl_dp PATH [curl args]: authenticated, CSRF header from cookie jar
curl_dp() {
  local p="$1"; shift
  [ -s "$DP_JAR" ] || printf 'user = "jobucaldas:%s"\n' "$DP_PASS" | curl -sk -m 30 ${DP_SOCKS:+--socks5-hostname $DP_SOCKS} -K - -c "$DP_JAR" -o /dev/null "https://$DP_HOST:11443/api/os/info"
  local csrf; csrf=$(awk '$6=="CSRF-Token"{print $7}' "$DP_JAR" | tail -1)
  printf 'user = "jobucaldas:%s"\n' "$DP_PASS" | curl -sk -m "${DP_TIMEOUT:-120}" ${DP_SOCKS:+--socks5-hostname $DP_SOCKS} -K - -b "$DP_JAR" -H "X-CSRF-Token: $csrf" "$@" "https://$DP_HOST:11443$p"
}
dp_lock() {
  # A caller may hold the console lock for a whole multi-helper transaction.
  # Reuse its inherited open file description rather than deadlocking on it.
  if [[ ${DP_LOCK_FD:-} =~ ^[0-9]+$ ]] &&
     [ "/proc/$$/fd/$DP_LOCK_FD" -ef "$DP_LOCK" ]; then
    flock -w 600 "$DP_LOCK_FD" || return 99
    "$@"
  else
    ( flock -w 600 9 || exit 99; "$@" ) 9>"$DP_LOCK"
  fi
}
# dp_retry METHOD PATH [curl args] -> stdout body, retries 4x
dp_retry() { local m="$1" p="$2"; shift 2; local a out; for a in 1 2 3 4; do out=$(dp_lock curl_dp "$p" -X "$m" -f "$@") && { printf %s "$out"; return 0; }; sleep 2; done; return 1; }
