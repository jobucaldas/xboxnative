#!/bin/bash
# Run after app/build.sh (or play-runtime.sh): needs app/out/layout and app/out/obj.
# Mapped guest imports exercise the production loader.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)   # repo root: defaults below must not depend on the cwd
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
CB=${UWP_CROSSBUILD:-$root/out/uwp-crossbuild}
x86_64-w64-mingw32-g++ -c -O2 -fno-stack-protector runtime-guest.cpp -o "$work/guest.obj"
lld-link-19 /nodefaultlib /entry:input_entry /include:input_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$work/play-guest.exe" "$work/guest.obj" /usr/x86_64-w64-mingw32/lib/libxinput1_4.a /usr/x86_64-w64-mingw32/lib/libkernel32.a
# Plugin loaded from a subdirectory by path, as Mono loads Unity plugins.
x86_64-w64-mingw32-g++ -c -O2 -fno-stack-protector runtime-plugin.cpp -o "$work/plugin.obj"
mkdir -p "$work/Plugins/x86_64"
lld-link-19 /nodefaultlib /dll /noentry /include:plugin_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$work/Plugins/x86_64/play-xinput-plugin.dll" "$work/plugin.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
bash ../host/build-sqlite-lib.sh "$work/sqlite3.lib" >/dev/null
UWP_OBJ_DIR="$work/obj" "$CB/scripts/build.sh" --uwp --link-arg "$work/sqlite3.lib" --out "$work/runner.exe" runtime-runner.cpp -- /MT /O2 "/I../../app/out/obj" /I../host/third_party/sqlite
cp ../../app/out/layout/play-tls-*.dll ../../app/out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$work/runner.exe" | tee "$work/run.log"
# The console's own XInput is blind to pads on UWP: the static import must never load it.
if grep -q "MLP SYSTEM xinput" "$work/run.log"; then echo "FAIL: the console XInput was loaded"; exit 1; fi
grep -q "PLAY-TEST executed=1 finished=1 success=1 code=42" "$work/run.log"
echo "PASS xinput runtime: emulated module for LoadLibrary, extensionless name, static import and plugin"
