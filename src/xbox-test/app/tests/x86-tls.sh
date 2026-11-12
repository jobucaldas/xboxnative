#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the implicit-TLS fixture, patches PE data-directory 9 to point at the
# fixture's own IMAGE_TLS_DIRECTORY32 (binutils ld emits .tls and defines
# __tls_start__/__tls_end__ but never fills the directory), then runs it
# through the x86 interpreter (x86-guest harness, whose run_entry does the
# loader's TLS setup) and cross-checks under Wine. Deterministic, console-free.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then
  # NixOS: the i686 mingw gcc wrapper lives in the store but is not on PATH.
  cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true)
fi
[[ -n $cc32 ]] || { echo "x86-tls: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
nm32="$(command -v i686-w64-mingw32-nm || true)"
if [[ -z $nm32 ]]; then
  nm32=$(ls "$cc32/../i686-w64-mingw32-nm" /nix/store/*-i686-w64-mingw32-binutils-*/bin/i686-w64-mingw32-nm 2>/dev/null | head -n1 || true)
fi
[[ -n $nm32 ]] || { echo "x86-tls: no i686-w64-mingw32-nm" >&2; exit 1; }
tmp=$(mktemp -d /tmp/x86-tls.XXXXXX); trap 'rm -rf "$tmp"' EXIT

"$cc32" -O1 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector \
  -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -mno-80387 -nostdlib \
  -Wl,--entry,_fixture_tls_start@0 -Wl,--subsystem,console -Wl,--dynamicbase \
  -Wl,--no-seh -Wl,--image-base,0x400000 \
  -o "$tmp/fixture-tls.exe" "$here/x86-tls/fixture-tls.c" -lkernel32 -lmsvcrt

# Patch data-directory 9: RVA = VA(_tls_dir) - ImageBase, size 24. The
# directory's fields are absolute VAs in .data, so the image's own relocation
# table fixes them up when the loader maps it away from the preferred base.
python3 - "$tmp/fixture-tls.exe" "$("$nm32" "$tmp/fixture-tls.exe" | awk '$3=="_tls_dir"{print "0x"$1}')" <<'EOF'
import struct, sys
path, sym = sys.argv[1], int(sys.argv[2], 0)
b = bytearray(open(path, 'rb').read())
pe = struct.unpack_from('<I', b, 0x3c)[0]
base = struct.unpack_from('<I', b, pe + 24 + 28)[0]
rva = sym - base
off = pe + 24 + 96 + 9 * 8
old = struct.unpack_from('<II', b, off)
assert old == (0, 0), f'directory 9 not empty: {old}'
struct.pack_into('<II', b, off, rva, 24)
open(path, 'wb').write(b)
EOF

# The interpreter: the guest harness runs the same run_entry the launcher uses.
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-guest.cpp" -o "$tmp/x86-guest"
# The guest exits with the fixture's exit code (42) and prints the fixture's
# stdout after its own two lines, so capture both around set -e.
set +e; raw=$("$tmp/x86-guest" "$tmp/fixture-tls.exe" 2>&1); rc=$?; set -e
out=$(echo "$raw" | grep -E '^(x86-tls|checks|failed)' | tr -d '\r')
want=$'x86-tls: start\nx86-tls: block ok\nx86-tls: callback ok\nchecks=11\nfailed=0'
[[ $rc -eq 42 ]] || { echo "X86-TLS FAIL: interpreter exit $rc (want 42)"; echo "$raw"; exit 1; }
[[ $out == "$want" ]] || { echo "X86-TLS FAIL: interpreter stdout differs"; echo "$raw"; diff <(echo "$want") <(echo "$out"); exit 1; }
echo "X86-TLS PASS: implicit TLS block, index, and callback verified in the interpreter (exit 42)"

if [[ ${1:-} == --wine ]]; then
  # Wine's loader does the whole implicit-TLS setup itself on the patched
  # binary: same stdout (its block is a fresh allocation, so the copy/alias
  # checks hold there too) and exit code. Wine is not on PATH everywhere; the
  # project's cross-build env provides it (CI's x86-core job has no Wine, so
  # this branch is off-device only). Runs BEFORE the negative control below,
  # which zeroes directory 9 in place.
  wine_bin="${WINE:-$(command -v wine || true)}"
  if [[ -z $wine_bin && -f /tmp/xboxnative-cookie-integration-20261104/env.sh ]]; then
    wine_bin=$(WINEPREFIX= WINEDEBUG=-all bash -c 'source /tmp/xboxnative-cookie-integration-20261104/env.sh command -v wine' 2>/dev/null || true)
  fi
  [[ -n $wine_bin ]] || { echo "X86-TLS: wine not found for cross-check (set WINE)" >&2; exit 1; }
  export WINEPREFIX="${WINEPREFIX:-/tmp/xbn-tls-wine}" WINEDEBUG=-all
  set +e; wout=$("$wine_bin" "$tmp/fixture-tls.exe" 2>/dev/null | tr -d '\r'); wrc=$?; set -e
  [[ $wrc -eq 42 ]] || { echo "X86-TLS FAIL: wine exit $wrc (want 42)"; echo "$wout"; exit 1; }
  [[ $wout == "$want" ]] || { echo "X86-TLS FAIL: wine stdout differs"; echo "$wout"; exit 1; }
  echo "X86-TLS PASS: fixture stdout and exit code identical under Wine (real loader TLS setup)"
fi

# Negative control: the same binary with directory 9 zeroed must NOT pass --
# without the loader's TLS setup the fixture reads TEB+0x2C -> 0 and faults,
# proving the fixture actually exercises the TLS path (not a lucky no-op).
# This mutates the exe in place, so it runs last.
python3 - "$tmp/fixture-tls.exe" <<'EOF'
import struct, sys
path = sys.argv[1]
b = bytearray(open(path, 'rb').read())
pe = struct.unpack_from('<I', b, 0x3c)[0]
struct.pack_into('<II', b, pe + 24 + 96 + 9 * 8, 0, 0)
open(path, 'wb').write(b)
EOF
if "$tmp/x86-guest" "$tmp/fixture-tls.exe" >/dev/null 2>&1; then
  echo "X86-TLS FAIL: unpatched binary passed; fixture does not exercise the TLS path"; exit 1
fi
echo "X86-TLS PASS: negative control faults without the TLS directory (fixture exercises the path)"
