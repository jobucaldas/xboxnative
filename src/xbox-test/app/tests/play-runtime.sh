#!/usr/bin/env bash
# Off-device Wine proof for the same runtime used by launcher Play. No console claim.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
export WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/prun-wine}
export WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d
mkdir -p "$WINEPREFIX"
if [[ ! -f "$WINEPREFIX/system.reg" ]];then timeout -k 2 60 wineboot -u;fi
out=out/play-tests
mkdir -p "$out/obj"
printf 'LIBRARY play-missing.dll\nEXPORTS\nDeliberatelyMissingPlayImport\n' > "$out/obj/missing.def"
llvm-dlltool-19 -m i386:x86-64 -d "$out/obj/missing.def" -l "$out/obj/missing.lib"
cat > "$out/obj/forward.def" <<'DEF'
LIBRARY play-forward.dll
EXPORTS
 ForwardName=play-forward-target.dependency_check @11
 ForwardOrdinal="play-forward-target.#1" @12
 ForwardDynamic=play-lazy-target.dependency_check @15
 ForwardSteam=steam_api64.SteamAPI_Init @16
 ForwardRejected=play-reject.dependency_check @17
 ForwardRejectedOrdinal="play-reject.#1" @18
 MissingPending=play-pending-target.DeliberatelyAbsentOptionalExport @19
 PendingName=play-pending-target.dependency_check @20
 PendingOrdinal="play-pending-target.#1" @21
 MissingPendingReject=play-pending-reject.DeliberatelyAbsentOptionalExport @22
 PendingRejectName=play-pending-reject.dependency_check @23
 PendingRejectOrdinal="play-pending-reject.#1" @24
 Loop=play-forward.Loop @13
 Absent=play-forward-target.DeliberatelyAbsentOptionalExport @14
DEF
printf 'int forwarder_anchor; void *forwarder_relocation = &forwarder_anchor;\n' | clang++-19 --target=x86_64-w64-windows-gnu -x c -c - -o "$out/obj/forward.obj"
lld-link-19 "$out/obj/forward.obj" /include:forwarder_anchor /include:forwarder_relocation /dll /noentry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /def:"$out/obj/forward.def" /out:"$out/play-forward.dll" /implib:"$out/obj/forward.lib"
for kind in dll exe;do
 flags=();[[ $kind == dll ]]&&flags=(-DPLAY_DLL)
 clang++-19 --target=x86_64-w64-windows-gnu -fno-emulated-tls -O1 -fno-stack-protector "${flags[@]}" -c tests/play-guest.cpp -o "$out/obj/$kind.obj"
done
clang++-19 --target=x86_64-w64-windows-gnu -fno-emulated-tls -O1 -fno-stack-protector -DPLAY_STATIC_MISSING -c tests/play-guest.cpp -o "$out/obj/missing.obj"
clang++-19 --target=x86_64-w64-windows-gnu -fno-emulated-tls -O1 -fno-stack-protector -DPLAY_STATIC_STEAM -c tests/play-guest.cpp -o "$out/obj/steam.obj"
clang++-19 --target=x86_64-w64-windows-gnu -fno-emulated-tls -O1 -fno-stack-protector -DPLAY_DLL -DPLAY_STATIC_MISSING -c tests/play-guest.cpp -o "$out/obj/unresolved.obj"
clang++-19 --target=x86_64-w64-windows-gnu -fno-emulated-tls -O1 -fno-stack-protector -DPLAY_DLL -DPLAY_REJECT_ATTACH -c tests/play-guest.cpp -o "$out/obj/reject.obj"
common=(/nodefaultlib /include:_tls_used /alternatename:tls_raw=tls_value /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat)
x86_64-w64-mingw32-windres ../uwph/kernel32/resource_fixture.rc -O coff -o "$out/obj/resources.obj"
lld-link-19 "${common[@]}" /dll /entry:guest_dll_entry /out:"$out/play-dependency.dll" /implib:"$out/obj/dependency.lib" "$out/obj/dll.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
lld-link-19 "${common[@]}" /entry:guest_exe_entry /out:"$out/play-guest.exe" "$out/obj/exe.obj" "$out/obj/resources.obj" "$out/obj/dependency.lib" "$out/obj/forward.lib" "$out/obj/missing.lib" /usr/x86_64-w64-mingw32/lib/libuser32.a /usr/x86_64-w64-mingw32/lib/libadvapi32.a /usr/x86_64-w64-mingw32/lib/libkernel32.a
lld-link-19 "${common[@]}" /entry:guest_exe_entry /out:"$out/play-missing.exe" "$out/obj/missing.obj" "$out/obj/dependency.lib" "$out/obj/forward.lib" "$out/obj/missing.lib" /usr/x86_64-w64-mingw32/lib/libuser32.a /usr/x86_64-w64-mingw32/lib/libadvapi32.a /usr/x86_64-w64-mingw32/lib/libkernel32.a
lld-link-19 "${common[@]}" /entry:guest_exe_entry /out:"$out/play-steam.exe" "$out/obj/steam.obj" "$out/obj/dependency.lib" "$out/obj/forward.lib" /usr/x86_64-w64-mingw32/lib/libuser32.a /usr/x86_64-w64-mingw32/lib/libadvapi32.a /usr/x86_64-w64-mingw32/lib/libkernel32.a
lld-link-19 "${common[@]}" /dll /entry:guest_dll_entry /out:"$out/play-unresolved.dll" "$out/obj/unresolved.obj" "$out/obj/missing.lib" /usr/x86_64-w64-mingw32/lib/libkernel32.a
# EXE that reads its TLS through TEB slot 0, as MSVC /GA code does (no _tls_index).
clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -c tests/play-slot0.cpp -o "$out/obj/slot0.obj"
lld-link-19 /nodefaultlib /include:_tls_used /entry:slot0_exe_entry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-slot0.exe" "$out/obj/slot0.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
# EXE whose D3D11 device (through Play's D3D11CreateDevice) rejects calls; Play must log them.
clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -c tests/play-d3d11.cpp -o "$out/obj/d3d11.obj"
lld-link-19 /nodefaultlib /entry:d3d_entry /include:d3d_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-d3d11.exe" "$out/obj/d3d11.obj" /usr/x86_64-w64-mingw32/lib/libd3d11.a /usr/x86_64-w64-mingw32/lib/libkernel32.a
# EXE that renders D3D12 frames through Play's routes: device, queue, flip-model
# swap chain on a guest HWND, RTVs, clear, present, fence wait. Play must log the
# device it got and the calls the device rejects, once each.
clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -c tests/play-d3d12.cpp -o "$out/obj/d3d12.obj"
lld-link-19 /nodefaultlib /entry:d3d12_entry /include:d3d12_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-d3d12.exe" "$out/obj/d3d12.obj" /usr/x86_64-w64-mingw32/lib/libd3d12.a /usr/x86_64-w64-mingw32/lib/libdxgi.a /usr/x86_64-w64-mingw32/lib/libuser32.a /usr/x86_64-w64-mingw32/lib/libdxguid.a /usr/x86_64-w64-mingw32/lib/libkernel32.a
# EXE that starts a D3D10 game: it statically imports d3d10.dll, which the
# console lacks, so Play must serve the shim from the runtime directory and the
# shim's own d3d11/dxgi imports must reach Play's routes. It creates a device
# with a swap chain on a guest HWND, clears and presents three frames.
clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -c tests/play-d3d10.cpp -o "$out/obj/d3d10.obj"
lld-link-19 /nodefaultlib /entry:d3d10_entry /include:d3d10_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-d3d10.exe" "$out/obj/d3d10.obj" /usr/x86_64-w64-mingw32/lib/libd3d10.a /usr/x86_64-w64-mingw32/lib/libdxgi.a /usr/x86_64-w64-mingw32/lib/libuser32.a /usr/x86_64-w64-mingw32/lib/libuuid.a /usr/x86_64-w64-mingw32/lib/libkernel32.a
# EXE that starts a D3D9 game: it statically imports d3d9.dll, which the console
# lacks, so Play must serve the shim from the runtime directory and the shim's
# own d3d11/dxgi imports must reach Play's routes. It creates a device with a
# swap chain and an auto depth-stencil surface on a guest HWND, clears and
# presents three frames.
clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -c tests/play-d3d9.cpp -o "$out/obj/d3d9.obj"
lld-link-19 /nodefaultlib /entry:d3d9_entry /include:d3d9_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-d3d9.exe" "$out/obj/d3d9.obj" /usr/x86_64-w64-mingw32/lib/libd3d9.a /usr/x86_64-w64-mingw32/lib/libuser32.a /usr/x86_64-w64-mingw32/lib/libkernel32.a
# EXEs that draw with D3D9 through the shim: one fixed-function triangle, one
# with a game's own vs_1_1/ps_1_1, a texture and a vertex declaration. Both read
# their pixels back and exit 42 only when the draw landed where it should.
for fixture in triangle textured;do
  clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -c "tests/play-d3d9-$fixture.cpp" -o "$out/obj/d3d9-$fixture.obj"
  lld-link-19 /nodefaultlib /entry:d3d9_${fixture}_entry /include:d3d9_${fixture}_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-d3d9-$fixture.exe" "$out/obj/d3d9-$fixture.obj" /usr/x86_64-w64-mingw32/lib/libd3d9.a /usr/x86_64-w64-mingw32/lib/libuser32.a /usr/x86_64-w64-mingw32/lib/libkernel32.a
done
# EXE that probes Vulkan the way a Vulkan-optional engine does: it loads
# vulkan-1.dll by name (Play serves the Khronos loader from the runtime
# directory), creates an instance and enumerates physical devices. The console
# has no driver, so the answer must be zero devices with
# VK_ERROR_INITIALIZATION_FAILED, as on a desktop with no GPU.
clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -c tests/play-vulkan.cpp -o "$out/obj/vulkan.obj"
lld-link-19 /nodefaultlib /entry:vk_entry /include:vk_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-vulkan.exe" "$out/obj/vulkan.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
# EXE that runs the Vulkan startup path a game takes against the packaged dzn
# ICD (Vulkan on D3D12): instance with VK_KHR_surface/VK_KHR_win32_surface, a
# real physical device, a win32 surface on a guest HWND, device, swap chain,
# render pass, framebuffers, clear and present. Exits 42 when a frame is
# presented, 43 when the host's D3D12 refuses dzn's device (BLOCKED, not a pass).
clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -fno-builtin -c tests/play-vulkan-dzn.cpp -o "$out/obj/vulkan-dzn.obj"
lld-link-19 /nodefaultlib /entry:vk_dzn_entry /include:vk_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-vulkan-dzn.exe" "$out/obj/vulkan-dzn.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a /usr/x86_64-w64-mingw32/lib/libuser32.a
# A guest that uses a Discord SDK as a game does. The SDK is our own stand-in
# (never Discord's binary): it connects to \?\pipe\discord-ipc-N exactly as
# discord-rpc and the Game SDK do, so Play's answer is what a real game sees.
clang++-19 --target=x86_64-w64-windows-gnu -fno-emulated-tls -O1 -fno-stack-protector -c tests/play-discord-sdk.cpp -o "$out/obj/discord-sdk.obj"
lld-link-19 /nodefaultlib /dll /entry:DllMain /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-discord-sdk.dll" "$out/obj/discord-sdk.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
clang++-19 --target=x86_64-w64-windows-gnu -fno-emulated-tls -O1 -fno-stack-protector -c tests/play-discord.cpp -o "$out/obj/discord.obj"
lld-link-19 /nodefaultlib /entry:discord_entry /include:discord_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-discord.exe" "$out/obj/discord.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
# Steam leaderboards (steam_leaderboards.h). The provider stand-in is our own
# code reproducing gbe_fork's one relevant behaviour: it reads a board's file on
# the first Find and answers downloads from it. The guest statically imports
# steam_api64.dll, as a game built against the Steamworks SDK does, so Play's
# provider substitution is what serves it.
cat > "$out/obj/steam-api.def" <<'DEF'
LIBRARY steam_api64.dll
EXPORTS
 SteamAPI_ISteamUserStats_FindLeaderboard
 SteamAPI_ISteamUserStats_FindOrCreateLeaderboard
 SteamAPI_ISteamUserStats_DownloadLeaderboardEntries
 SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry
 SteamAPI_ISteamUserStats_UploadLeaderboardScore
 SteamAPI_ISteamUserStats_GetLeaderboardName
 SteamAPI_ISteamFriends_GetFriendPersonaName
 SteamAPI_ISteamFriends_GetPersonaName
 SteamAPI_ISteamFriends_RequestUserInformation
 PlayLeaderboardTrace
 PlayLeaderboardCallBoard
 PlayLeaderboardPersonaCalls
DEF
llvm-dlltool-19 -m i386:x86-64 -d "$out/obj/steam-api.def" -l "$out/obj/steam-api.lib"
clang++-19 --target=x86_64-w64-windows-gnu -fno-emulated-tls -O1 -fno-stack-protector -c tests/play-leaderboard-provider.cpp -o "$out/obj/leaderboard-provider.obj"
lld-link-19 /nodefaultlib /include:provider_relocation /dll /entry:DllMain /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-leaderboard-provider.dll" /implib:"$out/obj/provider.lib" "$out/obj/leaderboard-provider.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
clang++-19 --target=x86_64-w64-windows-gnu -fno-emulated-tls -O1 -fno-stack-protector -c tests/play-leaderboard.cpp -o "$out/obj/leaderboard.obj"
lld-link-19 /nodefaultlib /entry:lb_entry /include:lb_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-leaderboard.exe" "$out/obj/leaderboard.obj" "$out/obj/steam-api.lib" /usr/x86_64-w64-mingw32/lib/libkernel32.a
# A static import from a DLL whose own mapping fails must stay unresolved: the
# failed image is never published to resolve the importer (never executed).
# baselib.dll is a game-DLL (uwph-routed) name, the route Megabonk exercised.
printf 'LIBRARY baselib.dll\nEXPORTS\n dependency_check\n' > "$out/obj/unresolved-import.def"
llvm-dlltool-19 -m i386:x86-64 -d "$out/obj/unresolved-import.def" -l "$out/obj/unresolved-import.lib"
printf '__declspec(dllimport) int dependency_check(void);\nint failed_anchor; void *failed_relocation = &failed_anchor;\nint failed_dependency_entry(void) { return dependency_check() ? 42 : 43; }\n' | clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -x c -c - -o "$out/obj/failed-dependency.obj"
lld-link-19 /nodefaultlib /entry:failed_dependency_entry /include:failed_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-failed-dependency.exe" "$out/obj/failed-dependency.obj" "$out/obj/unresolved-import.lib"
# CRT exit family imported from the API set (as MSVC /MD binaries do): Play
# contains them, so the guest ends and the runner process survives.
printf 'LIBRARY api-ms-win-crt-runtime-l1-1-0.dll\nEXPORTS\n exit\n abort\n' > "$out/obj/crt-runtime.def"
llvm-dlltool-19 -m i386:x86-64 -d "$out/obj/crt-runtime.def" -l "$out/obj/crt-runtime.lib"
for kind in exit abort;do
 printf '__declspec(dllimport) void exit(int);\n__declspec(dllimport) void abort(void);\nint crt_anchor; void *crt_relocation = &crt_anchor;\nint crt_entry(void) { %s; return 41; }\n' "$([[ $kind == exit ]] && echo 'exit(37)' || echo 'abort()')" | clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -x c -c - -o "$out/obj/crt-$kind.obj"
 lld-link-19 /nodefaultlib /entry:crt_entry /include:crt_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-crt-$kind.exe" "$out/obj/crt-$kind.obj" "$out/obj/crt-runtime.lib"
done
cat > "$out/obj/reject.def" <<'DEF'
LIBRARY play-reject.dll
EXPORTS
 dependency_check @1
 RejectedForward=play-dependency.dependency_check @2
DEF
lld-link-19 "${common[@]}" /dll /entry:guest_dll_entry /def:"$out/obj/reject.def" /out:"$out/play-reject.dll" "$out/obj/reject.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
cp "$out/play-dependency.dll" "$out/play-dynamic.dll"
cp "$out/play-dependency.dll" "$out/play-detach-target.dll"
cp "$out/play-dependency.dll" "$out/play-forward-target.dll"
cp "$out/play-dependency.dll" "$out/play-lazy-target.dll"
mkdir -p "$out/loader-search"
cp "$out/play-dependency.dll" "$out/loader-search/play-search.dll"
cp "$out/play-dependency.dll" "$out/loader-search/café.dll"
for role in dependency consumer;do
 flags=();[[ $role == consumer ]]&&flags=(-DSEARCH_CONSUMER)
 clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector "${flags[@]}" -c ../uwph/kernel32/guest_loader_search_dependency.cpp -o "$out/obj/search-$role.obj"
done
lld-link-19 /nodefaultlib /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /dll /entry:search_entry /out:"$out/loader-search/search-dependency.dll" /implib:"$out/obj/search-dependency.lib" "$out/obj/search-dependency.obj"
lld-link-19 /nodefaultlib /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /dll /entry:search_entry /out:"$out/loader-search/search-consumer.dll" "$out/obj/search-consumer.obj" "$out/obj/search-dependency.lib"

cp "$out/play-dependency.dll" "$out/play-pending-target.dll"
cp "$out/play-reject.dll" "$out/play-pending-reject.dll"
CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
UWP_OBJ_DIR="$out/obj/cxx" "$CB/scripts/build.sh" --out "$out/play-cxx.dll" --link-arg /dll tests/play-cxx.cpp -- /MT /O2 /EHsc
# Relink against the desktop import names used by the owned game. WindowsApp
# otherwise selects API-set names outside the production import allowlist.
crt=${XWIN_ROOT:-$HOME/.cache/uwp-crossbuild/xwin}
lld-link-19 "$out"/obj/cxx/tests_play-cxx.*.obj /dll /out:"$out/play-cxx.dll" \
 /libpath:"$crt/crt/lib/x86_64" /libpath:"$crt/sdk/lib/um/x86_64" \
 /libpath:"$crt/sdk/lib/ucrt/x86_64" kernel32.lib
# A /MD C++ DLL importing vcruntime140.dll and vcruntime140_1.dll, as games
# built for the VC++ redistributable do. No redistributable copy is present.
printf 'LIBRARY vcruntime140_1.dll\nEXPORTS\n __CxxFrameHandler4\n' > "$out/obj/vcrt1.def"
llvm-dlltool-19 -m i386:x86-64 -d "$out/obj/vcrt1.def" -l "$out/obj/play-vcrt1.lib"
UWP_OBJ_DIR="$out/obj/vcrt" "$CB/scripts/build.sh" --out "$out/play-vcrt.dll" --link-arg /dll tests/play-vcrt.cpp -- /MD /O2 /EHsc /GR
lld-link-19 "$out"/obj/vcrt/tests_play-vcrt.*.obj /dll /nodefaultlib /out:"$out/play-vcrt.dll" \
 /libpath:"$crt/crt/lib/x86_64" /libpath:"$crt/sdk/lib/um/x86_64" \
 /libpath:"$crt/sdk/lib/ucrt/x86_64" msvcrt.lib vcruntime.lib ucrt.lib kernel32.lib "$out/obj/play-vcrt1.lib"
llvm-readobj --coff-imports "$out/play-vcrt.dll" | grep -q 'Name: VCRUNTIME140.dll'
llvm-readobj --coff-imports "$out/play-vcrt.dll" | grep -q 'Name: VCRUNTIME140_1.dll'
rm -f "$out/vcruntime140.dll" "$out/vcruntime140_1.dll"
# A /MD C++ DLL using msvcp140.dll, served from Play's runtime directory
# (Wine's build from the launcher layout); no copy beside the game.
UWP_OBJ_DIR="$out/obj/msvcp" "$CB/scripts/build.sh" --out "$out/play-msvcp.dll" --link-arg /dll tests/play-msvcp.cpp -- /MD /O2 /EHsc /GR
lld-link-19 "$out"/obj/msvcp/tests_play-msvcp.*.obj /dll /nodefaultlib /out:"$out/play-msvcp.dll" \
 /libpath:"$crt/crt/lib/x86_64" /libpath:"$crt/sdk/lib/um/x86_64" \
 /libpath:"$crt/sdk/lib/ucrt/x86_64" msvcrt.lib msvcprt.lib vcruntime.lib ucrt.lib kernel32.lib
llvm-readobj --coff-imports "$out/play-msvcp.dll" | grep -q 'Name: MSVCP140.dll'
rm -f "$out/msvcp140.dll"
rm -rf "$out/runtime";mkdir -p "$out/runtime";cp out/layout/runtime/*.dll out/layout/runtime/*.json "$out/runtime/"
# Source-built donors from the launcher layout, as packaged: the Vulkan loader
# imports setupapi's cfgmgr32 family, which Play serves from the setupapi donor.
mkdir -p "$out/donors";cp out/layout/donors/*.dll "$out/donors/"
bash ../uwph/host/build-sqlite-lib.sh "$out/obj/sqlite3.lib"
UWP_OBJ_DIR="$out/obj/runner" "$CB/scripts/build.sh" --uwp --link-arg "$out/obj/sqlite3.lib" --out "$out/play-runner.exe" host/tls_slot0.cpp tests/play-runner.cpp -- /MT /O2 /Iout/obj /I../uwph/host/third_party/sqlite
# Use the exact packaged carriers from the launcher build.
cp out/layout/play-tls-*.dll out/layout/uwph-u32c.dll "$out/"
# Owned stand-in so steam-client proves refusal of a present root client DLL.
cp "$out/play-dependency.dll" "$out/steamclient64.dll"
# Steam leaderboards run on their own: the provider stand-in is staged as the
# Steam API provider, and the bridge's save root is a fresh directory so the
# first Find has no file to read.
lb="$out/leaderboards"; rm -rf "$lb"; mkdir -p "$lb/saves/3405340/leaderboard"
cp "$out/play-leaderboard-provider.dll" "$lb/steam_api64.dll"
# The guest's own steam_api64.dll, as every Steamworks game ships one: Play maps
# the configured provider in its place and leaves this file untouched.
cp "$out/play-leaderboard-provider.dll" "$out/steam_api64.dll"
PLAY_LB_SAVE_PATH=$(winepath -w "$lb/saves") PLAY_LB_APPID=3405340 \
PRUN_STEAM_PROVIDER=$(winepath -w "$lb/steam_api64.dll") PRUN_LB_SAVES=$(winepath -w "$lb/saves") PRUN_LB_APPID=3405340 \
 WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$out/play-runner.exe" leaderboards > "$out/leaderboards.log" 2>&1
tail -1 "$out/leaderboards.log"
# The root copy was only for this run: later modes prove what happens when a
# game root has no steam_api64.dll of its own.
rm -f "$out/steam_api64.dll"
for mode in observer-log reject-attach reject-mapping normal missing static-steam crash exit hang dialog wide-dialog wide-long dialog-milestone steam-api steam-client steam-forward loader-lock reject-export-name reject-export-ordinal reject-export-source-forward reject-export-target-name reject-export-target-ordinal reject-export-lazy startup-lock attach-publication pending-forward-name pending-forward-ordinal pending-reject-name pending-reject-ordinal native-cxx native-vcrt native-msvcp shell-execute loader-search loader-thread loader-query loader-lifetime resource-query thread-exit thread-exit-main rwx-jit slot0 d3d11-diag d3d12-diag d3d10-diag d3d9-diag d3d9-triangle d3d9-textured discord-sdk vulkan-dzn;do
 WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$out/play-runner.exe" "$mode" > "$out/$mode.log" 2>&1
 tail -1 "$out/$mode.log"
done
# Every vcruntime import took the ucrtbase route; none was left unresolved.
grep -q 'VCRT ROUTE vcruntime140.dll!__CxxFrameHandler3 found=1' "$out/native-vcrt.log"
grep -q 'VCRT ROUTE vcruntime140_1.dll!__CxxFrameHandler4 found=1' "$out/native-vcrt.log"
if grep -q 'VCRT ROUTE .* found=0' "$out/native-vcrt.log";then exit 1;fi
grep -q 'PRUN EVENT mapping msvcp140.dll' "$out/native-msvcp.log"
# The /GA-style fixture got the launcher's slot 0, not a packaged carrier.
grep -q 'PRUN EVENT TLS slot 0 for play-slot0.exe' "$out/slot0.log"
if grep -q 'TLS slot 0 unavailable' "$out/slot0.log";then exit 1;fi
grep -q 'PRUN OPTIONAL ShellExecuteExW refused' "$out/shell-execute.log"
# A Discord SDK is present and there is no Discord client to reach: Play says so
# once, and both SDK flavours return at once with the answer they return on a
# desktop where Discord is not running (the fixture exits 42 only then).
grep -q 'PRUN EVENT Discord SDK present (play-discord-sdk.dll); no Discord client on this console' "$out/discord-sdk.log"
[[ $(grep -c 'Discord SDK present' "$out/discord-sdk.log") == 1 ]]
# Steam leaderboards: the launcher seeds the provider's leaderboard file before
# the provider's first Find, so the game's own rank screen lists real players.
# The guest exits 42 only when the provider read three players and the unseeded
# board stayed empty; Play must also have said what it seeded.
grep -q "PRUN EVENT mapping steam_api64.dll" "$out/leaderboards.log"
grep -q "STEAM leaderboards: SteamAPI_ISteamUserStats_FindLeaderboard observed; rows come from the launcher's Steam session" "$out/leaderboards.log"
grep -q "STEAM leaderboards: 'v2_kills' seeded 3 rows (id 7, 3 on Steam, file written)" "$out/leaderboards.log"
grep -q "STEAM leaderboards: score 2000 sent to 'v2_kills' (changed=1, rank 0 -> 1)" "$out/leaderboards.log"
[[ $(grep -c "'v2_kills' seeded" "$out/leaderboards.log") == 1 ]]
if grep -q "v2_unseeded' seeded" "$out/leaderboards.log";then exit 1;fi
# Persona names: the provider stand-in answers "Unknown User" for every SteamID
# (gbe_fork's LAN-only friend list), and the launcher's persona tap replaces that
# with the name Steam gave, in memory, after the provider's own call ran. The
# guest exits 42 only when the names, the RequestUserInformation contract and the
# refreshed rows after its own upload all came from the launcher.
grep -q "STEAM persona: SteamAPI_ISteamFriends_GetFriendPersonaName observed; names come from the launcher's Steam session" "$out/leaderboards.log"
grep -q "STEAM persona: SteamAPI_ISteamFriends_GetPersonaName observed; names come from the launcher's Steam session" "$out/leaderboards.log"
grep -q "STEAM persona: SteamAPI_ISteamFriends_RequestUserInformation observed; names come from the launcher's Steam session" "$out/leaderboards.log"
grep -q "STEAM persona: 3 of 3 leaderboard names resolved" "$out/leaderboards.log"
grep -q "STEAM leaderboards: 'v2_kills' refreshed 4 rows (id 7, 4 on Steam)" "$out/leaderboards.log"
# Counts only: no persona name and no SteamID may reach the log.
if grep -qE "Player (Two|Three|Four)|Fixture Owner|1100001000" "$out/leaderboards.log";then exit 1;fi
# Vulkan (research/VULKAN-PLAN.md): a guest's LoadLibrary("vulkan-1.dll") is served by the Khronos loader from the
# runtime directory, and Play must have listed the packaged ICD manifests in the guest registry, which is the route
# the loader reads (VK_DRIVER_FILES is ignored at the process's integrity level).
# The launcher build packages dzn and the null ICD, so the milestone-1 fixture's "no devices" answer no longer holds
# against that runtime directory: the loader now finds a real physical device. The milestone-1 answer is asserted
# against a runtime directory without dzn instead (below), which is exactly what a PLAY_DZN=0 launcher ships.
if [ -f out/layout/runtime/vulkan-dzn.json ]; then
  grep -q 'PRUN EVENT mapping vulkan-1.dll' "$out/vulkan-dzn.log"
  grep -q 'PRUN EVENT Vulkan loader mapped from the runtime directory; the guest registry lists the packaged dzn driver (Vulkan on D3D12) with the null ICD as fallback' "$out/vulkan-dzn.log"
  grep -q 'PRUN EVENT mapping vulkan_dzn.dll' "$out/vulkan-dzn.log"
  [[ $(grep -c 'lists the packaged dzn driver' "$out/vulkan-dzn.log") == 1 ]]
else
  grep -q 'PRUN EVENT Vulkan loader mapped from the runtime directory; the guest registry lists the packaged null ICD' "$out/vulkan-dzn.log"
  grep -q 'PRUN EVENT mapping vulkan-null-icd.dll' "$out/vulkan-dzn.log"
  [[ $(grep -c 'the guest registry lists the packaged null ICD' "$out/vulkan-dzn.log") == 1 ]]
fi
# The milestone-1 answer, against a runtime directory that has no dzn manifest: the instance is created and the
# enumeration reports no devices (the fixture exits 42 only then, so the runner exits 0). This is the PLAY_DZN=0
# launcher's behaviour.
rm -rf "$out/runtime-null";mkdir -p "$out/runtime-null"
cp out/layout/runtime/*.dll out/layout/runtime/*.json "$out/runtime-null/"
rm -f "$out/runtime-null/vulkan_dzn.dll" "$out/runtime-null/vulkan-dzn.json"
# A fresh guest registry: the previous run's entries name the other runtime directory, and
# clearVulkanDriverFiles() only removes the manifests of the directory it is given.
rm -f "$out"/registry.sqlite*
PRUN_RUNTIME_DIR=$(winepath -w "$out/runtime-null") WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d \
 timeout -k 2 20 wine "$out/play-runner.exe" vulkan > "$out/vulkan-null-runtime.log" 2>&1
tail -1 "$out/vulkan-null-runtime.log"
grep -q 'PRUN EVENT Vulkan loader mapped from the runtime directory; the guest registry lists the packaged null ICD' "$out/vulkan-null-runtime.log"
grep -q 'PRUN EVENT mapping vulkan-null-icd.dll' "$out/vulkan-null-runtime.log"
# dzn (research/VULKAN-PLAN.md, milestone 2): the guest gets a real physical device and a win32 surface from the
# packaged ICD, and either presents a frame (PASS) or stops at device creation on a host whose D3D12 refuses dzn's
# device (BLOCKED, never a pass). The launcher build always packages dzn, so a missing manifest is a failure.
if [ -f out/layout/runtime/vulkan-dzn.json ]; then
  grep -q 'PLAY-VULKAN-DZN STAGE instance-created' "$out/vulkan-dzn.log"
  grep -q 'PLAY-VULKAN-DZN STAGE physical-device count=[1-9]' "$out/vulkan-dzn.log"
  grep -q 'PLAY-VULKAN-DZN STAGE win32-surface-created' "$out/vulkan-dzn.log"
  if grep -q 'PLAY-VULKAN-DZN PASS frame-presented' "$out/vulkan-dzn.log"; then
    echo "PLAY-VULKAN-DZN PASS frame-presented"
  elif grep -q 'PLAY-VULKAN-DZN BLOCKED device-creation' "$out/vulkan-dzn.log"; then
    echo "PLAY-VULKAN-DZN BLOCKED device-creation on this host; not a pass"
    [ "${PRUN_REQUIRE_DZN:-0}" = 1 ] && { echo "PLAY-VULKAN-DZN FAIL: PRUN_REQUIRE_DZN=1"; exit 1; }
  else
    echo "PLAY-VULKAN-DZN FAIL (see $out/vulkan-dzn.log)"; exit 1
  fi
else
  echo "PLAY-VULKAN-DZN FAIL: the launcher build did not package dzn"; exit 1
fi
# A guest's rejected D3D11 calls are logged by Play, once per distinct call, with
# the feature levels it asked for and got. (Results pass through: exit code 42.)
grep -q 'PRUN EVENT guest D3D11CreateDevice request type=1 flags=0x0 adapter=default levels=\[0xb100,0xb000\]' "$out/d3d11-diag.log"
grep -q 'PRUN EVENT guest D3D11CreateDevice feature level got=0x' "$out/d3d11-diag.log"
grep -q 'PRUN D3D11 CreateTexture2D hr=0x[0-9a-f]* fmt=0(UNKNOWN) 256x256 .*bind=0x8 ' "$out/d3d11-diag.log"
grep -q 'PRUN D3D11 CreateBuffer hr=0x[0-9a-f]* bytes=0 bind=0x1 ' "$out/d3d11-diag.log"
grep -q 'PRUN D3D11 CheckFormatSupport hr=0x[0-9a-f]* fmt=32767 support=0x0 UNSUPPORTED' "$out/d3d11-diag.log"
grep -q 'PRUN D3D11 adapter vendor=0x' "$out/d3d11-diag.log"
[[ $(grep -c 'PRUN D3D11 CreateTexture2D hr=' "$out/d3d11-diag.log") == 1 ]]
# A guest's D3D12 device, queue, flip-model swap chain on a guest HWND and its
# frames go through Play; the device answers shader model, and the calls it
# rejects are logged once per distinct call. (Exit code 42 = every step passed.)
grep -q 'PRUN EVENT guest D3D12CreateDevice request adapter=default minimum=0xb000 iid=189819f1-1db6-4b57-be54-1821339b85f7' "$out/d3d12-diag.log"
grep -q 'PRUN EVENT guest D3D12CreateDevice returned hr=0x00000000 device=' "$out/d3d12-diag.log"
grep -q 'PRUN D3D12 diagnostics on: failed D3D12 create/query/submit calls are logged once' "$out/d3d12-diag.log"
grep -q 'PRUN D3D12 CheckFeatureSupport feature=7(SHADER_MODEL) size=4 highest=0x' "$out/d3d12-diag.log"
grep -q 'PRUN D3D12 CreateCommittedResource hr=0x[0-9a-f]* heap=default .*fmt=0(UNKNOWN) 64x64 ' "$out/d3d12-diag.log"
[[ $(grep -c 'PRUN D3D12 CreateCommittedResource hr=' "$out/d3d12-diag.log") == 1 ]]
# A guest's static d3d10.dll import is served by Play's own D3D10 shim from the
# runtime directory (the console has no d3d10.dll), and the shim's own
# d3d11.dll!D3D11CreateDevice import reaches Play's device hook. The fixture
# exits 42 only when the device, a swap chain on a guest HWND and three presents
# all succeeded; the CoreWindow adapter answered the chain.
grep -q 'PRUN EVENT mapping d3d10.dll' "$out/d3d10-diag.log"
grep -q 'PRUN EVENT guest D3D11CreateDevice caller=' "$out/d3d10-diag.log"
grep -q "guest device's DXGI factory: guest-window swapchains go through the CoreWindow adapter" "$out/d3d10-diag.log"
grep -q 'guest DXGI CoreWindow swapchain returned hr=0x00000000' "$out/d3d10-diag.log"
[[ $(grep -c 'guest D3D11CreateDevice returned hr=0x00000000' "$out/d3d10-diag.log") == 1 ]]
# A guest's static d3d9.dll import is served by Play's own D3D9 shim from the
# runtime directory (the console has no d3d9.dll), and the shim's own
# d3d11.dll!D3D11CreateDevice import reaches Play's device hook. The fixture
# exits 42 only when the factory, the device, a swap chain on a guest HWND, the
# auto depth-stencil surface, the clear and three presents all succeeded; the
# CoreWindow adapter answered the chain.
grep -q 'PRUN EVENT mapping d3d9.dll' "$out/d3d9-diag.log"
grep -q 'PRUN EVENT guest D3D11CreateDevice caller=.*d3d9.dll' "$out/d3d9-diag.log"
grep -q "guest device's DXGI factory: guest-window swapchains go through the CoreWindow adapter" "$out/d3d9-diag.log"
grep -q 'guest DXGI CoreWindow swapchain returned hr=0x00000000' "$out/d3d9-diag.log"
[[ $(grep -c 'guest D3D11CreateDevice returned hr=0x00000000' "$out/d3d9-diag.log") == 1 ]]
# The shim draws. Both fixtures read the pixels back through a SYSTEMMEM
# surface and exit 42 only when the triangle is where it should be and the
# clear colour is everywhere else, so a draw that silently did nothing fails.
# The textured one also proves the translator: its vs_1_1/ps_1_1 are a game's
# own bytecode, and the shim loads d3d9translator.dll from the runtime
# directory to turn them into DXBC.
grep -q 'PRUN EVENT mapping d3d9.dll' "$out/d3d9-triangle.log"
grep -q 'PRUN EVENT mapping d3d9translator.dll' "$out/d3d9-triangle.log"
grep -q 'PLAY-TEST .*code=42' "$out/d3d9-triangle.log"
grep -q 'PLAY-TEST .*code=42' "$out/d3d9-textured.log"
if grep -q 'the shader translator refused the bytecode' "$out/d3d9-textured.log";then exit 1;fi
# Guest RWX (JIT) memory is emulated over W^X: pages flip RW<->RX on fault.
grep -q 'RWX emulation: guest requested RWX' "$out/rwx-jit.log"
# Guest registry (PlayerPrefs) persists in the uwph SQLite registry across runs.
rm -f "$out"/registry.sqlite*
for mode in registry-write registry-read;do
 WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$out/play-runner.exe" "$mode" > "$out/$mode.log" 2>&1
 tail -1 "$out/$mode.log"
done
test -s "$out/registry.sqlite"
# Megabonk/Unity load-config shape (GuardFlags CF_INSTRUMENTED + volatile
# metadata) through production Play, reusing the unchanged native C++ test.
# A GuardFlags value outside the accepted contract must still be rejected.
cxx=$out/play-cxx.dll
cp "$cxx" "$out/obj/play-cxx-cookie.dll"
for flags in 0x100 0x500;do
 rm -rf "$out/obj/cxx-cf"
 UWP_OBJ_DIR="$out/obj/cxx-cf" "$CB/scripts/build.sh" --out "$cxx" --link-arg /dll tests/play-cxx.cpp tests/play-cxx-instrumented-config.c -- /MT /O2 /EHsc -DPLAY_CXX_GUARD_FLAGS=$flags
 lld-link-19 "$out"/obj/cxx-cf/*.obj /dll /out:"$cxx" \
  /libpath:"$crt/crt/lib/x86_64" /libpath:"$crt/sdk/lib/um/x86_64" \
  /libpath:"$crt/sdk/lib/ucrt/x86_64" kernel32.lib
 config=$(llvm-readobj --coff-load-config "$cxx")
 grep -q "GuardFlags \[ ($flags)" <<<"$config"
 grep -Eq 'VolatileMetadataPointer: 0x[0-9A-F]*[1-9A-F]' <<<"$config"
 log=$out/native-cxx-instrumented-$flags.log
 status=0
 WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$out/play-runner.exe" native-cxx > "$log" 2>&1 || status=$?
 tail -1 "$log"
 if [[ $flags == 0x100 ]];then
  [[ $status == 0 ]] && grep -q 'WXL Play MSVC load config GuardFlags=0x100 volatileMetadata=present' "$log"
 else
  [[ $status != 0 ]] && grep -q 'WXL FAIL explicit MSVC file preflight' "$log"
 fi
done
cp "$out/obj/play-cxx-cookie.dll" "$cxx"
# Owned stand-in under a game-DLL name, present only for this run.
cp "$out/play-unresolved.dll" "$out/baselib.dll"
WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$out/play-runner.exe" failed-dependency > "$out/failed-dependency.log" 2>&1
tail -1 "$out/failed-dependency.log"
grep -q "PLAY-RELEASE released=1 restarted=1 ran=1 retained=1" "$out/failed-dependency.log"
for mode in crt-exit crt-abort;do
 WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$out/play-runner.exe" "$mode" > "$out/$mode.log" 2>&1
 tail -1 "$out/$mode.log"
done
WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$out/play-runner.exe" top-level > "$out/top-level.log" 2>&1
grep -q "PLAY-TOP-LEVEL survived=1 exit=1" "$out/top-level.log"
rm -f "$out/baselib.dll"
# Steam without a genuine client. A steamclient64.dll in the game root must be
# refused (steam-client runs above with it present); a root steam_api64.dll is
# an ordinary game DLL. Owned stand-ins only, removed afterwards.
rm -f "$out/steamclient64.dll" "$out/steam_api64.dll"
cp "$out/play-dependency.dll" "$out/steam_api64.dll"
WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$out/play-runner.exe" root-steam > "$out/root-steam.log" 2>&1
tail -1 "$out/root-steam.log"
grep -q "steam_api64.dll is the game's original file, mapped unmodified" "$out/root-steam.log"
# A configured Steam API provider serves steam_api64.dll for the owned game. The
# root stand-in rejects attach, so only a real substitution reaches exit 42.
cp "$out/play-reject.dll" "$out/steam_api64.dll"
mkdir -p "$out/provider"; cp "$out/play-dependency.dll" "$out/provider/steam_api64.dll"
PRUN_STEAM_PROVIDER=$(winepath -w "$out/provider/steam_api64.dll") WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d \
 timeout -k 2 20 wine "$out/play-runner.exe" root-steam-provider > "$out/root-steam-provider.log" 2>&1
tail -1 "$out/root-steam-provider.log"
grep -q "steam_api64.dll served by the configured Steam API provider" "$out/root-steam-provider.log"
rm -rf "$out/steam_api64.dll" "$out/provider"

