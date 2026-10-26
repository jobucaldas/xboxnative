#!/usr/bin/env bash
# Fetch Boxedwine (GPL-2.0-or-later) at the pin in deps.pin into third_party/boxedwine (git-ignored). Usage: fetch-boxedwine.sh [dest] [--dry-run]
exec "$(dirname "${BASH_SOURCE[0]}")/fetch-dep.sh" boxedwine "$@"
