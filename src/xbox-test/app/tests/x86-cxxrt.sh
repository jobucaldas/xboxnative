#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# x86 stage 3: the C++ runtime fixture. Builds an ordinary i686 C++ program
# (iostream/std::string/std::vector/exceptions) with the mingw toolchain and
# runs it through the interpreter: the real C++ runtime's CRT startup,
# TLS-callback global initialization and exception unwinding tables.
# std::thread is out on purpose: libstdc++'s thread support imports symbols
# that differ per toolchain (_M_start_thread, a thread-proc trampoline), so it
# belongs to a later stage, not to the runtime a normal game links.
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
# and CI needs no runtime DLLs beside it. Some toolchains (Debian's posix-
# threaded i686 mingw among them) lack the static libs for a full -static
# link; the fallback is a normal link with the runtime DLLs copied beside the
# exe — which the stage-3 loader loads as guest DLLs anyway, so the run is
# the stronger test (a real multi-DLL process).
if ! "$cxx32" -O1 -Wall -Wextra -Werror -static "${inc[@]}" "${extra[@]}" \
    -o "$tmp/fixture-cxx.exe" "$here/x86-cxxrt/fixture-cxx.cpp" 2>"$tmp/static-err.log"; then
  echo "x86-cxxrt: -static link failed ($(tail -n1 "$tmp/static-err.log")); falling back to a DLL-linked runtime"
  "$cxx32" -O1 -Wall -Wextra -Werror "${inc[@]}" "${extra[@]}" \
    -o "$tmp/fixture-cxx.exe" "$here/x86-cxxrt/fixture-cxx.cpp"
  gxxdir=$(dirname "$cxx32")
  for dll in libstdc++-6.dll libgcc_s_dw2-1.dll libwinpthread-1.dll libmcfgthread-2.dll; do
    found=$("$cxx32" -print-file-name="$dll" 2>/dev/null || true)
    if [[ -z $found || ! -f $found ]]; then
      # print-file-name answers the bare name when it cannot find the file;
      # try the toolchain's own dirs and (NixOS) the mcfgthread store bin/.
      for d in "$gxxdir" "$gxxdir/../lib" /nix/store/*-mcfgthread-i686-w64-mingw32-*/bin; do
        [[ -f $d/$dll ]] && { found=$d/$dll; break; }
      done
    fi
    [[ -n $found && -f $found ]] && cp "$found" "$tmp/"
  done
  ls "$tmp"/*.dll >/dev/null 2>&1 || { echo "x86-cxxrt: fallback link produced no runtime DLLs to stage" >&2; exit 1; }
fi
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-cxxrt.cpp" -o "$tmp/x86-cxxrt"
set +e
out=$("$tmp/x86-cxxrt" "$tmp/fixture-cxx.exe" "$tmp"); rc=$?
set -e
[[ $rc -eq 0 ]] || { echo "X86-CXXRT FAIL: driver exit $rc"; echo "$out"; exit 1; }
grep -q '^play: cxxrt ran to exit 42, all imports bound$' <<<"$out" || {
  echo "X86-CXXRT FAIL: no clean run"; echo "$out"; exit 1; }
! grep -q '^play: unresolved\|^play: fault' <<<"$out" || { echo "X86-CXXRT FAIL: unresolved import or fault"; echo "$out"; exit 1; }
echo "X86-CXXRT PASS: i686 C++ program (iostream, std::string, exceptions) ran in the interpreter to exit 42"
if [[ ${1:-} == --wine ]]; then
  wine="${WINE:-wine}"
  if ! command -v "$wine" >/dev/null && ! [[ -x $wine ]]; then
    echo "X86-CXXRT SKIP: wine not found (set WINE); interpreter side already verified"
    exit 0
  fi
  # A 64-bit-only wine cannot start a PE32: it reports "application could not
  # be started" / bad exe format with a nonzero rc. That is an environment
  # limit, not a fixture failure — the interpreter side is the CI-relevant
  # check — so report it loudly and pass. Any OTHER nonzero rc is a real
  # failure: the binary ran and disagreed with the interpreter.
  set +e
  WINEDEBUG=-all "$wine" "$tmp/fixture-cxx.exe" >"$tmp/wine-out.txt" 2>"$tmp/wine-err.txt"
  wrc=$?
  set -e
  wout=$(tr -d '\r' <"$tmp/wine-out.txt")
  want='hello from i686 C++ len=19
sum ok
acc=55'
  if [[ $wrc -ne 42 ]]; then
    if grep -qi "bad exe format\|cannot execute\|application could not be started" "$tmp/wine-err.txt" \
       && [[ ! -s $tmp/wine-out.txt ]]; then
      echo "X86-CXXRT SKIP: wine could not run the PE32 (exit $wrc) — 64-bit-only wine needs wine32 for i386"
      tail -3 "$tmp/wine-err.txt"
      exit 0
    fi
    echo "X86-CXXRT FAIL: wine exited $wrc (expected 42)"; echo "$wout"; tail -3 "$tmp/wine-err.txt"; exit 1
  fi
  [[ $wout == "$want" ]] || { echo "X86-CXXRT FAIL: wine stdout differs"; echo "$wout"; exit 1; }
  echo "X86-CXXRT PASS: same binary, stdout and exit code identical under Wine (real C++ runtime)"
fi
