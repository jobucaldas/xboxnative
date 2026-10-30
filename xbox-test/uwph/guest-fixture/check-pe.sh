#!/usr/bin/env bash
# Build and inspect only; no guest execution. SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tc=${GUEST_TOOLCHAIN:-/data/OpenBot/Shared/toolchains/llvm-mingw-20260908-ucrt-ubuntu-22.04-x86_64/bin}
out=$(mktemp -d "${TMPDIR:-/tmp}/guest-pe.XXXXXXXX")
trap 'rm -rf -- "$out"' EXIT
cc="$tc/x86_64-w64-mingw32-clang"
objdump="$tc/x86_64-w64-mingw32-objdump"
"$tc/llvm-dlltool" -d "$here/marker.def" -l "$out/marker.lib" -D guest-marker.dll
"$cc" -O1 -fno-stack-protector -c "$here/guest-entry.c" -o "$out/guest.obj"
"$tc/lld-link" /nodefaultlib /entry:guest_entry /subsystem:windows,6.02 /machine:x64 /dynamicbase /nxcompat /out:"$out/guest.exe" "$out/guest.obj" "$out/marker.lib"
"$objdump" -p "$out/guest.exe" > "$out/headers.txt"
"$objdump" -f "$out/guest.exe" > "$out/format.txt"
grep -Eq '(pe-x86-64|pei-x86-64|coff-x86-64)' "$out/format.txt"
grep -Eq 'AddressOfEntryPoint[[:space:]]+0*[1-9a-fA-F]' "$out/headers.txt"
grep -qi 'guest-marker.dll' "$out/headers.txt"
grep -q 'entry_marker' "$out/headers.txt"
"$tc/llvm-readobj" --coff-basereloc "$out/guest.exe" > "$out/relocations.txt"
grep -q 'Type: DIR64' "$out/relocations.txt"
for directory in 'Load Configuration Directory' 'Delay Import Directory' 'CLR Runtime Header' 'Thread Storage Directory'; do
  line=$(grep -F "$directory" "$out/headers.txt")
  [[ "$line" =~ ^Entry[[:space:]]+[0-9a-fA-F]+[[:space:]]+0+[[:space:]]+0+ ]] || { echo "FAIL unsupported directory: $line" >&2; exit 1; }
done
printf 'compiler: '; "$cc" --version | head -1
printf 'linker: '; "$tc/lld-link" --version
printf 'sources: '; sha256sum "$here/guest-entry.c" "$here/marker.def"
echo 'PE inspection PASS: x64, entry, named marker import, DIR64 relocation, unsupported directories absent; guest not executed'
