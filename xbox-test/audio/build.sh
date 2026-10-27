#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?output directory required}"
mkdir -p "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -c -O2 -fno-exceptions -fno-rtti -fno-stack-protector "$here/tone.cpp" -o "$work/audio-tone.obj"
lld-link-19 /dll /noentry /nodefaultlib /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat /implib:"$work/audio-tone.lib" /out:"$out/audio-tone.dll" "$work/audio-tone.obj" /libpath:/usr/x86_64-w64-mingw32/lib libkernel32.a libole32.a
