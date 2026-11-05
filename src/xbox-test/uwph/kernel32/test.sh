#!/bin/bash
# Off-device native-thread behavior regression; no console access.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# MinGW declares this UWP API but omits its kernelbase import-library entry.
# Wine exports it from kernelbase; test the production FromApp adapter itself.
printf 'LIBRARY kernelbase.dll\nEXPORTS\nOpenFileMappingFromApp\n' > "$work/kernelbase.def"
x86_64-w64-mingw32-dlltool -d "$work/kernelbase.def" -l "$work/kernelbase.lib"
suites=(sync handles memory file nls_time process slist file_ops exception system thread_query console process_attributes winrt process_creation snapshot module_filename loader_ansi)
if (( $# )); then suites=("$@"); fi
for suite in "${suites[@]}"; do
    x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static "${suite}_test.cpp" "$work/kernelbase.lib" -lsynchronization -lruntimeobject -o "$work/$suite-test.exe"
    WINEDEBUG=-all timeout 45s wine "$work/$suite-test.exe"
done

if (( $# == 0 )); then ./test-unwind.sh; bash ./test-winrt-aux.sh; bash ./test-thread-policy.sh; fi
