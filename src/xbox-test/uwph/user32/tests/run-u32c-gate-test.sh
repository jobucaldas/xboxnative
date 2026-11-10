#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs the mapped regression PE (u32c-test.exe) that gates every Play launch,
# off-device: build uwph-u32c.dll and the PE, map the PE through the real wxl
# loader, and call its entry.
#
# It runs twice, because the gate runs inside the launcher's own process where
# the UI thread publishes the connected pad slots every frame:
#   - with no pad published (Wine's default state), and
#   - with a pad published beforehand (--pads 1), the console state that used to
#     fail the gate at GetRawInputDeviceList with every launch blocked.
# The PE must pass both; a count that only holds with no pad connected is the
# regression this catches.
#
# Needs a MinGW cross compiler, lld-link and Wine; CI installs them.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"   # the user32 lane, not tests/
work="$(mktemp -d "${TMPDIR:-/tmp}/u32c-offdevice.XXXXXX")"
trap 'rm -rf "$work"' EXIT
CXX=${CXX:-x86_64-w64-mingw32-g++}
LINK=${LINK:-lld-link}
if ! command -v "$LINK" >/dev/null 2>&1; then LINK=lld-link-19; fi
WINE=${WINE_BIN:-wine}
for tool in "$CXX" "$LINK" "$WINE" x86_64-w64-mingw32-dlltool; do
  command -v "$tool" >/dev/null 2>&1 || { echo "FAIL missing tool: $tool" >&2; exit 1; }
done
mingw_lib=$(dirname "$("$CXX" -print-file-name=libkernel32.a)")
# Some MinGW builds (Nix's, for one) keep the thread library's headers and
# archives outside the compiler's default search path; Ubuntu's gcc-mingw-w64
# uses winpthreads and needs nothing. Probe with a header core.cpp includes, and
# when it fails point the compiler at MCFGTHREAD_PREFIX (a prefix holding
# include/mcfgthread/gthr.h and lib/libmcfgthread.a).
thread_flags=()
if ! echo '#include <memory>' | "$CXX" -x c++ -fsyntax-only - >/dev/null 2>&1; then
  prefix=${MCFGTHREAD_PREFIX:-}
  if [[ -z "$prefix" ]]; then
    echo "FAIL $CXX cannot compile <memory>; set MCFGTHREAD_PREFIX to the mcfgthread prefix" >&2
    exit 1
  fi
  thread_flags+=(-I"$prefix/include" -L"$prefix/lib")
fi

# The DLL exports the record API and the pad entry points the launcher and the
# PE call; the PE links against this import library.
printf 'LIBRARY uwph-u32c.dll\nEXPORTS\n uwph_atom_add\n uwph_atom_find\n uwph_atom_delete\n uwph_u32c_key_event\n uwph_u32c_input_set_fallback\n uwph_u32c_input_release_all\n uwph_u32c_input_key\n uwph_u32c_pads\n uwph_u32c_pads_test\n uwph_u32c_pads_test_end\n uwph_u32c_pads_current\n' > "$work/atoms.def"
x86_64-w64-mingw32-dlltool -d "$work/atoms.def" -l "$work/atoms.a"

# Static CRT: the runner has no Windows runtime beside these images.
"$CXX" -DUNICODE -D_UNICODE "${thread_flags[@]}" -shared -static -static-libgcc -static-libstdc++ \
  -O1 -fno-exceptions -fno-rtti -o "$work/uwph-u32c.dll" "$here/core.cpp" "$here/core.def"
"$CXX" -DUNICODE -D_UNICODE -c -Os -fno-stack-protector -fno-exceptions -fno-rtti \
  "$here/test.cpp" -o "$work/test.obj"
# Same image flags the launcher maps: no CRT, app container, unwind data on.
"$LINK" /nodefaultlib /entry:u32c_entry /subsystem:windows,6.02 /appcontainer \
  /dynamicbase /nxcompat /out:"$work/u32c-test.exe" "$work/test.obj" \
  /libpath:"$mingw_lib" libkernel32.a libuser32.a "$work/atoms.a"
python3 -I "$here/../d3d9/check-unwind-data.py" "$work/u32c-test.exe"
"$CXX" -DUNICODE -D_UNICODE "${thread_flags[@]}" -std=c++20 -O1 -static -static-libgcc -static-libstdc++ \
  -o "$work/u32c-offdevice.exe" "$here/offdevice.cpp" -lbcrypt

export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$work/wine-prefix}
run() {
  local label=$1; shift
  local log="$work/run-$label.log"
  if ! ( cd "$work" && "$WINE" ./u32c-offdevice.exe "$@" ) >"$log" 2>&1; then
    echo "FAIL u32c gate with $label" >&2; cat "$log" >&2; exit 1
  fi
  # Wine's console output is CRLF; the marker lines must be there and the entry
  # must return 42 (the PE's pass code, which the gate checks).
  tr -d '\r' <"$log" | grep -q '^U32C TEST PASS ' || { echo "FAIL u32c gate with $label: no pass marker" >&2; cat "$log" >&2; exit 1; }
  tr -d '\r' <"$log" | grep -q '^U32C OFFDEVICE entry=42$' || { echo "FAIL u32c gate with $label: entry != 42" >&2; cat "$log" >&2; exit 1; }
  echo "OK u32c gate with $label"
}
run "no-pad"
run "one-pad" --pads 1
run "two-pads" --pads 5

# Re-entrancy: the gate runs inside the launcher's own process, where a second
# call is normal (probe.h caches only a PASS, so a failed run is retried in the
# same process, and a self-test step can follow a PE32 fixture). The classes and
# counters the PE registers are process state; a second run must see none of the
# first run's leftovers. Run the entry twice in one process with a pad published
# before each call, and require both to return 42.
twice_log="$work/run-twice.log"
if ! ( cd "$work" && "$WINE" ./u32c-offdevice.exe --twice ) >"$twice_log" 2>&1; then
  echo "FAIL u32c gate twice in one process" >&2; cat "$twice_log" >&2; exit 1
fi
tr -d '\r' <"$twice_log" | grep -q '^U32C OFFDEVICE entry1=42$' || { echo "FAIL u32c gate twice: first entry != 42" >&2; cat "$twice_log" >&2; exit 1; }
tr -d '\r' <"$twice_log" | grep -q '^U32C OFFDEVICE entry2=42$' || { echo "FAIL u32c gate twice: second entry != 42" >&2; cat "$twice_log" >&2; exit 1; }
# Both runs must have reached the end of the PE, not just returned the code.
[[ $(tr -d '\r' <"$twice_log" | grep -c '^U32C TEST PASS ') -eq 2 ]] || { echo "FAIL u32c gate twice: expected two pass markers" >&2; cat "$twice_log" >&2; exit 1; }
if tr -d '\r' <"$twice_log" | grep -q '^U32C CHECK FAIL '; then
  echo "FAIL u32c gate twice: a check failed" >&2; cat "$twice_log" >&2; exit 1
fi
echo "OK u32c gate twice in one process with pads published"
