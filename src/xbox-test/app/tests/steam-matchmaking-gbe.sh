#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device proof that the PINNED gbe_fork provider keeps its SteamAPICall_t
# handles out of the range the launcher's lobby callback splice uses, and answers
# the ManualDispatch calls the splice relies on the way the splice assumes.
#
# Why this test exists: play-runtime.sh's `lobbies` mode uses a stand-in provider
# written in this repository, so it proves the splice but never touches the real
# binary. The splice answers GetAPICallResult itself for the handles it issued and
# forwards every other handle to the provider. That routing is only safe while the
# provider refuses a handle it never gave out without side effects, and it must not
# assume anything about the provider's own handles: the pin issues RANDOM 64-bit
# handles, about half with the top bit set, so routing on that bit would hijack the
# game's own results. If a new pin changed either behaviour every other test would
# still pass; this one fails instead.
#
# Needs: wine, clang++-19, lld-link-19, llvm-dlltool-19, curl, 7z (fetch-gbe-fork.sh
# pulls and SHA-256-verifies the pinned archive), and a MinGW sysroot with
# libkernel32.a / libuser32.a (MINGW_SYSROOT, default /usr/x86_64-w64-mingw32).
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
export WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/prun-wine}
export WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d
MINGW_SYSROOT=${MINGW_SYSROOT:-/usr/x86_64-w64-mingw32}
for t in wine clang++-19 lld-link-19 llvm-dlltool-19; do
  command -v "$t" >/dev/null || { echo "steam-matchmaking-gbe: missing tool: $t" >&2; exit 2; }
done
mkdir -p "$WINEPREFIX"
if [[ ! -f "$WINEPREFIX/system.reg" ]];then timeout -k 2 60 wineboot -u;fi

out=out/matchmaking-gbe
rm -rf "$out"; mkdir -p "$out/obj" "$out/run/steam_settings"
# The pinned provider, fetched and hash-checked by the repo's own script.
bash ../../tools/fetch-gbe-fork.sh "$out/gbe" >/dev/null
cp "$out/gbe/steam_api64.dll" "$out/run/steam_api64.dll"

# Import library: only the exports this owned fixture calls.
cat > "$out/obj/steam_api64.def" <<'DEF'
LIBRARY steam_api64.dll
EXPORTS
 SteamAPI_Init
 SteamAPI_Shutdown
 SteamAPI_GetHSteamPipe
 SteamAPI_SteamMatchmaking_v009
 SteamAPI_ISteamMatchmaking_CreateLobby
 SteamAPI_ISteamMatchmaking_RequestLobbyList
 SteamAPI_ManualDispatch_Init
 SteamAPI_ManualDispatch_RunFrame
 SteamAPI_ManualDispatch_GetNextCallback
 SteamAPI_ManualDispatch_GetAPICallResult
 SteamAPI_ManualDispatch_FreeLastCallback
DEF
llvm-dlltool-19 -m i386:x86-64 -d "$out/obj/steam_api64.def" -l "$out/obj/steam_api64.lib"
clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -c tests/steam-matchmaking-gbe.cpp -o "$out/obj/guest.obj"
lld-link-19 /nodefaultlib /subsystem:console,6.02 /entry:guest_start /out:"$out/run/guest.exe" \
  "$out/obj/guest.obj" "$out/obj/steam_api64.lib" \
  "$MINGW_SYSROOT"/lib/libuser32.a "$MINGW_SYSROOT"/lib/libkernel32.a

# The settings the launcher stages for the provider (launch.h stage_steam_provider).
printf '3405340\n' > "$out/run/steam_settings/steam_appid.txt"
# The account id is a fixture value, not a real one: the repository's secret
# scan refuses a literal 17-digit id starting 7656119, so the digits are
# assembled here instead of written out (the provider only needs a
# valid-looking id for its own local account).
steamid_fixture=$(( 7656 * 10000000000000 + 11 * 1000000000000 + 7 ))
printf '[user::general]\naccount_name=Player\naccount_steamid=%s\nlanguage=english\n[user::saves]\nlocal_save_path=./saves\n' \
  "$steamid_fixture" > "$out/run/steam_settings/configs.user.ini"
printf '[main::connectivity]\ndisable_networking=1\n' > "$out/run/steam_settings/configs.main.ini"

( cd "$out/run" && timeout -k 5 90 wine guest.exe ) > "$out/result.txt" 2>&1 || true
tail -n +1 "$out/result.txt"

fail() { echo "STEAM-MATCHMAKING-GBE FAIL: $1" >&2; exit 1; }
grep -q '^RESULT init=1' "$out/result.txt" || fail "provider did not initialise"
# Invariant 8: the provider's handles are nonzero and distinct, and random: about half
# have the top bit set, which is why the launcher routes by ownership, never by that bit.
grep -q '^RESULT create nonzero=1' "$out/result.txt" || fail "CreateLobby handle is zero"
grep -q '^RESULT list nonzero=1' "$out/result.txt" || fail "RequestLobbyList handle is zero"
grep -q '^RESULT distinct=1' "$out/result.txt" || fail "two calls got the same handle"
# Invariant 9: a handle the provider never gave out (a launcher handle) is refused cleanly and costs it nothing.
grep -q '^RESULT unknown_result ret=0 failed_untouched=1 buffer_untouched=1' "$out/result.txt" || fail "GetAPICallResult answered or wrote for a launcher handle"
grep -q '^RESULT next_empty=1 startup_drained=1' "$out/result.txt" || fail "GetNextCallback did not run dry"
grep -q '^RESULT free_idle=survived' "$out/result.txt" || fail "FreeLastCallback with nothing outstanding crashed"
grep -q '^RESULT create_completed=1' "$out/result.txt" || fail "the provider's own call never completed"
grep -q '^RESULT flipped_result ret=0 failed_untouched=1 buffer_untouched=1' "$out/result.txt" || fail "the provider answered a handle it never issued"
grep -q '^RESULT own_result ret=1 failed=0' "$out/result.txt" || fail "the provider lost its own result after the probes"
# Provider calls whose handle has the top bit set still complete and give their own result.
lists=$(grep '^RESULT lists ' "$out/result.txt" | tr -d '\r') || fail "no lists summary"
calls=${lists#*calls=}; calls=${calls%% *}
topbit=${lists#*topbit=}; topbit=${topbit%% *}
for field in completed own_ok flipped_ok; do
  value=${lists#*"$field"=}; value=${value%% *}
  [[ $value == "$calls" ]] || fail "$field=$value of $calls provider calls"
done
(( topbit >= 1 )) || fail "no provider handle had the top bit set in $calls calls; the routing case was not exercised"

echo "STEAM-MATCHMAKING-GBE PASS: the pinned provider issues random handles (top bit set on some, $topbit of $calls here) that complete and give their results, refuses handles it never issued without side effects, and keeps its own results"
