#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Requires freshly source-built app/layout donors and record API, not a console.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
export WINEPREFIX=${DONOR_WINEPREFIX:-${WINEPREFIX:-/tmp/xboxnative-donor-wine}}
mkdir -p "$WINEPREFIX"
if [[ ! -f "$WINEPREFIX/system.reg" ]];then timeout -k 2 60 wineboot -u;fi
out=out/donor-tests
mkdir -p "$out/donors" "$out/obj"
CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
sdk=${UWP_XWIN_ROOT:-$HOME/.cache/uwp-crossbuild/xwin}
# Import libraries describe only this owned fixture's donor calls. They do not
# replace a donor DLL or alter the production router or game acceptance oracle.
for donor in hid setupapi version shcore avrt;do
 case "$donor" in
  hid) symbols=(HidD_GetHidGuid DeliberatelyAbsentDonorExport);;
  setupapi) symbols=(SetupDiCreateDeviceInfoList SetupDiDestroyDeviceInfoList);;
  version) symbols=(GetFileVersionInfoSizeW GetFileVersionInfoW VerQueryValueW);;
  shcore) symbols=(CommandLineToArgvW);;
  avrt) symbols=(AvSetMmThreadCharacteristicsW);;
 esac
 printf 'LIBRARY %s.dll\nEXPORTS\n' "$donor" > "$out/obj/$donor.def"
 printf ' %s\n' "${symbols[@]}" >> "$out/obj/$donor.def"
 "${LLVM_DLLTOOL:-llvm-dlltool}" -m i386:x86-64 -d "$out/obj/$donor.def" -l "$out/obj/$donor.lib"
done
for kind in guest missing;do
 flags=();[[ $kind == missing ]]&&flags=(/DDONOR_MISSING)
 "${CLANG_CL:-clang-cl}" -target x86_64-pc-windows-msvc /c /GS- /GR- /O2 \
  /imsvc "$sdk/crt/include" /imsvc "$sdk/sdk/include/ucrt" \
  /imsvc "$sdk/sdk/include/um" /imsvc "$sdk/sdk/include/shared" \
  "${flags[@]}" tests/donor-guest.cpp /Fo"$out/obj/$kind.obj"
 "${LLD_LINK:-lld-link}" /nodefaultlib /entry:donor_guest_entry \
  /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat \
  /include:donor_relocation /include:donor_relocation_anchor \
  /out:"$out/donor-$kind.exe" "$out/obj/$kind.obj" \
  "$out/obj/"{hid,setupapi,version,shcore,avrt}.lib \
  /libpath:"$sdk/sdk/lib/um/x86_64" kernel32.lib
 done
UWP_OBJ_DIR="$out/obj/runner" "$CB/scripts/build.sh" --uwp --out "$out/runner.exe" tests/donor-runtime.cpp -- /MT /O2 /Iout/obj
cp out/layout/donors/*.dll "$out/donors/"
cp out/layout/uwph-u32c.dll "$out/"
# Probe and guest each have a 30-second wait. Keep both gates intact and
# allow startup/binding overhead before the outer process deadline.
for mode in normal missing disabled;do
 WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d timeout -k 2 90 wine "$out/runner.exe" "$mode" | tr -d '\r' | tee "$out/$mode.log"
 grep -Fx 'DONOR-RUNTIME PASS: behavior gate, cross-thread publication, retained addresses; no game code' "$out/$mode.log"
 grep -Fx 'DONOR-DELAY PASS: shcore delay-loaded user32 DPI internals resolve to the record core' "$out/$mode.log"
 if [[ $mode == normal ]];then
  grep -Fx 'DONOR-GUEST PASS: actual Play runtime; 8 verified donor IAT slots; behavior exit=42; owned fixture only' "$out/$mode.log"
 else
  grep -Fx "DONOR-GUEST PASS mode=$mode: rejected before guest initialization; no placeholders" "$out/$mode.log"
 fi
done
