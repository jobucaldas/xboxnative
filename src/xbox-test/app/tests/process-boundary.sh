#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# OFFLINE owned-fixture evidence only. No console, Steam, or sandbox claim.
# Requires an already installed crossbuild/SDK, MinGW import libs and Wine.
set -euo pipefail
cd "$(dirname "$0")/.."
out=out/process-boundary-tests
logs=${PROCESS_TEST_LOG_DIR:-$out/logs}
export WINEPREFIX=${PRUN_WINEPREFIX:-/tmp/process-boundary-wine}
export WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d
mkdir -p "$out/obj" "$logs" "$WINEPREFIX"
cb=${UWP_CROSSBUILD:-/workspace/ref/uwp-crossbuild}
cc=${PROCESS_CC:-clang++-19}
link=${PROCESS_LINKER:-lld-link-19}
common=(/nodefaultlib /entry:process_entry /include:process_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat)
kernel=/usr/x86_64-w64-mingw32/lib/libkernel32.a
# All target exports below are references only, never called by any fixture.
cat > "$out/obj/forward.def" <<'DEF'
LIBRARY process-forward.dll
EXPORTS
 Kernel32A=kernel32.CreateProcessA @1
 Kernel32W=kernel32.CreateProcessW @2
 KernelbaseA=kernelbase.CreateProcessA @3
 KernelbaseW=kernelbase.CreateProcessW @4
 CoreA=api-ms-win-core-processthreads-l1-1-0.CreateProcessA @5
 CoreW=api-ms-win-core-processthreads-l1-1-0.CreateProcessW @6
 Core2A=api-ms-win-core-processthreads-l1-1-2.CreateProcessA @7
 Core2W=api-ms-win-core-processthreads-l1-1-2.CreateProcessW @8
DEF
printf 'int process_anchor; void* process_relocation=&process_anchor;\n' | "$cc" --target=x86_64-w64-windows-gnu -x c -c - -o "$out/obj/forward.obj"
"$link" /dll /noentry /include:process_relocation /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat \
    /def:"$out/obj/forward.def" /out:"$out/process-forward.dll" /implib:"$out/obj/forward.lib" "$out/obj/forward.obj"
compile() { "$cc" --target=x86_64-w64-windows-gnu -O1 -fno-stack-protector "$@" -c tests/process-guest.cpp; }
compile -o "$out/obj/dynamic.obj"
"$link" "${common[@]}" /out:"$out/process-dynamic.exe" "$out/obj/dynamic.obj" "$kernel"
modes=(resolver dynamic)
for alias in Kernel32 Kernelbase Core Core2;do
    case $alias in
        Kernel32) dll=KeRnEl32.DlL;;
        Kernelbase) dll=kernelbase.dll;;
        Core) dll=api-ms-win-core-processthreads-l1-1-0.dll;;
        Core2) dll=api-ms-win-core-processthreads-l1-1-2.dll;;
    esac
    for encoding in A W;do
        symbol=CreateProcess$encoding
        printf 'LIBRARY %s\nEXPORTS\n %s\n' "$dll" "$symbol" > "$out/obj/import.def"
        llvm-dlltool-19 -m i386:x86-64 -d "$out/obj/import.def" -l "$out/obj/import.lib"
        mode=static-$alias$encoding
        compile -DPROCESS_IMPORT="$symbol" -o "$out/obj/static.obj"
        "$link" "${common[@]}" /include:process_reference /include:_tls_used /out:"$out/$mode.exe" \
            "$out/obj/static.obj" "$out/obj/import.lib" "$kernel"
        modes+=("$mode")
        mode=forward-$alias$encoding
        compile -DPROCESS_FORWARD -DPROCESS_IMPORT="$alias$encoding" -o "$out/obj/static.obj"
        "$link" "${common[@]}" /include:process_reference /include:_tls_used /out:"$out/$mode.exe" \
            "$out/obj/static.obj" "$out/obj/forward.lib" "$kernel"
        modes+=("$mode")
    done
done
for adapter in native sentinel;do
    flags=();[[ $adapter == sentinel ]] && flags=(/DPROCESS_TEST_SENTINEL)
    UWP_OBJ_DIR="$out/obj/$adapter" "$cb/scripts/build.sh" --uwp \
        --out "$out/process-$adapter.exe" tests/process-boundary.cpp -- /MT /O2 "${flags[@]}"
    for mode in "${modes[@]}";do
        # Do not pass unrelated service credentials to owned guest execution.
        env -u DP_PASS -u TS_AUTHKEY timeout -k 2 30 wine "$out/process-$adapter.exe" "$mode" > "$logs/$adapter-$mode.log" 2>&1
        grep -F "PROCESS-BOUNDARY $mode PASS " "$logs/$adapter-$mode.log"
    done
done
printf 'PROCESS-BOUNDARY PASS: 36 offline cases; no process entrypoint called\n'
