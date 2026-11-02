#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/out}
mkdir -p "$out"
cc=x86_64-w64-mingw32-gcc
common=(-O2 -static-libgcc -Wl,--subsystem,windows:6.2 -Wl,--dynamicbase -Wl,--nxcompat)
# No CRT startup in DLLs: only OS primitives and an explicit log callback.
"$cc" "${common[@]}" -shared -nostdlib -Wl,--entry,0 -o "$out/uwph-hid.dll" "$here/backend.c" -lkernel32
"$cc" "${common[@]}" -shared -nostdlib -Wl,--entry,0 -o "$out/hid-test.dll" "$here/test.c" -lkernel32
"$cc" -O2 -static-libgcc -DHID_STANDALONE -o "$out/hid-test.exe" "$here/test.c" -luser32

# GNU PE linker lacks --appcontainer; set the documented DLL-characteristics bit.
python3 - "$out/uwph-hid.dll" "$out/hid-test.dll" <<'PYCODE'
import struct,sys
for name in sys.argv[1:]:
    b=bytearray(open(name,'rb').read()); pe=struct.unpack_from('<I',b,0x3c)[0]
    pos=pe+24+70
    struct.pack_into('<H',b,pos,struct.unpack_from('<H',b,pos)[0]|0x1000)
    open(name,'wb').write(b)
PYCODE
