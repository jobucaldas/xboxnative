#!/bin/bash
# Build only the record API DLL and a CRT-free mapped regression PE.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)   # repo root: defaults below must not depend on the cwd
export PATH="$HOME/.local/bin:$PATH"
here="$(cd "$(dirname "$0")" && pwd)"   # before cd: callers pass a relative path
cd "$here"
out=${1:-out}; mkdir -p "$out/layout" "$out/obj"
CB=${UWP_CROSSBUILD:-$root/out/uwp-crossbuild}
UWP_OBJ_DIR="$out/obj" "$CB/scripts/build.sh" --uwp --out "$out/layout/uwph-u32c.dll" --link-arg /dll --link-arg /def:core.def core.cpp -- /MT /O2 /DNDEBUG
printf "LIBRARY uwph-u32c.dll\nEXPORTS\n uwph_atom_add\n uwph_atom_find\n uwph_atom_delete\n uwph_u32c_key_event\n uwph_u32c_input_set_fallback\n uwph_u32c_input_release_all\n uwph_u32c_input_key\n uwph_u32c_pads\n uwph_u32c_pads_test\n uwph_u32c_pads_test_end\n uwph_u32c_pads_current\n" > "$out/obj/atoms.def"
x86_64-w64-mingw32-dlltool -d "$out/obj/atoms.def" -l "$out/obj/atoms.a"
# Unwind tables stay ON: Play maps this PE and calls its entry in the launcher's
# process, so a fault inside it must be unwindable or the launcher dies with no
# crash record (see src/xbox-test/uwph/d3d9/build.sh).
x86_64-w64-mingw32-g++ -DUNICODE -D_UNICODE -c -Os -fno-stack-protector -fno-exceptions -fno-rtti test.cpp -o "$out/obj/test.obj"
lld-link-19 /nodefaultlib /entry:u32c_entry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/layout/u32c-test.exe" "$out/obj/test.obj" /libpath:/usr/x86_64-w64-mingw32/lib libkernel32.a libuser32.a "$out/obj/atoms.a"
python3 -I "$here/../d3d9/check-unwind-data.py" "$out/layout/u32c-test.exe"
# Off-device only (desktop Win32, not UWP): maps uwph-u32c.dll through the real
# wxl loader with desktop VirtualAlloc/VirtualProtect, then runs u32c-test.exe's
# entry against it. Exercises this lane's logic before any console window.
UWP_OBJ_DIR="$out/obj" "$CB/scripts/build.sh" --out "$out/layout/u32c-offdevice.exe" offdevice.cpp -- /MT /O1 /EHsc
