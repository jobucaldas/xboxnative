#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out=${1:-out-tls}
mkdir -p "$out/obj" "$out/layout"
llvm-dlltool-19 -m i386:x86-64 -d tests/cxx-runtime.def -l "$out/obj/cxx-runtime.lib"
python3 - "$out" <<'PYCODE'
import subprocess,pathlib,sys,os
lib=os.path.expanduser('~/.cache/uwp-crossbuild/xwin/crt/lib/x86_64/libcmt.lib')
name=next(n for n in subprocess.check_output(['llvm-ar-19','t',lib],text=True).splitlines() if n.endswith('\\std_type_info_static.obj'))
pathlib.Path(sys.argv[1]+'/obj/typeinfo.obj').write_bytes(subprocess.check_output(['llvm-ar-19','p',lib,name]))
PYCODE
for kind in dll exe; do
  flags=(); [[ $kind == dll ]] && flags=(-DTLS_DLL)
  clang-cl-19 /c /O1 /GS- /EHs /Zl "${flags[@]}" /Fo"$out/obj/cxx-$kind.obj" tests/cxx-guest.cpp
  clang++-19 --target=x86_64-w64-windows-gnu -fno-emulated-tls -O1 -fno-stack-protector "${flags[@]}" -c tests/tls-guest.cpp -o "$out/obj/tls-$kind.obj"
done
common=("$out/obj/typeinfo.obj" "$out/obj/cxx-runtime.lib" /nodefaultlib /include:_tls_used /alternatename:tls_raw=tls_value /subsystem:windows,6.02 /appcontainer /dynamicbase /nxcompat)
lld-link-19 "${common[@]}" /dll /entry:tls_dll_entry /out:"$out/layout/tls-test.dll" /implib:"$out/obj/tls-test.lib" "$out/obj/tls-dll.obj" "$out/obj/cxx-dll.obj" /usr/x86_64-w64-mingw32/lib/libkernel32.a
lld-link-19 "${common[@]}" /entry:tls_exe_entry /out:"$out/layout/tls-test.exe" "$out/obj/tls-exe.obj" "$out/obj/cxx-exe.obj" "$out/obj/tls-test.lib" /usr/x86_64-w64-mingw32/lib/libkernel32.a
for slot in a b; do
  define=/DCARRIER_A; [[ $slot == b ]] && define=/DCARRIER_B
  UWP_OBJ_DIR="$out/carrier-$slot-obj" /workspace/ref/uwp-crossbuild/scripts/build.sh --uwp --out "$out/layout/tls-carrier-$slot.dll" --link-arg /dll tls_carrier.cpp -- /MT /O2 /GS- "$define"
done
