# shared helpers; DP_PASS from env, never printed. Override: DP_HOST, DP_PORT, DP_USER, DP_SOCKS, DP_LOCK.
# An optional environment file (git-ignored) is sourced first if present: $DP_ENV_FILE, or .env in the repo root.
# DP_HOST may be a bare host or a URL ("https://host[:port][/]"); DP_PORT wins over a port in the URL (default 11443).
_dp_env="${DP_ENV_FILE:-}"
if [ -z "$_dp_env" ]; then
  _dp_here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  _dp_root="$(git -C "$_dp_here" rev-parse --show-toplevel 2>/dev/null)" || _dp_root="$(cd "$_dp_here/../../.." && pwd)"
  _dp_env="$_dp_root/.env"
  unset _dp_here _dp_root
fi
[ -f "$_dp_env" ] && . "$_dp_env"
unset _dp_env
if [ -z "${DP_HOST:-}" ]; then
  echo "dp-lib.sh: DP_HOST not set (copy .env.example to .env, or export DP_HOST)" >&2
  return 1 2>/dev/null || exit 1
fi
DP_HOST="${DP_HOST#*://}"; DP_HOST="${DP_HOST%%/*}"
[[ $DP_HOST == *:* ]] && { DP_PORT="${DP_PORT:-${DP_HOST##*:}}"; DP_HOST="${DP_HOST%:*}"; }
DP_PORT="${DP_PORT:-11443}"; DP_SOCKS="${DP_SOCKS-}"   # DP_SOCKS= (empty, the default) connects directly
DP_LOCK="${DP_LOCK:-${TMPDIR:-/tmp}/xboxnative-console.lock}"
DP_JAR="${DP_JAR:-/tmp/dp-cookies.$$}"
urlenc() { python3 -c 'import urllib.parse,sys;print(urllib.parse.quote(sys.argv[1],safe=""))' "$1"; }
b64() { printf %s "$1" | base64 -w0; }
# curl_dp PATH [curl args]: authenticated, CSRF header from cookie jar
curl_dp() {
  local p="$1"; shift
  [ -s "$DP_JAR" ] || printf 'user = "%s:%s"\n' "${DP_USER:-}" "$DP_PASS" | curl -sk -m 30 ${DP_SOCKS:+--socks5-hostname $DP_SOCKS} -K - -c "$DP_JAR" -o /dev/null "https://$DP_HOST:$DP_PORT/api/os/info"
  local csrf; csrf=$(awk '$6=="CSRF-Token"{print $7}' "$DP_JAR" | tail -1)
  printf 'user = "%s:%s"\n' "${DP_USER:-}" "$DP_PASS" | curl -sk -m "${DP_TIMEOUT:-120}" ${DP_SOCKS:+--socks5-hostname $DP_SOCKS} -K - -b "$DP_JAR" -H "X-CSRF-Token: $csrf" "$@" "https://$DP_HOST:$DP_PORT$p"
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
