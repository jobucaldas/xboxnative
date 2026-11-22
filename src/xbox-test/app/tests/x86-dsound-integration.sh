#!/usr/bin/env bash
# Builds the real DirectSound shim and a PE32 guest, then exercises both through
# the interpreter's x86 COM proxy under Wine. Requires Wine's XAudio2 backend.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../../.." && pwd)"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-dsound.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cc32="${I686_CXX:-$(command -v i686-w64-mingw32-g++ || true)}"
cc64="${X86_64_CXX:-$(command -v x86_64-w64-mingw32-g++ || true)}"
[[ -n $cc32 && -n $cc64 ]] || { echo "X86-DSOUND FAIL: need i686 and x86_64 mingw g++" >&2; exit 1; }
extra=()
for d in /nix/store/*-mcfgthread-i686-w64-mingw32-*/lib; do [[ -d $d ]] && extra+=("-L$d"); done
extra64=()
for d in /nix/store/*-mcfgthread-x86_64-w64-mingw32-*; do
  [[ -d $d/include ]] && extra64+=("-I$d/include")
  [[ -d $d/lib ]] && extra64+=("-L$d/lib")
done
WINEPREFIX="${WINEPREFIX:-/tmp/xbn-feature-dsound-xaudio2-wine}"
export WINEPREFIX
mkdir -p "$WINEPREFIX"
shim="$tmp/dsound"
bash "$root/src/xbox-test/uwph/dsound/build.sh" "$shim"
cp "$shim/dsound.dll" "$tmp/dsound-shim.dll"
"$cc32" -std=c++17 -O1 -Wall -Wextra -Werror -static -static-libgcc -Wl,--no-seh "${extra[@]}" -DX86_DSOUND_GUEST \
  "$here/x86-dsound-integration.cpp" -o "$tmp/dsound-fixture.exe" -ldsound -lole32 -luuid
"$cc64" -std=c++17 -O1 -mno-red-zone -Wall -Wextra -Werror -Wno-error=cast-function-type -Wno-error=misleading-indentation -Wno-error=unused-parameter -Wno-error=sign-compare "${extra64[@]}" -static -static-libgcc -static-libstdc++ \
  "$here/x86-dsound-integration.cpp" -o "$tmp/x86-dsound-integration.exe" -lws2_32 -luuid
wine="${WINE:-wine}"
command -v "$wine" >/dev/null || { echo "X86-DSOUND FAIL: Wine not found" >&2; exit 1; }
WINEDEBUG=-all "$wine" "$tmp/x86-dsound-integration.exe" "$tmp/dsound-shim.dll" "$tmp/dsound-fixture.exe"
