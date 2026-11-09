#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device proof that the PINNED gbe_fork provider reads the leaderboard cache
# file the launcher writes, with the launcher's own encoder.
#
# Why this test exists: play-runtime.sh's `leaderboards` mode uses a stand-in
# provider written in this repository, so it proves the bridge and the file
# format but never touches the real binary. gbe_fork reads a board's cache file
# once, on the first Find in the process; if it stopped doing that (a new pin, a
# changed path, a changed layout) the console would show an empty Rank screen
# and every other test would still pass. This one fails instead.
#
# Needs: wine, clang++-19, lld-link-19, llvm-dlltool-19, curl, 7z (fetch-gbe-fork.sh
# pulls and SHA-256-verifies the pinned archive), and a host C++ compiler for the
# seeder (CXX, default c++).
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
export WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/prun-wine}
export WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d
export CXX=${CXX:-c++}
for t in wine "${CXX}" clang++-19 lld-link-19 llvm-dlltool-19; do
  command -v "$t" >/dev/null || { echo "steam-leaderboard-gbe: missing tool: $t" >&2; exit 2; }
done
mkdir -p "$WINEPREFIX"
if [[ ! -f "$WINEPREFIX/system.reg" ]];then timeout -k 2 60 wineboot -u;fi

out=out/leaderboard-gbe
rm -rf "$out"; mkdir -p "$out/obj" "$out/run/steam_settings" "$out/run/saves"
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
 SteamAPI_SteamUserStats_v013
 SteamAPI_ISteamUserStats_FindLeaderboard
 SteamAPI_ISteamUserStats_DownloadLeaderboardEntries
 SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry
 SteamAPI_ISteamUserStats_GetLeaderboardEntryCount
 SteamAPI_ISteamUserStats_GetLeaderboardName
 SteamAPI_ManualDispatch_Init
 SteamAPI_ManualDispatch_RunFrame
 SteamAPI_ManualDispatch_GetNextCallback
 SteamAPI_ManualDispatch_GetAPICallResult
 SteamAPI_ManualDispatch_FreeLastCallback
DEF
llvm-dlltool-19 -m i386:x86-64 -d "$out/obj/steam_api64.def" -l "$out/obj/steam_api64.lib"
clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -c tests/steam-leaderboard-gbe.cpp -o "$out/obj/guest.obj"
lld-link-19 /nodefaultlib /subsystem:console,6.02 /entry:guest_start /out:"$out/run/guest.exe" \
  "$out/obj/guest.obj" "$out/obj/steam_api64.lib" \
  /usr/x86_64-w64-mingw32/lib/libuser32.a /usr/x86_64-w64-mingw32/lib/libkernel32.a

# The settings the launcher stages for the provider (launch.h stage_steam_provider).
printf '3405340\n' > "$out/run/steam_settings/steam_appid.txt"
printf '[user::general]\naccount_name=Player\naccount_steamid=76561190000000001\nlanguage=english\n[user::saves]\nlocal_save_path=./saves\n' \
  > "$out/run/steam_settings/configs.user.ini"
printf '[main::connectivity]\ndisable_networking=1\n' > "$out/run/steam_settings/configs.main.ini"

# Seed with the launcher's encoder, then run the guest against the real provider.
"${CXX}" -std=c++17 -O1 -I. tests/steam-leaderboard-seed.cpp -o "$out/seed"
"$out/seed" "$out/run/saves" >/dev/null
( cd "$out/run" && timeout -k 5 90 wine guest.exe ) > "$out/seeded.txt" 2>&1 || true
tail -n +1 "$out/seeded.txt"

fail() { echo "STEAM-LEADERBOARD-GBE FAIL: $1" >&2; exit 1; }
grep -q '^RESULT find found=1 name=v2_kills' "$out/seeded.txt" || fail "provider did not find the board"
grep -q '^RESULT entry_count=3' "$out/seeded.txt" || fail "provider did not read 3 rows from the launcher's file"
grep -q '^RESULT download_count=3' "$out/seeded.txt" || fail "provider did not download 3 rows"
grep -q '^RESULT entry_0 rank=1 score=1500 user_set=1' "$out/seeded.txt" || fail "top row is not the seeded top score"
grep -q '^RESULT entry_2 rank=3 score=400 user_set=1' "$out/seeded.txt" || fail "last row is not the seeded last score"

# Control: the same guest with no cache file must see the empty board it saw
# before the bridge existed. Without this, the checks above could pass on a
# provider that invents rows.
rm -f "$out/run/saves/3405340/leaderboard/v2_kills"
( cd "$out/run" && timeout -k 5 90 wine guest.exe ) > "$out/unseeded.txt" 2>&1 || true
grep -q '^RESULT entry_count=0' "$out/unseeded.txt" || fail "unseeded board is not empty"
grep -q '^RESULT download_count=0' "$out/unseeded.txt" || fail "unseeded board downloaded rows"

echo "STEAM-LEADERBOARD-GBE PASS: the pinned provider reads the launcher's cache file (3 rows, ranks 1-3) and an unseeded board stays empty"
