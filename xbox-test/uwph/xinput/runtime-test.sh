#!/bin/bash
# Run after app/build.sh. Mapped guest imports exercise the production loader.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
x86_64-w64-mingw32-g++ -c -O2 -fno-stack-protector runtime-guest.cpp -o "$work/guest.obj"
lld-link-19 /nodefaultlib /entry:input_entry /include:input_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$work/play-guest.exe" "$work/guest.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
UWP_OBJ_DIR="$work/obj" "$CB/scripts/build.sh" --uwp --out "$work/runner.exe" runtime-runner.cpp -- /MT /O2
cp ../../app/out/layout/play-tls-*.dll ../../app/out/layout/uwph-u32c.dll "$work/"
WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 20 wine "$work/runner.exe"
