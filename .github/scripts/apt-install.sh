#!/usr/bin/env bash
# CI apt installer resilient to Ubuntu mirror outages on hosted runners.
#
# The hosted runners can lose DNS for the ubuntu.com archive hosts
# (azure.archive, archive, security) while other domains still resolve:
# on 2026-11-11 every ci job spent ~50 minutes in apt retry loops and then
# died with "Temporary failure resolving ..." / exit 100, while
# packages.microsoft.com fetched fine. This wrapper probes the configured
# mirrors first; when none answers it prepends the first reachable
# third-party mirror to /etc/apt/sources.list.d/ubuntu.sources so apt uses
# it, and fails fast when nothing is reachable instead of grinding through
# apt's retry cycle for an hour.
#
# Usage: apt-install.sh <package>...
set -euo pipefail

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <package>..." >&2
    exit 2
fi

sources_file=/etc/apt/sources.list.d/ubuntu.sources
mirrors_file=/etc/apt/apt-mirrors.txt

suite=$(. /etc/os-release && printf '%s' "${VERSION_CODENAME:-}")
if [ -z "$suite" ] && [ -f "$sources_file" ]; then
    suite=$(awk '/^Suites:/ { print $2; exit }' "$sources_file")
fi

probe() { # probe <mirror-root-url>: does it serve this suite's InRelease?
    [ -n "$suite" ] || return 1
    curl -fsSI --max-time 8 "$1/dists/$suite/InRelease" -o /dev/null
}

default_mirrors_ok() {
    [ -f "$mirrors_file" ] || return 0
    while IFS=$'\t' read -r url _prio; do
        case $url in '' | '#'*) continue ;; esac
        if probe "$url"; then
            echo "apt-install: default mirror reachable: $url" >&2
            return 0
        fi
    done <"$mirrors_file"
    return 1
}

# Third-party mirrors on DNS domains independent of ubuntu.com, each
# serving the standard dists/<suite> layout over https.
fallbacks=(
    https://mirror.rackspace.com/ubuntu
    https://mirrors.kernel.org/ubuntu
    https://mirror.math.princeton.edu/pub/ubuntu
    https://ubuntu.mirror.constant.com
)

if ! default_mirrors_ok; then
    echo "apt-install: ubuntu.com mirrors unreachable; probing fallbacks" >&2
    picked=''
    for candidate in "${fallbacks[@]}"; do
        if probe "$candidate"; then
            picked=$candidate
            break
        fi
    done
    if [ -z "$picked" ]; then
        echo "apt-install: no Ubuntu mirror reachable; failing fast instead of hanging" >&2
        exit 1
    fi
    echo "apt-install: using fallback mirror $picked" >&2
    sudo sed -i "s|^URIs:.*|URIs: $picked|" "$sources_file"
fi

export DEBIAN_FRONTEND=noninteractive
sudo apt-get update \
    -o Acquire::Retries=5 \
    -o Acquire::https::Timeout=20 \
    -o Acquire::http::Timeout=20
sudo apt-get install -y \
    -o Acquire::Retries=5 \
    -o Acquire::https::Timeout=20 \
    -o Acquire::http::Timeout=20 \
    "$@"
