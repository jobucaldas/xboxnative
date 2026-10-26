#!/bin/bash
# Build only the record API DLL and a CRT-free mapped regression PE.
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
cd "$(dirname "$0")"
out=${1:-out}; mkdir -p "$out/layout" "$out/obj"
CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
UWP_OBJ_DIR="$out/obj" "$CB/scripts/build.sh" --uwp --out "$out/layout/uwph-u32c.dll" --link-arg /dll --link-arg /def:core.def core.cpp -- /MT /O2 /DNDEBUG
printf "LIBRARY uwph-u32c.dll\nEXPORTS\n uwph_atom_add\n uwph_atom_find\n uwph_atom_delete\n" > "$out/obj/atoms.def"
x86_64-w64-mingw32-dlltool -d "$out/obj/atoms.def" -l "$out/obj/atoms.a"
x86_64-w64-mingw32-g++ -DUNICODE -D_UNICODE -c -Os -fno-stack-protector -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -fno-unwind-tables test.cpp -o "$out/obj/test.obj"
lld-link-19 /nodefaultlib /entry:u32c_entry /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /out:"$out/layout/u32c-test.exe" "$out/obj/test.obj" /libpath:/usr/x86_64-w64-mingw32/lib libkernel32.a libuser32.a "$out/obj/atoms.a"
