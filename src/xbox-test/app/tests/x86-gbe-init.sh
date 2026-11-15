#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run the pinned x86 gbe_fork Steam API DLL through the PE32 interpreter.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../../.." && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true); fi
[[ -n $cc32 ]] || { echo "x86-gbe-init: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
dlltool="$(dirname "$cc32")/i686-w64-mingw32-dlltool"
[[ -x $dlltool ]] || { echo "x86-gbe-init: no i686-w64-mingw32-dlltool" >&2; exit 1; }
bash "$root/src/tools/fetch-gbe-fork.sh" "$root/out/gbe-fork" >/dev/null
provider="$root/out/gbe-fork/steam_api.dll"
[[ -f $provider ]] || { echo "x86-gbe-init: pinned steam_api.dll missing" >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-gbe-init.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
flags=(-O1 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -nostdlib)
cat > "$tmp/steam_api.def" <<'EOF'
LIBRARY steam_api.dll
EXPORTS
    SteamAPI_Init
    SteamAPI_RunCallbacks
    SteamAPI_SteamUser_v023
    SteamAPI_SteamApps_v009
    SteamAPI_ISteamUser_GetSteamID
    SteamAPI_ISteamApps_BIsSubscribedApp
EOF
"$dlltool" -m i386 -k -d "$tmp/steam_api.def" -l "$tmp/libsteam_api.a"
"$cc32" "${flags[@]}" -Wl,--entry,_fixture_start -Wl,--subsystem,console \
  -o "$tmp/x86-gbe-init.exe" "$here/x86-gbe-init-fixture.c" -L"$tmp" -lsteam_api -lkernel32
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror \
  "$here/x86-gbe-init.cpp" -o "$tmp/x86-gbe-init"
mkdir -p "$tmp/run/steam_settings"
printf '480\n' > "$tmp/run/steam_appid.txt"
printf '[main::connectivity]\ndisable_networking=1\n' > "$tmp/run/steam_settings/configs.main.ini"
cd "$tmp/run"
"$tmp/x86-gbe-init" "$tmp/x86-gbe-init.exe" "$provider"
