#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# x86 stage 3: guest DLL loading. Builds six freestanding i686 PE32 images
# (exe -> dll-a.dll -> dll-b.dll, the cycle dll-c <-> dll-d, plus fwd.dll with
# pure OS/guest forwarder exports) and runs the exe through the interpreter:
# recursive import binding between guest PEs, DllMain(DLL_PROCESS_ATTACH) once
# per module (cycles included), LoadLibraryA/GetProcAddress (name and ordinal,
# forwarders through both)/FreeLibrary/GetModuleHandle, bad-image rejection
# with ERROR_BAD_EXE_FORMAT, and ExitThread ending only the calling thread.
# Deterministic and console-free.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then
  # NixOS: the i686 mingw gcc wrapper lives in the store but is not on PATH.
  cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true)
fi
[[ -n $cc32 ]] || { echo "x86-dll: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-dll.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
flags="-O1 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib"
# dll-b: leaf. dll-a: imports dllb_value from dll-b.dll (guest-to-guest import).
# --kill-at: undecorated export names (a .def-file build), so the fixture's
# GetProcAddress("dllb_value") matches what a game's import list looks like.
"$cc32" $flags -shared -Wl,--entry,_DllMain@12 -Wl,--kill-at -o "$tmp/dll-b.dll" "$here/x86-dll/dll-b.c"
"$cc32" $flags -shared -Wl,--entry,_DllMain@12 -Wl,--kill-at -o "$tmp/dll-a.dll" "$here/x86-dll/dll-a.c" -L"$tmp" -ldll-b
# dll-c <-> dll-d: a load cycle. Each imports the other, so the loader must
# map each exactly once and resolve the back edge against the in-progress
# mapping (Windows puts a module in the load table before walking imports).
# The import libraries come from dlltool (each link needs the other's
# exports before either DLL exists); the link order below breaks the cycle.
dlltool=$(command -v i686-w64-mingw32-dlltool || true)
if [[ -z $dlltool ]]; then
  # NixOS: the binutils wrapper lives in the store but is not on PATH.
  dlltool=$(ls -d /nix/store/*-i686-w64-mingw32-binutils-wrapper-*/bin/i686-w64-mingw32-dlltool 2>/dev/null | head -n1 || true)
fi
[[ -n $dlltool ]] || { echo "x86-dll: no i686-w64-mingw32-dlltool" >&2; exit 1; }
cat > "$tmp/dll-c.def" <<'EOF'
LIBRARY dll-c.dll
EXPORTS
    dllc_value@0
    dllc_attach_count@0
    dllc_sum@4
EOF
cat > "$tmp/dll-d.def" <<'EOF'
LIBRARY dll-d.dll
EXPORTS
    dlld_value@0
    dlld_attach_count@0
EOF
"$dlltool" -m i386 -k -d "$tmp/dll-c.def" -l "$tmp/libdll-c.a"
"$dlltool" -m i386 -k -d "$tmp/dll-d.def" -l "$tmp/libdll-d.a"
"$cc32" $flags -shared -Wl,--entry,_DllMain@12 -Wl,--kill-at -o "$tmp/dll-c.dll" "$here/x86-dll/dll-c.c" -L"$tmp" -ldll-d
"$cc32" $flags -shared -Wl,--entry,_DllMain@12 -Wl,--kill-at -o "$tmp/dll-d.dll" "$here/x86-dll/dll-d.c" -L"$tmp" -ldll-c
# fwd.dll: pure forwarder exports (msvcrt.memcpy, NTDLL.memcpy, kernel32.Sleep,
# the ordinal form "ntdll.#123", nosuchmod.nosuchsym, dll-b.dllb_value). The
# .def goes to ld directly and its entries are referenced by nothing, so ld
# emits real forwarder export entries (a referenced entry becomes an import
# stub instead).
"$cc32" $flags -shared -Wl,--entry,_DllMain@12 -o "$tmp/fwd.dll" "$here/x86-dll/dll-fwd.c" "$here/x86-dll/fwd.def"
# dll-zeroent: NO entry point (--entry 0 writes AddressOfEntryPoint = 0) and a
# ~4 MiB BSS over a few KiB of file — SizeOfImage >> file size. dll-second:
# the same preferred base (mingw's 0x10000000), so the loader must relocate it
# into the dedicated DLL arena without overlapping zeroent's huge reservation
# or the guest heap (Bugbot 4559749424/4559749442/4559749445).
"$cc32" $flags -shared -Wl,--entry,0 -Wl,--kill-at -o "$tmp/dll-zeroent.dll" "$here/x86-dll/dll-zeroent.c"
"$cc32" $flags -shared -Wl,--entry,_DllMain@12 -Wl,--kill-at -o "$tmp/dll-second.dll" "$here/x86-dll/dll-second.c"
# Import libraries for the two new DLLs (the fixture links their exports
# before the DLLs are seen by the loader; they are LoadLibrary'd at runtime).
cat > "$tmp/dll-zeroent.def" <<'EOF'
LIBRARY dll-zeroent.dll
EXPORTS
    zeroent_tail_get@0
    zeroent_tail_set@4
    zeroent_fn@0
EOF
cat > "$tmp/dll-second.def" <<'EOF'
LIBRARY dll-second.dll
EXPORTS
    second_fn@0
EOF
"$dlltool" -m i386 -k -d "$tmp/dll-zeroent.def" -l "$tmp/libzeroent.a"
"$dlltool" -m i386 -k -d "$tmp/dll-second.def" -l "$tmp/libsecond.a"
# Malformed images the fixture's LoadLibraryA must reject with
# ERROR_BAD_EXE_FORMAT (193): garbage bytes, and a real PE64 image (the
# interpreter is PE32-only, as a 32-bit process cannot map one).
printf 'not a PE image at all, just text' > "$tmp/badimg.dll"
# A minimal but structurally valid PE32+ (0x8664/0x20b) image: enough headers
# for the mapper to see the wrong machine type. python3 builds it deterministically.
python3 - "$tmp/pe64img.dll" <<'PYEOF'
import struct, sys
b = bytearray()
b += b'MZ' + b'\x00' * 0x3a + struct.pack('<I', 0x40)   # e_lfanew = 0x40
b += b'PE\x00\x00' + struct.pack('<HHIIIHH', 0x8664, 1, 0, 0, 0, 0, 0xf0)  # machine x64
opt = bytearray(0xf0); struct.pack_into('<H', opt, 0, 0x20b)              # PE32+
b += opt
b += b'\x00' * 0x100
open(sys.argv[1], 'wb').write(bytes(b))
PYEOF
# exe: imports dll_a_sum/dllb_attach_marker and the cyclic pair's exports,
# calls the kernel32 DLL APIs (LoadLibrary/GetProcAddress/FreeLibrary/
# GetModuleHandleA/W/ExW, Encode/DecodePointer), and exercises ExitThread's
# per-thread semantics.
"$cc32" $flags -Wl,--entry,_fixture_main -Wl,--subsystem,console -o "$tmp/fixture-dll.exe" \
  "$here/x86-dll/fixture-dll.c" -L"$tmp" -ldll-a -ldll-b -ldll-c -ldll-d -lzeroent -lsecond -lkernel32
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-dll.cpp" -o "$tmp/x86-dll"
set +e
out=$("$tmp/x86-dll" "$tmp/fixture-dll.exe" "$tmp"); rc=$?
set -e
[[ $rc -eq 0 ]] || { echo "X86-DLL FAIL: driver exit $rc"; echo "$out"; exit 1; }
grep -q '^play: dll-chain loaded, all imports bound, exit 42$' <<<"$out" || {
  echo "X86-DLL FAIL: no clean chain run"; echo "$out"; exit 1; }
! grep -q '^play: unresolved\|^play: fault' <<<"$out" || { echo "X86-DLL FAIL: unresolved import or fault"; echo "$out"; exit 1; }
echo "X86-DLL PASS: guest PE32 DLL chain (exe -> dll-a -> dll-b), the cycle dll-c <-> dll-d (each mapped once), DllMain attach, LoadLibraryA/GetProcAddress/FreeLibrary/GetModuleHandleA/W/ExW (flags, NULL name, FROM_ADDRESS) from guest code, Encode/DecodePointer round trip, entry-RVA-0 DLL with SizeOfImage >> file, second DLL relocated to a disjoint arena, bad-image rejection, per-thread ExitThread"
