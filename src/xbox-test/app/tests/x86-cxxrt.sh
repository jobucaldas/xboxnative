#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# x86 stage 3: the C++ runtime fixture. Builds an ordinary i686 C++ program
# (iostream/std::string/std::vector/exceptions/std::thread) with the mingw
# toolchain and runs it through the interpreter: the real C++ runtime's CRT
# startup, TLS-callback global initialization (mcfgthread), exception
# unwinding tables and a std::thread joined through the keyed-event surface.
# With --wine the same binary runs under real Windows semantics and stdout and
# the exit code must be identical. Deterministic and console-free.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cxx32="${I686_CXX:-$(command -v i686-w64-mingw32-g++ || true)}"
if [[ -z $cxx32 ]]; then
  # NixOS: the i686 mingw g++ wrapper lives in the store but is not on PATH.
  cxx32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-g++ 2>/dev/null | head -n1 || true)
fi
[[ -n $cxx32 ]] || { echo "x86-cxxrt: no i686-w64-mingw32-g++ (set I686_CXX)" >&2; exit 1; }
# NixOS's wrapper needs its mcfgthread include/lib from separate store paths.
inc=(); extra=()
for d in /nix/store/*-mcfgthread-i686-w64-mingw32-*-dev/include; do [[ -d $d ]] && inc+=("-I$d"); done
for d in /nix/store/*-mcfgthread-i686-w64-mingw32-*/lib; do [[ -d $d ]] && extra+=("-L$d"); done
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-cxxrt.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
# -static: the whole C++ runtime (libstdc++, libgcc, mcfgthread) links into the
# exe, so the fixture exercises the interpreter against one self-contained PE
# and CI needs no runtime DLLs beside it.
"$cxx32" -O1 -Wall -Wextra -Werror -static "${inc[@]}" "${extra[@]}" \
  -o "$tmp/fixture-cxx.exe" "$here/x86-cxxrt/fixture-cxx.cpp"
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-cxxrt.cpp" -o "$tmp/x86-cxxrt"
set +e
out=$("$tmp/x86-cxxrt" "$tmp/fixture-cxx.exe"); rc=$?
set -e
[[ $rc -eq 0 ]] || { echo "X86-CXXRT FAIL: driver exit $rc"; echo "$out"; exit 1; }
grep -q '^play: cxxrt ran to exit 42, all imports bound$' <<<"$out" || {
  echo "X86-CXXRT FAIL: no clean run"; echo "$out"; exit 1; }
! grep -q '^play: unresolved\|^play: fault' <<<"$out" || { echo "X86-CXXRT FAIL: unresolved import or fault"; echo "$out"; exit 1; }
echo "X86-CXXRT PASS: i686 C++ program (iostream, std::string, exceptions, std::thread) ran in the interpreter to exit 42"
if [[ ${1:-} == --wine ]]; then
  wine="${WINE:-wine}"
  set +e; wout=$(WINEDEBUG=-all "$wine" "$tmp/fixture-cxx.exe" 2>/dev/null | tr -d '\r'); wrc=$?; set -e
  want='hello from i686 C++ len=19
sum ok
acc=55'
  [[ $wrc -eq 42 ]] || { echo "X86-CXXRT FAIL: wine exit $wrc (want 42)"; echo "$wout"; exit 1; }
  [[ $wout == "$want" ]] || { echo "X86-CXXRT FAIL: wine stdout differs"; echo "$wout"; exit 1; }
  echo "X86-CXXRT PASS: same binary, stdout and exit code identical under Wine (real C++ runtime)"
fi
