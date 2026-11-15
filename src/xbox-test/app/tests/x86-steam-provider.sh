#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Owned PE32 Steam API import and staged SteamStub client routing fixture.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true); fi
[[ -n $cc32 ]] || { echo "x86-steam-provider: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
dlltool="$(dirname "$cc32")/i686-w64-mingw32-dlltool"
[[ -x $dlltool ]] || { echo "x86-steam-provider: no i686-w64-mingw32-dlltool" >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-steam-provider.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
flags=(-O1 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -nostdlib)
"$cc32" "${flags[@]}" -DSTEAM_PROVIDER_DLL -shared -Wl,--entry,0 -Wl,--image-base,0x20000000 \
  -Wl,--export-all-symbols -o "$tmp/steam_api.dll" "$here/x86-steam-provider-fixture.c"
cat > "$tmp/steam_api.def" <<'EOF'
LIBRARY steam_api.dll
EXPORTS
    SteamAPI_Init=_SteamAPI_Init
EOF
"$dlltool" -m i386 -k -d "$tmp/steam_api.def" -l "$tmp/libsteam_api.a"
"$cc32" "${flags[@]}" -Wl,--entry,_fixture_main -Wl,--subsystem,console \
  -o "$tmp/fixture.exe" "$here/x86-steam-provider-fixture.c" -L"$tmp" -lsteam_api -ladvapi32
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror \
  "$here/x86-steam-provider.cpp" -o "$tmp/x86-steam-provider"
"$tmp/x86-steam-provider" "$tmp/fixture.exe" "$tmp/steam_api.dll"
