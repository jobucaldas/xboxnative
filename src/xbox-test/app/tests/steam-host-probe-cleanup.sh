#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# An early exit (compile failure here) must not delete a config.vdf the probe did not create.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d /tmp/steam-probe-cleanup.XXXXXX)"
trap 'rm -rf "$work"' EXIT
client="$work/client"
mkdir -p "$client/config"
printf 'pre-existing' > "$client/config/config.vdf"
printf 'token' > "$work/token"
set +e
CC=/bin/false bash "$here/steam-host-probe.sh" "$client" --token-file "$work/token" --login user --steamid 1 >/dev/null 2>&1
status=$?
set -e
if [[ $status == 0 ]]; then echo "FAIL expected the probe compile to fail"; exit 1; fi
if [[ "$(cat "$client/config/config.vdf" 2>/dev/null)" != pre-existing ]]; then echo "FAIL pre-existing config.vdf was removed"; exit 1; fi
echo "PASS steam-host-probe keeps a pre-existing config.vdf on early exit"
