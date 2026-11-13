#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Compare real PE32 MinGW CRT header layouts in legacy msvcrt and UCRT modes.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cc32="${I686_CC:-$(command -v i686-w64-mingw32-gcc || true)}"
if [[ -z $cc32 ]]; then
  cc32=$(ls -d /nix/store/*-i686-w64-mingw32-gcc-wrapper-*/bin/i686-w64-mingw32-gcc 2>/dev/null | head -n1 || true)
fi
[[ -n $cc32 ]] || { echo "x86-apiset-layout: no i686-w64-mingw32-gcc (set I686_CC)" >&2; exit 1; }
objdump="${OBJDUMP:-$(dirname "$cc32")/i686-w64-mingw32-objdump}"
[[ -x $objdump ]] || { echo "x86-apiset-layout: no i686-w64-mingw32-objdump (set OBJDUMP)" >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-apiset-layout.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
common=(-x c -std=c11 -O1 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -mno-sse -mno-mmx -mno-80387 -c)
"$cc32" "${common[@]}" -D__MSVCRT_VERSION__=0x0700 -D_WIN32_WINNT=0x0600 -D_USE_32BIT_TIME_T -U_UCRT \
  -o "$tmp/msvcrt.o" "$here/x86-apiset-layout.cpp"
"$cc32" "${common[@]}" -D_UCRT -U__MSVCRT_VERSION__ \
  -o "$tmp/ucrt.o" "$here/x86-apiset-layout.cpp"
"$objdump" -s -j .rdata "$tmp/msvcrt.o" > "$tmp/msvcrt.txt"
"$objdump" -s -j .rdata "$tmp/ucrt.o" > "$tmp/ucrt.txt"
python3 -I - "$tmp/msvcrt.txt" "$tmp/ucrt.txt" <<'PY'
import re, struct, sys

stats = "sizeof st_ino st_size st_atime st_mtime st_ctime"
fields = {"lconv": "sizeof decimal_point int_frac_digits _W_decimal_point _W_negative_sign", "tm": "sizeof tm_sec tm_isdst",
          "_stat64": stats, "_stat64i32": stats, "_stat32": stats, "stat": stats}
for names, members in (("_finddata_t _finddatai64_t _finddata32_t _finddata32i64_t _finddata64i32_t __finddata64_t _wfinddata_t _wfinddatai64_t _wfinddata32_t _wfinddata32i64_t _wfinddata64i32_t _wfinddata64_t", "sizeof time_create size name"),
                       ("_timeb __timeb64", "sizeof time millitm timezone dstflag"), ("div_t ldiv_t lldiv_t", "sizeof quot rem")):
    fields.update(dict.fromkeys(names.split(), members))
fields.update({"_exception": "sizeof type name arg1 retval", "_complex": "sizeof x y",
               "FILE": "sizeof _ptr _cnt _base _flag _file _charbuf _bufsiz _tmpfname _Placeholder"})
keys = [f"{n}.{v}" for n, members in fields.items() for v in members.split()]
marker = struct.pack("<I", 0x58424c59)

def read(path):
    data = bytearray()
    for line in open(path, encoding="ascii"):
        words = line.strip().split("  ", 1)[0].split()[1:]
        data.extend(bytes.fromhex("".join(w for w in words if re.fullmatch(r"[0-9a-fA-F]{8}", w))))
    start, count = data.find(marker), len(keys) + 1
    if start < 0 or len(data) < start + count * 4:
        raise SystemExit(f"FAIL layout data table missing/truncated in {path}")
    values = struct.unpack_from(f"<{count}I", data, start + 4)
    return dict(zip(keys, (None if v == 0xffffffff else v for v in values)))

old, ucrt = map(read, sys.argv[1:])
keys = sorted(set(old) | set(ucrt))
names = {key.split(".", 1)[0] for key in keys if old.get(key) != ucrt.get(key)}
expected = {"FILE", "_finddata_t", "_finddatai64_t", "_timeb", "_wfinddata_t", "_wfinddatai64_t", "lconv", "stat"}
for name in sorted(names):
    print(f"layout mismatch: {name}\n" + "\n".join(f"  {key}: msvcrt={old.get(key, 'absent')} UCRT={ucrt.get(key, 'absent')}" for key in keys if key.startswith(name + ".") and old.get(key) != ucrt.get(key)))
if names != expected: raise SystemExit(f"FAIL unexpected layout mismatch set: expected {sorted(expected)}, got {sorted(names)}")
print(f"x86-apiset-layout: {len(names)} reviewed layout mismatches; all other measured layouts match")
PY
