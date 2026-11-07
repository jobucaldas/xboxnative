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
# A static import from a DLL whose own mapping fails must stay unresolved: the
# failed image is never published to resolve the importer (never executed).
# baselib.dll is a game-DLL (uwph-routed) name, the route Megabonk exercised.
printf 'LIBRARY baselib.dll\nEXPORTS\n dependency_check\n' > "$out/obj/unresolved-import.def"
llvm-dlltool-19 -m i386:x86-64 -d "$out/obj/unresolved-import.def" -l "$out/obj/unresolved-import.lib"
printf '__declspec(dllimport) int dependency_check(void);\nint failed_anchor; void *failed_relocation = &failed_anchor;\nint failed_dependency_entry(void) { return dependency_check() ? 42 : 43; }\n' | clang++-19 --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector -x c -c - -o "$out/obj/failed-dependency.obj"
lld-link-19 /nodefaultlib /entry:failed_dependency_entry /include:failed_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/play-failed-dependency.exe" "$out/obj/failed-dependency.obj" "$out/obj/unresolved-import.lib"
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
rm -rf "$out/runtime";mkdir -p "$out/runtime";cp out/layout/runtime/*.dll "$out/runtime/"
bash ../uwph/host/build-sqlite-lib.sh "$out/obj/sqlite3.lib"
UWP_OBJ_DIR="$out/obj/runner" "$CB/scripts/build.sh" --uwp --link-arg "$out/obj/sqlite3.lib" --out "$out/play-runner.exe" tests/play-runner.cpp -- /MT /O2 /Iout/obj /I../uwph/host/third_party/sqlite
# Use the exact packaged carriers from the launcher build.
cp out/layout/play-tls-*.dll out/layout/uwph-u32c.dll "$out/"
# Owned stand-in so steam-client proves refusal of a present root client DLL.
cp "$out/play-dependency.dll" "$out/steamclient64.dll"
for mode in observer-log reject-attach reject-mapping normal missing static-steam crash exit hang dialog wide-dialog wide-long dialog-milestone steam-api steam-client steam-forward loader-lock reject-export-name reject-export-ordinal reject-export-source-forward reject-export-target-name reject-export-target-ordinal reject-export-lazy startup-lock attach-publication pending-forward-name pending-forward-ordinal pending-reject-name pending-reject-ordinal native-cxx native-vcrt native-msvcp shell-execute loader-search loader-thread loader-query loader-lifetime resource-query thread-exit thread-exit-main rwx-jit;do
 WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$out/play-runner.exe" "$mode" > "$out/$mode.log" 2>&1
 tail -1 "$out/$mode.log"
done
# Every vcruntime import took the ucrtbase route; none was left unresolved.
grep -q 'VCRT ROUTE vcruntime140.dll!__CxxFrameHandler3 found=1' "$out/native-vcrt.log"
grep -q 'VCRT ROUTE vcruntime140_1.dll!__CxxFrameHandler4 found=1' "$out/native-vcrt.log"
if grep -q 'VCRT ROUTE .* found=0' "$out/native-vcrt.log";then exit 1;fi
grep -q 'PRUN EVENT mapping msvcp140.dll' "$out/native-msvcp.log"
grep -q 'PRUN OPTIONAL ShellExecuteExW refused' "$out/shell-execute.log"
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
WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$out/play-runner.exe" top-level > "$out/top-level.log" 2>&1
grep -q "PLAY-TOP-LEVEL survived=1" "$out/top-level.log"
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

