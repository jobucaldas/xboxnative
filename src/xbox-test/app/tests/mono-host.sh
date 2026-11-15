#!/usr/bin/env bash
# Offline Mono embedding smoke test; run with nix shell nixpkgs#mono nixpkgs#gcc.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
for tool in mcs mono g++; do command -v "$tool" >/dev/null || { echo "missing tool: $tool (use nix shell nixpkgs#mono nixpkgs#gcc)" >&2; exit 2; }; done
prefix=$(dirname "$(dirname "$(command -v mono)")")
[[ -f "$prefix/lib/libmonosgen-2.0.so" ]] || { echo "Mono embedding library missing under $prefix/lib" >&2; exit 2; }
# Console Mono must be mapped by Play; never fetch or package Microsoft's VC runtime.
! grep -Eiq 'vcruntime140|vc_redist|aka\.ms/vs|visual.?c\+\+.*redistributable' "$root/src/tools/fetch-mono.sh"
grep -q 'RUNTIME=(vcruntime140 ' "$root/src/tools/build-wine-donors.sh"
! grep -q 'LoadLibraryW(wide(library)' "$root/src/xbox-test/app/host/mono_host.h"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/mono-host-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
ln -s "$prefix/lib" "$tmp/lib"
if [[ -d "$prefix/etc/mono" ]]; then ln -s "$prefix/etc" "$tmp/etc"; else ln -s /etc "$tmp/etc"; fi
printf '%s\n' 'using System;' 'class Hello { static int Main() { Console.WriteLine("HELLO FROM EMBEDDED MONO"); return 23; } }' > "$tmp/Hello.cs"
mcs -nologo -platform:anycpu -out:"$tmp/Hello.exe" "$tmp/Hello.cs"
g++ -std=c++17 -Wall -Wextra -Werror "$root/src/xbox-test/app/tests/mono-host.cpp" -ldl -o "$tmp/mono-host"
"$tmp/mono-host" "$tmp" "$tmp/Hello.exe"
