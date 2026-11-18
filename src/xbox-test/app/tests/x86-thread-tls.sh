#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PE32 integration test for static TLS blocks and thread notifications in both
# the executable and a preloaded guest DLL.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
[[ -n "$cc32" ]] || { echo "x86-thread-tls: no i686-w64-mingw32-gcc" >&2; exit 1; }
nm32="$(command -v i686-w64-mingw32-nm || true)"
[[ -n "$nm32" ]] || { echo "x86-thread-tls: no i686-w64-mingw32-nm" >&2; exit 1; }
tmp=$(mktemp -d /tmp/x86-thread-tls.XXXXXX); trap 'rm -rf "$tmp"' EXIT
flags=(-O1 -Wall -Wextra -Werror -Wno-attributes -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -mno-80387 -nostdlib)
"$cc32" "${flags[@]}" -shared -Wl,--entry,_DllMain@12 -Wl,--kill-at -Wl,--image-base,0x20000000 \
  -Wl,--out-implib,"$tmp/libtls-dll.a" -o "$tmp/tls-dll.dll" "$here/x86-tls/fixture-thread-tls-dll.c"
"$cc32" "${flags[@]}" -Wl,--entry,_fixture_thread_tls_start@0 -Wl,--subsystem,console -Wl,--dynamicbase -Wl,--image-base,0x400000 \
  -o "$tmp/fixture-thread-tls.exe" "$here/x86-tls/fixture-thread-tls.c" -L"$tmp" -ltls-dll -lkernel32 -lmsvcrt
patch_tls() {
  local image="$1" symbol="$2"
  python3 -I - "$image" "$($nm32 "$image" | awk -v sym="$symbol" '$3 == sym {print "0x"$1; exit}')" <<'PY'
import struct, sys
path, symbol = sys.argv[1], sys.argv[2]
sym = int(symbol, 0)
b = bytearray(open(path, 'rb').read())
pe = struct.unpack_from('<I', b, 0x3c)[0]
base = struct.unpack_from('<I', b, pe + 24 + 28)[0]
assert sym >= base
rva = sym - base
off = pe + 24 + 96 + 9 * 8
assert struct.unpack_from('<II', b, off) == (0, 0)
struct.pack_into('<II', b, off, rva, 24)
open(path, 'wb').write(b)
PY
}
patch_tls "$tmp/tls-dll.dll" _tls_dir
patch_tls "$tmp/fixture-thread-tls.exe" _tls_dir
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror "$here/x86-thread-tls.cpp" -o "$tmp/x86-thread-tls"
set +e
raw=$("$tmp/x86-thread-tls" "$tmp/fixture-thread-tls.exe" "$tmp" 2>&1)
rc=$?
set -e
printf '%s\n' "$raw"
[[ "$rc" -eq 0 ]] || { echo "X86-THREAD-TLS FAIL: driver exit $rc"; exit 1; }
grep -q '^x86-thread-tls: loaded=1 finished=1 exit=42$' <<<"$raw"
grep -q '^x86-thread-tls: per-thread exe+DLL TLS and notifications ok$' <<<"$raw"
echo "X86-THREAD-TLS PASS: executable and DLL static TLS templates, TLS callbacks, and DllMain thread attach/detach"
