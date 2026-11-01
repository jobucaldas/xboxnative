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
 Loop=play-forward.Loop @13
 Absent=play-forward-target.DeliberatelyAbsentOptionalExport @14
DEF
printf 'int forwarder_anchor; void *forwarder_relocation = &forwarder_anchor;\n' | clang++-19 --target=x86_64-w64-windows-gnu -x c -c - -o "$out/obj/forward.obj"
lld-link-19 "$out/obj/forward.obj" /include:forwarder_anchor /include:forwarder_relocation /dll /noentry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /def:"$out/obj/forward.def" /out:"$out/play-forward.dll" /implib:"$out/obj/forward.lib"
for kind in dll exe;do
 flags=();[[ $kind == dll ]]&&flags=(-DPLAY_DLL)
 clang++-19 --target=x86_64-w64-windows-gnu -fno-emulated-tls -O1 -fno-stack-protector "${flags[@]}" -c tests/play-guest.cpp -o "$out/obj/$kind.obj"
done
clang++-19 --target=x86_64-w64-windows-gnu -fno-emulated-tls -O1 -fno-stack-protector -DPLAY_DLL -DPLAY_REJECT_ATTACH -c tests/play-guest.cpp -o "$out/obj/reject.obj"
common=(/nodefaultlib /include:_tls_used /alternatename:tls_raw=tls_value /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat)
x86_64-w64-mingw32-windres ../uwph/kernel32/resource_fixture.rc -O coff -o "$out/obj/resources.obj"
lld-link-19 "${common[@]}" /dll /entry:guest_dll_entry /out:"$out/play-dependency.dll" /implib:"$out/obj/dependency.lib" "$out/obj/dll.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
lld-link-19 "${common[@]}" /entry:guest_exe_entry /out:"$out/play-guest.exe" "$out/obj/exe.obj" "$out/obj/resources.obj" "$out/obj/dependency.lib" "$out/obj/forward.lib" "$out/obj/missing.lib" /usr/x86_64-w64-mingw32/lib/libuser32.a /usr/x86_64-w64-mingw32/lib/libkernel32.a
lld-link-19 "${common[@]}" /dll /entry:guest_dll_entry /out:"$out/play-reject.dll" "$out/obj/reject.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
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

CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
UWP_OBJ_DIR="$out/obj/runner" "$CB/scripts/build.sh" --uwp --out "$out/play-runner.exe" tests/play-runner.cpp -- /MT /O2
# Use the exact packaged carriers from the launcher build.
cp out/layout/play-tls-*.dll out/layout/uwph-u32c.dll "$out/"
for mode in loader-search loader-thread observer-log reject-attach normal missing crash exit hang dialog wide-dialog wide-long steam-api steam-client steam-forward loader-query loader-lifetime resource-query thread-exit thread-exit-main;do
 WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$out/play-runner.exe" "$mode" > "$out/$mode.log" 2>&1
 tail -1 "$out/$mode.log"
done
