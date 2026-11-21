#!/usr/bin/env bash
# Source guards for launch.h fixes. launch.h is Windows-only and CI does not
# compile it (see include-scope-test.py), so these fixed patterns are checked
# against the source text. Usage: launch-bugbot-guards.sh [path/to/launch.h]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
src=${1:-$here/../launch.h}
fail=0
has() { grep -Fq -- "$2" "$src" || { echo "launch-bugbot-guards: missing: $1" >&2; fail=1; }; }
lacks() { grep -Fq -- "$2" "$src" && { echo "launch-bugbot-guards: still present: $1" >&2; fail=1; } || true; }
count() { local n; n=$(grep -Fc -- "$2" "$src" || true); [[ $n -eq $3 ]] || { echo "launch-bugbot-guards: $1: found $n, want $3" >&2; fail=1; }; }

lacks "exe name narrowed by low byte" 'std::string(exe.begin(),exe.end())'
has "exe name narrowed with narrow()" 'imports.report(play_runtime::narrow(exe),"x86")'
lacks "cwd failure uses dummy guest path" '__x86_guard_cwd_unavailable__'
has "cwd failure denies" 'if(!n||n>=32768)return false;'
lacks "window class proc returns 0" '[](HWND,UINT,WPARAM,LPARAM)->LRESULT{return 0;}'
has "window class proc defers to uwph DefWindowProcW" 'GetProcAddress(records,"DefWindowProcW")'
lacks "unmap calls host glUnmapBuffer" 'unmap=pointer("glUnmapBuffer")'
has "unmap returns GL_TRUE after copy-back" 'q.result=1;return; // GL_TRUE'
has "cached client requires completed stamp" 'return read_bytes(stamp).size()==64&&'
count "stamp cleared before partial store" 'DeleteFileW(stamp.c_str()); // rewritten only' 2
has "failed genuine probe clears SteamPath" 'if(!ok)SetEnvironmentVariableW(L"SteamPath",nullptr);'
count "PE32 route initialises registry" 'regb::init(g_dir,lp_sink); // guest registry (PlayerPrefs) for the PE32 route too' 1
count "managed route probes Wine donor" 'if(!donor_pe::probe(lp_sink))' 2
count "managed route wires donor resolver" 'mlp::donor_resolve=donor_pe::exported;' 2
count "managed route pins FNA3D to D3D11 via RAII" 'fna_driver::Scope fnaDriver;' 1
count "SEH wrapper releases the FNA3D pin after __except" 'fna_driver::release_all();' 1
has "managed route logs the effective FNA3D driver" 'fna3d=%s'
has "launch.h includes the FNA driver helper" '#include "host/fna_driver.h"'

if [[ $fail -ne 0 ]]; then exit 1; fi
echo "launch-bugbot-guards: PASS ($src)"
