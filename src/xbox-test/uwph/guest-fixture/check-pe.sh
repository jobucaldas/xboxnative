#!/usr/bin/env bash
# Build and inspect only; no guest execution. SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
if (( $# )); then echo "usage: $0" >&2; exit 2; fi
# An LLVM-MinGW bin directory is optional; ordinary MinGW GCC + LLVM on PATH
# can build the same no-CRT fixture, including on Linux/Nix development hosts.
if [[ -n ${GUEST_TOOLCHAIN:-} ]]; then export PATH="$GUEST_TOOLCHAIN:$PATH"; fi
cc=${GUEST_CC:-x86_64-w64-mingw32-clang}
if [[ -z ${GUEST_CC:-} ]] && ! command -v "$cc" >/dev/null 2>&1; then cc=x86_64-w64-mingw32-gcc; fi
objdump=${GUEST_OBJDUMP:-x86_64-w64-mingw32-objdump}
dlltool=${GUEST_DLLTOOL:-llvm-dlltool}
linker=${GUEST_LINKER:-lld-link}
readobj=${GUEST_READOBJ:-llvm-readobj}
for tool in "$cc" "$objdump" "$dlltool" "$linker" "$readobj"; do
  command -v "$tool" >/dev/null 2>&1 || { echo "FAIL missing tool: $tool" >&2; exit 1; }
done
out=$(mktemp -d "${TMPDIR:-/tmp}/guest-pe.XXXXXXXX")
trap 'rm -rf -- "$out"' EXIT
"$dlltool" -d "$here/marker.def" -l "$out/marker.lib" -D guest-marker.dll
"$cc" -O1 -fno-stack-protector -c "$here/guest-entry.c" -o "$out/guest.obj"
"$linker" /nodefaultlib /entry:guest_entry /subsystem:windows,6.02 /machine:x64 /dynamicbase /nxcompat /out:"$out/guest.exe" "$out/guest.obj" "$out/marker.lib"
"$objdump" -p "$out/guest.exe" > "$out/headers.txt"
"$objdump" -f "$out/guest.exe" > "$out/format.txt"
grep -Eq '(pe-x86-64|pei-x86-64|coff-x86-64)' "$out/format.txt"
grep -Eq 'AddressOfEntryPoint[[:space:]]+0*[1-9a-fA-F]' "$out/headers.txt"
grep -qi 'guest-marker.dll' "$out/headers.txt"
grep -q 'entry_marker' "$out/headers.txt"
"$readobj" --coff-basereloc "$out/guest.exe" > "$out/relocations.txt"
grep -q 'Type: DIR64' "$out/relocations.txt"
for directory in 'Load Configuration Directory' 'Delay Import Directory' 'CLR Runtime Header' 'Thread Storage Directory'; do
  line=$(grep -F "$directory" "$out/headers.txt")
  [[ "$line" =~ ^Entry[[:space:]]+[0-9a-fA-F]+[[:space:]]+0+[[:space:]]+0+ ]] || { echo "FAIL unsupported directory: $line" >&2; exit 1; }
done
printf 'compiler: '; "$cc" --version | head -1
printf 'linker: '; "$linker" --version
printf 'sources: '; sha256sum "$here/guest-entry.c" "$here/marker.def"
echo 'PE inspection PASS: x64, entry, named marker import, DIR64 relocation, unsupported directories absent; guest not executed'
