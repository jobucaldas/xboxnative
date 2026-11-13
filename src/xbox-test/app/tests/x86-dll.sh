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
# dll-exp-after: a python-built minimal PE32 whose export function sits right
# after the export directory — OUTSIDE the directory's real Size (0x40) but
# INSIDE the 64 KiB window a guessed bound would use. The forwarder test must
# bound by IMAGE_DATA_DIRECTORY[0].Size exactly, or this DLL's real function
# RVA reads as a forwarder STRING (Bugbot 4560048276).
python3 - "$tmp/dll-exp-after.dll" <<'PYEOF'
import struct, sys

out = sys.argv[1]
IMG = 0x2000   # two pages: SizeOfImage
HDRS = 0x1000  # SizeOfHeaders
EXP = 0x1000   # export directory RVA (the section's own page)
FN = 0x1080    # the export's code RVA: OUTSIDE the directory Size, INSIDE 64K
STRS = 0x10a0  # strings + tables

b = bytearray(IMG)
b[0:2] = b'MZ'
NT = 0x40
OPT = NT + 24  # 0x58
struct.pack_into('<I', b, 0x3c, NT)  # e_lfanew
b[NT:NT + 4] = b'PE\x00\x00'
# COFF: machine i386, 1 section, no symbols; opt header size 0xe0
struct.pack_into('<HHIIIHH', b, NT + 4, 0x14c, 1, 0, 0, 0, 0xe0, 0x10e)
opt = bytearray(0xe0)
struct.pack_into('<H', opt, 0, 0x10b)        # PE32
struct.pack_into('<I', opt, 16, FN)          # AddressOfEntryPoint = the export
struct.pack_into('<I', opt, 28, 0x64000000)  # ImageBase (the interpreter's section arena)
struct.pack_into('<I', opt, 32, 4096)        # SectionAlignment
struct.pack_into('<I', opt, 36, 4096)        # FileAlignment
struct.pack_into('<H', opt, 40, 4)           # MajorOSVersion (mingw-like)
struct.pack_into('<I', opt, 56, IMG)         # SizeOfImage
struct.pack_into('<I', opt, 60, HDRS)        # SizeOfHeaders
struct.pack_into('<H', opt, 68, 2)           # SubSystem console
struct.pack_into('<I', opt, 92, 1)           # NumberOfRvaAndSizes
# data directory 0 (export): RVA 0x1000, Size 0x40 — the directory struct and
# its three tables ONLY; the function at 0x1080 is OUTSIDE these bounds.
struct.pack_into('<II', opt, 96, EXP, 0x40)
b[OPT:OPT + len(opt)] = opt
# one .flat section covering everything past the headers, R|W|X: the mapper's
# headers span (0..SizeOfHeaders) must not overlap it, and the section VA must
# be SectionAlignment-aligned.
SEC_VA = 0x1000
sec = struct.pack('<8sIIIIIIHHI', b'.flat\0\0\0', IMG - SEC_VA, SEC_VA, IMG - SEC_VA, SEC_VA, 0, 0, 0, 0, 0xE0000040)
b[OPT + 0xe0:OPT + 0xe0 + 40] = sec

# Export directory, 4-byte aligned as the Win32 loader reads it: Char u16 @0,
# pad u16 @2, TimeDateStamp u32 @4, Major u16 @8, Minor u16 @10, Name u32 @12,
# Base u32 @16, NumberOfFunctions u32 @20, NumberOfNames u32 @24, EAT u32 @28,
# Names u32 @32, Ords u32 @36 — 0x40 bytes, exactly the declared Size. The EAT
# is a table at STRS+0x18 (one u32 holding the function RVA FN), the
# name-pointer table at STRS+0x10 (one u32 -> the name string at STRS+0x20),
# the ordinal table at STRS+0x14 (one u16, index 0).
struct.pack_into('<HHIHHIIIIIII', b, EXP,
                 0, 0, 0, 0, 0, STRS, 1, 1, 1, STRS + 0x18, STRS + 0x10, STRS + 0x14)
# the export's code at 0x1080: mov eax,0x0DDBA11 ; ret
b[FN:FN + 6] = b'\xb8\x11\xba\xdd\x00\xc3'
b[STRS:STRS + 0x10] = b'exp-after.dll\x00\x00'          # dll name @0x10a0
struct.pack_into('<I', b, STRS + 0x10, STRS + 0x20)     # name ptr table -> 0x10c0
struct.pack_into('<H', b, STRS + 0x14, 0)               # ordinal table: idx 0
struct.pack_into('<I', b, STRS + 0x18, FN)              # EAT: function RVA 0x1080
b[STRS + 0x20:STRS + 0x30] = b'after_fn\x00\x00\x00\x00\x00\x00\x00\x00'  # @0x10c0

b += bytes(IMG - len(b))  # raw section covers the whole image: pad the file
open(out, 'wb').write(bytes(b))
PYEOF
# dll-leaf: mapped and attached only by a later LoadLibrary. dll-fail imports it
# first, then dll-corrupt, whose file is replaced below with non-PE bytes: its
# load throws after dll-leaf is mapped (the rollback case). The import libraries
# come from dlltool; the corrupt DLL is never built, only its import entry.
cat > "$tmp/dll-leaf.def" <<'EOF'
LIBRARY dll-leaf.dll
EXPORTS
leaf_marker
EOF
cat > "$tmp/dll-corrupt.def" <<'EOF'
LIBRARY dll-corrupt.dll
EXPORTS
corrupt_fn
EOF
"$dlltool" -m i386 -k -d "$tmp/dll-leaf.def" -l "$tmp/libdll-leaf.a"
"$dlltool" -m i386 -k -d "$tmp/dll-corrupt.def" -l "$tmp/libdll-corrupt.a"
"$cc32" $flags -shared -Wl,--entry,_DllMain@12 -Wl,--kill-at -o "$tmp/dll-leaf.dll" "$here/x86-dll/dll-leaf.c"
"$cc32" $flags -shared -Wl,--entry,_DllMain@12 -Wl,--kill-at -o "$tmp/dll-fail.dll" "$here/x86-dll/dll-fail.c" -L"$tmp" -ldll-leaf -ldll-corrupt
printf 'not a PE image' > "$tmp/dll-corrupt.dll"

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
echo "X86-DLL PASS: guest PE32 DLL chain (exe -> dll-a -> dll-b), the cycle dll-c <-> dll-d (each mapped once), DllMain attach, LoadLibraryA/GetProcAddress/FreeLibrary/GetModuleHandleA/W/ExW (flags, NULL name, FROM_ADDRESS) from guest code, forwarder bound = the export directory's Size (a real export right after it is code), Encode/DecodePointer round trip, entry-RVA-0 DLL with SizeOfImage >> file, second DLL relocated to a disjoint arena, bad-image rejection, per-thread ExitThread"
