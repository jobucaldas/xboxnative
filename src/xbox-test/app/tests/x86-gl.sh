#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Off-device PE32 GL thunk test; uses an API-checking host shim, not a game or console.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../" && pwd)
here="$root/src/xbox-test/app/tests"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/x86-gl.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cc32=${I686_CC:-i686-w64-mingw32-gcc}
command -v "$cc32" >/dev/null || { echo "x86-gl: no $cc32" >&2; exit 1; }
python3 -I "$root/src/tools/gen-x86-gl.py" --check
# Host GL proc lookup must clear the GetProcAddress error on success (launch.h gl_call path).
grep -Fq 'if(p) SetLastError(ERROR_SUCCESS);' "$root/src/xbox-test/app/launch.h"
# Host GL dispatch must marshal glGetString/glGetStringi into q.output, handle glMapBufferARB,
# and refuse unbridged pointer-returning functions on the generated catch-all path.
grep -Fq 'if(q.name=="glGetString"||q.name=="glGetStringi")' "$root/src/xbox-test/app/launch.h"
grep -Fq 'if(q.name=="glMapBufferRange"||q.name=="glMapBuffer"||q.name=="glMapBufferARB")' "$root/src/xbox-test/app/launch.h"
grep -Fq 'if(!p)p=pointer(q.name=="glBindBufferARB"?"glBindBuffer":"glBindBufferARB");' "$root/src/xbox-test/app/launch.h"
grep -Fq 'if(err_fn()!=0){q.result=0;return;}' "$root/src/xbox-test/app/launch.h"
grep -Fq 'q.return_kind!=x86::GlReturnKind::Pointer' "$root/src/xbox-test/app/launch.h"
# Indexed/VAO binds report host GL errors, and map/unmap verify the host's real binding.
grep -Fq 'if(err_fn()!=0){q.rejected=true;q.result=0;}' "$root/src/xbox-test/app/launch.h"
grep -Fq 'return bound!=0&&unsigned(bound)==expected;' "$root/src/xbox-test/app/launch.h"
"$cc32" -O1 -Wall -Wextra -Werror -Wno-cast-function-type -nostdlib -Wl,--entry,entry@0 -Wl,--subsystem,console \
  -o "$tmp/x86-gl-guest.exe" "$here/x86-gl-guest.c" -lkernel32 -luser32 -lgdi32 -lopengl32
"${CXX:-c++}" -std=c++17 -O2 -mno-red-zone -Wall -Wextra -Werror \
  "$here/x86-gl.cpp" -o "$tmp/x86-gl"
"$tmp/x86-gl" "$tmp/x86-gl-guest.exe"
