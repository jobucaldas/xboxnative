#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Requires freshly source-built donors (out/layout/donors), not a console:
# d3dcompiler_47 must probe, serve a guest's static imports and compile+reflect.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)   # repo root: defaults below must not depend on the cwd
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
export WINEPREFIX=${DONOR_WINEPREFIX:-${WINEPREFIX:-/tmp/xboxnative-donor-wine}}
mkdir -p "$WINEPREFIX"
if [[ ! -f "$WINEPREFIX/system.reg" ]];then timeout -k 2 60 wineboot -u;fi
out=out/d3dcompiler-tests
mkdir -p "$out/obj"
CB=${UWP_CROSSBUILD:-$root/out/uwp-crossbuild}
sdk=${UWP_XWIN_ROOT:-$HOME/.cache/uwp-crossbuild/xwin}
"${CLANG_CL:-clang-cl}" -target x86_64-pc-windows-msvc -std:c++20 /c /GS- /GR- /O2 /DNOMINMAX \
 /imsvc "$sdk/crt/include" /imsvc "$sdk/sdk/include/ucrt" \
 /imsvc "$sdk/sdk/include/um" /imsvc "$sdk/sdk/include/shared" \
 /imsvc "$sdk/sdk/include/winrt" /imsvc "$sdk/sdk/include/cppwinrt" \
 /D_CRT_USE_BUILTIN_OFFSETOF \
 /I../uwph/host/third_party/sqlite /Iout/obj \
 tests/d3dcompiler-guest.cpp /Fo"$out/obj/guest.obj"
# No donor import library: the fixture resolves donor exports through
# donor_pe::exported after the probe, exactly as a guest's static import
# binding would. The static CRT is linked because the probe core (std::map,
# mutex, snprintf) needs it; mainCRTStartup runs its initialisation.
"${LLD_LINK:-lld-link}" /entry:mainCRTStartup \
 /subsystem:console,6.02 /appcontainer /dynamicbase /nxcompat \
 /out:"$out/guest.exe" "$out/obj/guest.obj" \
 /libpath:"$sdk/crt/lib/x86_64" /libpath:"$sdk/sdk/lib/ucrt/x86_64" \
 /libpath:"$sdk/sdk/lib/um/x86_64" libcmt.lib libvcruntime.lib libucrt.lib kernel32.lib uuid.lib
bash ../uwph/host/build-sqlite-lib.sh "$out/obj/sqlite3.lib"
UWP_OBJ_DIR="$out/obj/runner" "$CB/scripts/build.sh" --uwp --link-arg "$out/obj/sqlite3.lib" --out "$out/runner.exe" tests/donor-runtime.cpp -- /MT /O2 /Iout/obj /I../uwph/host/third_party/sqlite
donors="$root/out/wine-donors"
mkdir -p "$out/donors"
cp "$donors"/graphics/*.dll "$donors"/graphics/SHA256SUMS "$out/donors/" 2>/dev/null || { echo "donors missing: run tools/build-wine-donors.sh first" >&2; exit 2; }
# The probe maps every donor beside the executable: copy the rest from the
# same source build so d3dcompiler_47's gate runs with a complete set.
cp "$donors"/{avrt,dinput8,hid,setupapi,shcore,version}.dll "$out/donors/"
WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 120 wine "$out/guest.exe" | tr -d '\r' | tee "$out/run.log"
grep -Fx 'D3DCOMPILER DONOR PASS: probe, static imports, compile and reflect; no game code' "$out/run.log"
echo "d3dcompiler donor Wine host test: ok"
