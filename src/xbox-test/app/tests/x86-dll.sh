#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# x86 stage 3: guest DLL loading. Builds three freestanding i686 PE32 images
# (exe -> dll-a.dll -> dll-b.dll) and runs the exe through the interpreter:
# recursive import binding between guest PEs, DllMain(DLL_PROCESS_ATTACH),
# LoadLibraryA/GetProcAddress (name and ordinal)/FreeLibrary/GetModuleHandle
# called from guest code. Deterministic and console-free.
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
# exe: imports dll_a_sum/dllb_attach_marker, calls the kernel32 DLL APIs
# (LoadLibrary/GetProcAddress/FreeLibrary/GetModuleHandleA/W/ExW).
"$cc32" $flags -Wl,--entry,_fixture_main -Wl,--subsystem,console -o "$tmp/fixture-dll.exe" \
  "$here/x86-dll/fixture-dll.c" -L"$tmp" -ldll-a -ldll-b -lkernel32
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-dll.cpp" -o "$tmp/x86-dll"
set +e
out=$("$tmp/x86-dll" "$tmp/fixture-dll.exe" "$tmp"); rc=$?
set -e
[[ $rc -eq 0 ]] || { echo "X86-DLL FAIL: driver exit $rc"; echo "$out"; exit 1; }
grep -q '^play: dll-chain loaded, all imports bound, exit 42$' <<<"$out" || {
  echo "X86-DLL FAIL: no clean chain run"; echo "$out"; exit 1; }
! grep -q '^play: unresolved\|^play: fault' <<<"$out" || { echo "X86-DLL FAIL: unresolved import or fault"; echo "$out"; exit 1; }
echo "X86-DLL PASS: guest PE32 DLL chain (exe -> dll-a -> dll-b), DllMain attach, LoadLibraryA/GetProcAddress/FreeLibrary/GetModuleHandleA/W/ExW from guest code"
