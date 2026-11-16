#!/bin/bash
# Builds Play's DirectSound compatibility DLL on the console's XAudio2 engine.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?output directory required}"
mkdir -p "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mingw_lib=$(dirname "$(x86_64-w64-mingw32-g++ -print-file-name=libkernel32.a)")
x86_64-w64-mingw32-g++ -c -O2 -std=c++17 -fno-builtin -fno-tree-loop-distribute-patterns \
    -fno-exceptions -fno-rtti -fno-stack-protector -Wall -Wextra -Wno-unused-parameter \
    "$here/dsound.cpp" -o "$work/dsound.obj"
link() { lld-link-19 "$@" 2>/dev/null || lld-link "$@"; }
link /dll /noentry /nodefaultlib /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat \
    /def:"$here/dsound.def" /out:"$out/dsound.dll" "$work/dsound.obj" /libpath:"$mingw_lib" libkernel32.a libuuid.a
rwx=$(llvm-readobj --sections "$out/dsound.dll" | awk '/Name: \./{n=$2;w=0;x=0} /IMAGE_SCN_MEM_WRITE/{w=1} /IMAGE_SCN_MEM_EXECUTE/{x=1} /^ *\]$/{if(w&&x)print n}')
[[ -z $rwx ]] || { echo "dsound.dll has writable code section(s): $rwx" >&2; exit 1; }
for name in DirectSoundCreate DirectSoundCreate8 DirectSoundEnumerateA DirectSoundEnumerateW GetDeviceID; do
    llvm-readobj --coff-exports "$out/dsound.dll" | grep -q "Name: ${name}$" || { echo "dsound.dll does not export $name" >&2; exit 1; }
done
for ordinal in 1 2 3 7 11; do
    llvm-readobj --coff-exports "$out/dsound.dll" | grep -q "Ordinal: $ordinal$" || { echo "dsound.dll does not export ordinal $ordinal" >&2; exit 1; }
done
