#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Owned fixture only; offline Wine evidence, never a gameplay/console claim.
set -euo pipefail
cd "$(dirname "$0")/.."
out=out/msvc-cookie-tests
mkdir -p "$out/obj"
"${HOST_CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror ../shared/tests/msvc_load_config_test.cpp -o "$out/pure-test"
"$out/pure-test"
cc=${CONFIG_CXX:-x86_64-w64-mingw32-g++}
link=${CONFIG_LINKER:-lld-link}
dlltool=${CONFIG_DLLTOOL:-llvm-dlltool-19}
printf 'LIBRARY cookie-observer.dll\nEXPORTS\nobserve_cookie\n' > "$out/obj/observer.def"
"$dlltool" -m i386:x86-64 -d "$out/obj/observer.def" -l "$out/obj/observer.lib"
CB=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
UWP_OBJ_DIR="$out/obj/guest" "$CB/scripts/build.sh" --out "$out/guest.dll" --link-arg /dll \
 --link-arg /entry:fixture_entry --link-arg "$out/obj/observer.lib" tests/msvc-cookie-guest.cpp -- /MT /O2 /EHsc /GS
crt=${XWIN_ROOT:-$HOME/.cache/uwp-crossbuild/xwin}
# Desktop imports for a standalone owned mapper test, not a production fallback.
"$link" "$out"/obj/guest/tests_msvc-cookie-guest.*.obj "$out/obj/observer.lib" \
 /dll /entry:fixture_entry /out:"$out/guest.dll" /subsystem:windows,6.02 /dynamicbase /nxcompat \
 /libpath:"$crt/crt/lib/x86_64" /libpath:"$crt/sdk/lib/um/x86_64" \
 /libpath:"$crt/sdk/lib/ucrt/x86_64" kernel32.lib
UWP_OBJ_DIR="$out/obj/carrier" "$CB/scripts/build.sh" --uwp --out "$out/carrier.dll" --link-arg /dll \
 ../uwph/host/tls_carrier.cpp -- /MT /O2
"$cc" -std=c++17 -O2 -static tests/msvc-cookie.cpp -o "$out/mapper-test.exe" -lbcrypt
WINEDEBUG=-all timeout -k 2 60 wine "$out/mapper-test.exe" "$out/guest.dll" "$out" "$out/carrier.dll" | tr -d '\r' | tee "$out/result.log"
grep -Fx 'MSVC-COOKIE PASS: real app mapper, owned CRT/TLS/entry/complement/unwind, bounded RNG and negative mutations' "$out/result.log"
