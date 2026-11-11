#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# xn: one entry point to build and test. Run it as ./xn from the repository root (see docs/BUILD-AND-TEST.md).
#   xn doctor            check tools and .env (read-only)
#   xn check             the offline checks CI runs (no console, no cross toolchain)
#   xn build             build the launcher package (src/xbox-test/app/build.sh)
#   xn local             check, then build
#   xn console           build, bump version, sign, install on the console, run the self-test, collect logs
# It reuses src/xbox-test/app/build.sh, dp/*.sh and tools/play-test.sh; it adds no new way of talking to the console.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
export PATH="$HOME/.local/bin:$PATH"
tools="$here/tools"
dp="${XN_DP_DIR:-$here/dp}"          # XN_DP_DIR is a test seam: the unit test points it at a fake Device Portal
helper="$tools/xn_console.py"

die() { echo "xn: error: $*" >&2; exit 2; }
note() { echo "xn: $*" >&2; }

usage() {
  cat <<'EOF'
usage: ./xn <command> [options]

commands
  doctor              Check tools and .env for every command. Read-only. Exit 1 when something is missing.
  check               Run the offline steps of .github/workflows/ci.yml locally. No console needed.
                        --job NAME (repeatable)   run another CI job (default: offline-checks)
                        --all                     every CI job; jobs whose cross tools are missing are SKIP, not PASS
                        --allow-skip              exit 0 even when steps were skipped
                        --list                    show the steps and what they need
  build               Build the launcher package: src/xbox-test/app/out/xbtest.msix (+ test.cer).
                        --version A.B.C.D         stamp the package with this version (default: manifest's)
  local               check, then build. Everything that does not need a console.
  console             Deploy to the Xbox and run the automated console tests (needs .env).
                        --yes                     do not ask for confirmation (required without a terminal)
                        --dry-run                 validate .env and tools, print the plan, touch nothing (no network)
                        --probe                   read-only: reach the console, check credentials, show the installed and the next version
                        --msix FILE               install this package instead of building one (its version must be newer)
                        --modes LIST              self-test modes, comma separated (default: all)
                        --timeout SECONDS         wait for the self-test (default: 900)
                        --game APPID              afterwards run tools/play-test.sh <appid> (an owned, logged-in game)
                        --out DIR                 evidence directory (default: out/xn/console-<time>)
  -h, --help          this text (also: xn <command> --help)

Exit codes: 0 pass, 1 test failure, 2 usage/environment problem, 3 passed but something was skipped.
A SKIP, BLOCKED or FAIL result is never counted as PASS.
EOF
}

# --- .env --------------------------------------------------------------------------------------
env_file=""
find_env() {
  local c common
  [ -n "${XN_ENV:-}" ] && { [ -f "$XN_ENV" ] && echo "$XN_ENV"; return; }
  [ -f "$root/.env" ] && { echo "$root/.env"; return; }
  # git worktrees do not copy the ignored .env: fall back to the main checkout's.
  common=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir 2>/dev/null) || return 0
  c="$(dirname "$common")/.env"
  [ -f "$c" ] && echo "$c"
  return 0
}
# KEY=VALUE lines only; never executed. Variables already in the environment win. Values are never printed.
load_env() {
  env_file=$(find_env)
  [ -n "$env_file" ] || return 0
  local line key val n=0
  while IFS= read -r line || [ -n "$line" ]; do
    n=$((n + 1))
    line="${line#"${line%%[![:space:]]*}"}"
    [[ -z $line || $line == \#* ]] && continue
    line="${line#export }"
    if ! [[ $line =~ ^([A-Za-z_][A-Za-z0-9_]*)=(.*)$ ]]; then note "$env_file:$n is not KEY=VALUE; ignored"; continue; fi
    key="${BASH_REMATCH[1]}"; val="${BASH_REMATCH[2]}"
    if [[ $val =~ ^\"(.*)\"$ || $val =~ ^\'(.*)\'$ ]]; then val="${BASH_REMATCH[1]}"; fi
    if ! [[ $key =~ ^(DP_[A-Z_]+|XN_[A-Z_]+|PT_[A-Z_]+|PLAY_[A-Z_]+|PKG|UWP_CROSSBUILD|LLVM_MINGW_ROOT|[A-Z0-9_]+_PATH)$ ]]; then
      note "$env_file:$n: $key is not a variable xn uses; ignored"; continue
    fi
    [ -n "${!key+x}" ] && continue
    export "$key=$val"
  done < "$env_file"
}

have() { command -v "$1" >/dev/null 2>&1; }
missing_cmds() { local m=() c; for c in "$@"; do have "$c" || m+=("$c"); done; echo "${m[*]:-}"; }

CHECK_TOOLS=(bash git python3 node c++)
CONSOLE_TOOLS=(bash curl python3 flock base64)
BUILD_TOOLS=(python3 openssl makemsix osslsigncode)

# --- doctor ------------------------------------------------------------------------------------
report_group() {   # title, then tools
  local title=$1 c bad=0; shift
  echo "[$title]"
  for c in "$@"; do
    if have "$c"; then printf '  ok       %s\n' "$c"; else printf '  MISSING  %s\n' "$c"; bad=1; fi
  done
  return $bad
}
cmd_doctor() {
  local bad=0
  load_env
  echo "repo: $root"
  report_group "check (CI parity: offline steps)" "${CHECK_TOOLS[@]}" || bad=1
  echo "[build (launcher package)]"
  if bash "$root/src/tools/check-uwp-build-env.sh" 2>&1 | sed 's/^/  /'; then :; else bad=1; fi
  report_group "console (Device Portal client)" "${CONSOLE_TOOLS[@]}" || bad=1
  echo "[console credentials: ${env_file:-no .env found; using the environment}]"
  local v
  for v in DP_HOST DP_PASS; do
    if [ -n "${!v:-}" ]; then echo "  ok       $v (set)"; else echo "  MISSING  $v  (copy .env.example to .env)"; bad=1; fi
  done
  for v in DP_USER DP_PORT DP_SOCKS DP_LOCK; do
    if [ -n "${!v+x}" ]; then echo "  ok       $v (set)"; else echo "  default  $v (see .env.example)"; fi
  done
  return $bad
}

# --- check -------------------------------------------------------------------------------------
cmd_check() {
  case "${1:-}" in -h|--help) usage; return 0;; esac
  local m; m=$(missing_cmds python3); [ -z "$m" ] || die "missing tool: $m"
  python3 -I "$tools/ci_local.py" "$@"
}

# --- build -------------------------------------------------------------------------------------
cmd_build() {
  local version=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --version) version=${2:-}; shift 2 || die "--version needs A.B.C.D";;
      -h|--help) usage; return 0;;
      *) die "build: unknown option $1";;
    esac
  done
  load_env
  [ -z "$version" ] || [[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "--version must be A.B.C.D"
  local env_out
  if ! env_out=$(bash "$root/src/tools/check-uwp-build-env.sh" 2>&1); then
    echo "$env_out" >&2
    echo "xn: the launcher build tools are missing. See README.md 'Build'; for the cross-build wrapper run:" >&2
    echo "      src/tools/fetch-uwp-crossbuild.sh   (then export UWP_CROSSBUILD=<its path>), and for makemsix: src/tools/build-msix-packaging.sh" >&2
    return 2
  fi
  echo "xn: building the launcher${version:+ as version $version} (this takes a while; log: out/xn/build.log)"
  mkdir -p "$root/out/xn"
  ( cd "$here/app" && PACKAGE_VERSION="$version" bash build.sh out ) 2>&1 | tee "$root/out/xn/build.log" | tail -5
  local rc=${PIPESTATUS[0]}
  [ "$rc" -eq 0 ] || { echo "xn: BUILD FAILED (exit $rc); full log: $root/out/xn/build.log" >&2; return 1; }
  [ -f "$here/app/out/xbtest.msix" ] || { echo "xn: BUILD FAILED: no xbtest.msix produced" >&2; return 1; }
  echo "xn: BUILD OK $here/app/out/xbtest.msix ($(python3 -I "$helper" manifest-version "$here/app/out/xbtest.msix"))"
}

cmd_local() {
  case "${1:-}" in -h|--help) usage; return 0;; esac
  cmd_check "$@"; local rc=$?
  [ "$rc" -eq 0 ] || { echo "xn: local stops: check did not pass (exit $rc)" >&2; return "$rc"; }
  cmd_build
}

# --- console -----------------------------------------------------------------------------------
dp_status() {   # GET PATH OUT -> prints the HTTP status
  dp_lock curl_dp "$1" -o "$2" -w '%{http_code}' 2>/dev/null
}
ls_localstate() { bash "$dp/ls-dir.sh" "$1" '\LocalState' 2>/dev/null; }

cmd_console() {
  local yes=0 dry=0 probe=0 msix="" modes="all" timeout=900 game="" out="" version=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --yes) yes=1; shift;;
      --dry-run) dry=1; shift;;
      --probe) probe=1; shift;;
      --msix) msix=${2:-}; shift 2 || die "--msix needs a file";;
      --modes) modes=${2:-}; shift 2 || die "--modes needs a list";;
      --timeout) timeout=${2:-}; shift 2 || die "--timeout needs seconds";;
      --game) game=${2:-}; shift 2 || die "--game needs an appid";;
      --out) out=${2:-}; shift 2 || die "--out needs a directory";;
      --version) version=${2:-}; shift 2 || die "--version needs A.B.C.D";;
      -h|--help) usage; return 0;;
      *) die "console: unknown option $1 (see ./xn --help)";;
    esac
  done
  [[ $timeout =~ ^[0-9]+$ ]] || die "--timeout must be a number of seconds"
  [ -z "$game" ] || [[ $game =~ ^[0-9]+$ ]] || die "--game must be a Steam appid (digits)"
  [[ $modes =~ ^[A-Za-z0-9_,-]+$ ]] || die "--modes may only contain letters, digits, '-', '_' and ','"
  [ -z "$version" ] || [[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "--version must be A.B.C.D"
  [ -z "$msix" ] || [ -f "$msix" ] || die "--msix: no such file: $msix"

  load_env
  local m
  m=$(missing_cmds "${CONSOLE_TOOLS[@]}"); [ -z "$m" ] || die "missing tool(s): $m (run ./xn doctor)"
  [ -n "$msix" ] || [ "$probe" -eq 1 ] || { m=$(missing_cmds "${BUILD_TOOLS[@]}"); [ -z "$m" ] || die "missing build tool(s): $m (run ./xn doctor; or pass --msix FILE)"; }
  m=""
  [ -n "${DP_HOST:-}" ] || m="$m DP_HOST"
  [ -n "${DP_PASS:-}" ] || m="$m DP_PASS"
  [ -z "$m" ] || die "missing in .env or the environment:$m. Copy .env.example to .env and fill it in (never commit it)."
  [ -n "${DP_USER:-}" ] || note "DP_USER is not set; the Device Portal scripts fall back to their built-in default user"
  [ "$game" = "" ] || [ -x "$here/tools/play-test.sh" ] || die "tools/play-test.sh is missing"

  # The console lock is shared by every session using this console; keep it on a path they can agree on.
  if [ -z "${DP_LOCK:-}" ]; then
    if [ -d /workspace/x64-run ]; then DP_LOCK=/workspace/x64-run/CONSOLE.lock; else DP_LOCK="${XDG_RUNTIME_DIR:-/tmp}/xboxnative-CONSOLE.lock"; fi
  fi
  export DP_LOCK
  : "${out:=$root/out/xn/console-$(date +%Y%m%d-%H%M%S)}"
  mkdir -p "$out" || die "cannot create $out"
  out=$(cd "$out" && pwd)

  echo "xn console plan"
  echo "  console:   $(printf '%s' "$DP_HOST" | sed -E 's#^[a-z]+://##; s#/.*$##')   (credentials from ${env_file:-the environment})"
  echo "  package:   ${msix:-build from src/xbox-test/app (version bumped above the installed one)}"
  echo "  deploy:    stop the running launcher, install, wait until the new version is listed, launch"
  echo "  test:      selftest.txt modes=$modes (timeout ${timeout}s)${game:+, then tools/play-test.sh $game}"
  echo "  evidence:  $out"
  echo "  lock:      $DP_LOCK"
  if [ "$dry" -eq 1 ]; then echo "xn: dry run; nothing was built, uploaded or installed"; return 0; fi

  if [ "$yes" -ne 1 ] && [ "$probe" -ne 1 ]; then
    cat <<'EOF'

Risk: installing replaces the launcher on a shared console. Past upgrades have, once, wiped the app's
local data (Steam login and installed games, which then have to be redone by the owner). The console may be in use
by another session. xn records the LocalState file list before and after and warns if the Steam login file is gone.
EOF
    if [ -t 0 ]; then read -r -p "Deploy to this console now? [type yes] " ans; [ "$ans" = yes ] || die "not confirmed"
    else die "refusing to deploy without a terminal to confirm on; re-run with --yes"; fi
  fi

  export DP_JAR; DP_JAR=$(mktemp "${TMPDIR:-/tmp}/xn-dp-cookies.XXXXXX"); trap 'rm -f "$DP_JAR"' EXIT
  # shellcheck source=/dev/null
  . "$dp/dp-lib.sh"

  # 1. reachability and credentials, before spending time on a build
  local code; code=$(dp_status /api/os/info "$out/os-info.json") || code=000
  case "$code" in
    2??) ;;
    401|403) die "Device Portal refused the credentials (HTTP $code): check DP_USER and DP_PASS";;
    000) die "Device Portal unreachable: check DP_HOST, DP_PORT, the network, and DP_SOCKS (set it empty to connect directly)";;
    *) die "Device Portal answered HTTP $code on /api/os/info";;
  esac
  rm -f "$out/os-info.json"
  echo "PASS: console reachable, credentials accepted"

  local pk="$out/packages.json" installed installed_ver="" manifest_ver new_ver
  dp_status /api/app/packagemanager/packages "$pk" >/dev/null || true
  installed=$(python3 -I "$helper" latest-package "$pk" 2>/dev/null || true)
  [ -z "$installed" ] || installed_ver=$(python3 -I "$helper" package-version "$installed")
  echo "xn: installed launcher: ${installed:-none}"

  # 2. package + version
  manifest_ver=$(python3 -I "$helper" manifest-version "$here/app/AppxManifest.xml") || die "cannot read the manifest version"
  if [ "$probe" -eq 1 ]; then
    echo "probe: installed ${installed:-none}; the next deploy would be version $(python3 -I "$helper" next-version "$manifest_ver" "$installed_ver")"
    echo "xn: probe only; nothing was built, uploaded or installed"; return 0
  fi
  if [ -n "$msix" ]; then
    new_ver=$(python3 -I "$helper" manifest-version "$msix") || die "cannot read the version inside $msix"
    if [ -n "$installed_ver" ] && ! python3 -I "$helper" version-gt "$new_ver" "$installed_ver"; then
      die "$msix is version $new_ver; the console has $installed_ver. A same or older version is silently ignored. Build a newer one."
    fi
  else
    new_ver=${version:-$(python3 -I "$helper" next-version "$manifest_ver" "$installed_ver")}
    if [ -n "$installed_ver" ] && ! python3 -I "$helper" version-gt "$new_ver" "$installed_ver"; then die "--version $new_ver is not newer than the installed $installed_ver"; fi
    cmd_build --version "$new_ver" || { echo "FAIL: build"; return 1; }
    msix="$here/app/out/xbtest.msix"
  fi
  echo "PASS: package ready, version $new_ver"
  local cert; cert="$(dirname "$msix")/test.cer"; [ -f "$cert" ] || cert=""

  # 3. take the console for the whole deploy+test; dp helpers reuse this lock (dp-lib.sh: DP_LOCK_FD)
  exec 9>"$DP_LOCK" || die "cannot open the console lock $DP_LOCK"
  flock -w 600 9 || die "console lock busy for 10 minutes: another session is using the console ($DP_LOCK)"
  export DP_LOCK_FD=9
  console_run "$@"
  local rc=$?
  unset DP_LOCK_FD; flock -u 9; exec 9>&-
  return $rc
}

# Everything below runs with the console lock held. Uses: out installed installed_ver new_ver msix cert modes timeout game
console_run() {
  local fails=0 full_new rid ver_listed=0 i
  # re-check under the lock: another session may have installed a newer version while we built
  dp_status /api/app/packagemanager/packages "$out/packages.json" >/dev/null || true
  local now_pkg now_ver=""
  now_pkg=$(python3 -I "$helper" latest-package "$out/packages.json" 2>/dev/null || true)
  [ -z "$now_pkg" ] || now_ver=$(python3 -I "$helper" package-version "$now_pkg")
  if [ -n "$now_ver" ] && ! python3 -I "$helper" version-gt "$new_ver" "$now_ver"; then
    echo "FAIL: console now has $now_ver, which is not older than the $new_ver we built; re-run ./xn console" >&2; return 1
  fi
  if [ -n "$now_pkg" ]; then
    ls_localstate "$now_pkg" > "$out/localstate-before.txt" || : > "$out/localstate-before.txt"
    bash "$dp/kill.sh" "$now_pkg" > "$out/kill.log" 2>&1 || true   # a running or hung app blocks the install; the HTTP status of a stop is unreliable
    sleep 3
  fi
  echo "-- install $new_ver --"
  if ! bash "$dp/install.sh" "$msix" ${cert:+"$cert"} > "$out/install.log" 2>&1; then
    tail -5 "$out/install.log"; echo "FAIL: install (log: $out/install.log)"; return 1
  fi
  # install.sh can report OK before the deployment finishes: wait for the version to be listed.
  for i in $(seq 1 60); do
    dp_status /api/app/packagemanager/packages "$out/packages.json" >/dev/null || true
    full_new=$(python3 -I "$helper" latest-package "$out/packages.json" 2>/dev/null || true)
    if [ -n "$full_new" ] && [ "$(python3 -I "$helper" package-version "$full_new")" = "$new_ver" ]; then ver_listed=1; break; fi
    sleep 5
  done
  [ "$ver_listed" -eq 1 ] || { echo "FAIL: $new_ver never appeared in the package list (stuck deployment? see $out/install.log)"; return 1; }
  echo "PASS: installed $full_new"
  ls_localstate "$full_new" > "$out/localstate-after.txt" || : > "$out/localstate-after.txt"
  if grep -q ' steam_session.bin$' "$out/localstate-before.txt" 2>/dev/null && ! grep -q ' steam_session.bin$' "$out/localstate-after.txt"; then
    echo "WARN: the Steam login file existed before the install and is gone now; the owner has to sign in again (see docs/BUILD-AND-TEST.md)"
  fi

  rid="${full_new%%_*}_${full_new##*__}!App"
  echo "-- launch --"
  if ! bash "$dp/launch.sh" "$rid" "$full_new" > "$out/launch.log" 2>&1 || ! grep -Eq 'launch HTTP 2[0-9][0-9]' "$out/launch.log"; then
    cat "$out/launch.log"; echo "FAIL: launch"; return 1
  fi
  if ! bash "$dp/wait-log.sh" "$full_new" boot.log 'Run: entering' 120 "$out/boot.log" >/dev/null 2>&1; then
    echo "FAIL: the launcher did not reach its main loop within 120s (no 'Run: entering' in boot.log)"; return 1
  fi
  echo "PASS: launcher running"

  echo "-- self-test ($modes) --"
  # The launcher deletes selftest.log when it consumes a request (selftest.h: "a new
  # request starts a new record"), so the fetched log is a fresh replacement, not an
  # append — judging it needs no line offset. Wait until the request file is gone
  # (consumption deletes it), then the log belongs to this run alone; an earlier
  # run's summary can never satisfy the verdict.
  local tmp consumed=0; tmp=$(mktemp -d)
  printf '%s\n' "$modes" | tr ',' '\n' > "$tmp/selftest.txt"
  if ! bash "$dp/put-file.sh" "$full_new" '\LocalState' "$tmp/selftest.txt" 2>/dev/null | grep -Eq 'put HTTP 2[0-9][0-9]'; then
    echo "FAIL: could not upload selftest.txt"; rm -rf "$tmp"; return 1
  fi
  local end=$((SECONDS + timeout)) vcode=4
  while [ $SECONDS -lt $end ]; do
    sleep 10
    if [ "$consumed" -eq 0 ]; then
      # the request file disappears once the launcher has taken the run
      rm -f "$out/selftest-gone.txt" "$out/selftest-gone.log"
      if bash "$dp/get-file.sh" "$full_new" '\LocalState' selftest.txt "$out/selftest-gone.txt" > "$out/selftest-gone.log" 2>&1; then
        continue   # still there: the launcher has not consumed the request yet
      fi
      grep -q 'HTTP 404' "$out/selftest-gone.log" || continue   # a network error is not consumption
      consumed=1
    fi
    # the launcher restarts between fixtures, so a failed pull is normal now and then
    rm -f "$out/selftest.log"
    bash "$dp/get-file.sh" "$full_new" '\LocalState' selftest.log "$out/selftest.log" >/dev/null 2>&1 || continue
    python3 -I "$helper" selftest-verdict "$out/selftest.log" > "$out/selftest-report.txt"; vcode=$?
    [ "$vcode" -eq 4 ] || break
  done
  rm -rf "$tmp"
  for f in selftest_probe.log boot.log; do
    rm -f "$out/$f"; bash "$dp/get-file.sh" "$full_new" '\LocalState' "$f" "$out/$f" >/dev/null 2>&1 || note "could not fetch $f"
  done
  bash "$dp/screenshot.sh" "$out/final.png" >/dev/null 2>&1 || true
  case "$vcode" in
    0) cat "$out/selftest-report.txt"; echo "PASS: console self-test";;
    1) cat "$out/selftest-report.txt"; echo "FAIL: console self-test"; fails=$((fails + 1));;
    *) echo "FAIL: console self-test did not finish within ${timeout}s (no SELFTEST SUMMARY); see $out/selftest.log"; fails=$((fails + 1));;
  esac

  if [ -n "$game" ]; then
    echo "-- acceptance: play-test.sh $game --"
    if PKG="$full_new" bash "$here/tools/play-test.sh" "$game" 600 "$out/play-$game" > "$out/play-test.log" 2>&1; then
      tail -3 "$out/play-test.log"; echo "PASS: play-test $game (visual/playable sign-off still required)"
    else
      tail -8 "$out/play-test.log"; echo "FAIL: play-test $game (log: $out/play-test.log)"; fails=$((fails + 1))
    fi
  fi
  echo "evidence: $out (boot.log can name the Steam account: review before sharing; never commit it)"
  if [ "$fails" -eq 0 ]; then echo "xn console: PASS (installed $full_new)"; return 0; fi
  echo "xn console: FAIL ($fails failing section(s))"; return 1
}

# --- dispatch ----------------------------------------------------------------------------------
cmd=${1:-}; [ $# -eq 0 ] || shift
case "$cmd" in
  ""|-h|--help|help) usage;;
  doctor) cmd_doctor "$@";;
  check) cmd_check "$@";;
  build) cmd_build "$@";;
  local) cmd_local "$@";;
  console) cmd_console "$@";;
  *) echo "xn: unknown command '$cmd'" >&2; usage >&2; exit 2;;
esac
