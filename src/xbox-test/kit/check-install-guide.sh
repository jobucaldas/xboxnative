#!/bin/bash
# check-install-guide.sh [GUIDE=src/xbox-test/kit/INSTALL.md]
#
# Extracts every backticked token from the guide and checks it exists:
#   - repo paths (src/xbox-test/..., docs/..., tools/...): must exist in the repo;
#     the guide's own `bash <path>` commands: every path they name must exist
#   - kit artifact names (<name>.msix / <name>.cer / wxl-test.exe / manifest.json):
#     <name> must be produced by build-kit.sh (add_package / add_extra)
#   - LocalState log files (k32.log, w32u.log, vrx.log, boot.log): must appear in
#     app sources
#   - success markers (K32 RESULT ..., WXL RESULT ..., etc.): the text before any
#     '=' value must appear in app sources
# Also requires >= 5 numbered steps and no .pfx mention. Exits non-zero naming
# every missing item.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
cd "$root"
guide="${1:-src/xbox-test/kit/INSTALL.md}"
[ -f "$guide" ] || { echo "check-install-guide: $guide not found" >&2; exit 2; }

fail=0; checked=0
bad() { echo "MISSING: $1"; fail=1; }
good() { checked=$((checked + 1)); }

# sources searched for log names / markers (never the guide itself, never evidence logs)
src_has() { grep -rqF --include='*.c' --include='*.cpp' --include='*.h' --include='*.sh' --include='*.py' -e "$1" src/xbox-test; }
kit_names() { grep -hoE '(add_package|add_extra) +[A-Za-z0-9._-]+' src/xbox-test/kit/build-kit.sh | awk '{print $2}'; }
# Packages and guest extras must be explicitly named by the kit builder.

grep -o '`[^`]*`' "$guide" | sed 's/^`//; s/`$//' | sort -u > /tmp/guide-tokens.$$
trap 'rm -f /tmp/guide-tokens.$$' EXIT

check_word() {  # single space-free word taken from a command or a bare token
    local w="${1#\'}"; w="${w%\'}"
    case "$w" in
        src/xbox-test/*|docs/*|tools/*) [ -e "$w" ] && good || bad "path $w";;
        /tmp/*|'<'*|.*) ;;
        *.msix|*.cer)
            local n="${w##*/}"; n="${n%.*}"
            if kit_names | grep -qx "$n"; then good; else bad "kit package '$n' not produced by build-kit.sh ($w)"; fi;;
        wxl-test.exe|swc-test.exe|tls-test.exe|tls-test.dll|lifecycle.dll|manifest.json)
            grep -q "$w" src/xbox-test/kit/build-kit.sh src/xbox-test/kit/kitlib.py && good || bad "kit file $w";;
        *.log) src_has "$w" && good || bad "log file $w not written by any source";;
        *.sh) if [ -e "src/xbox-test/dp/$w" ] || [ -e "src/xbox-test/kit/$w" ]; then good; else bad "script $w"; fi;;
    esac
}

while IFS= read -r tok; do
    [ -n "$tok" ] || continue
    case "$tok" in
        *' RESULT '*|'K32 RESULT'*|'D3DT RESULT'*|'WXL RESULT'*|'W32U RESULT'*|'WXL FAIL open'|'vrx: all cases done')
            m="${tok%%=*}"; m="${m% failures}"; m="${m% presented}"; m="${m% entry}"
            src_has "$m" && good || bad "marker '$m' not found in sources";;
        caseC*) src_has "$tok" && good || bad "marker $tok not found in sources";;
        *' '*)  # command line: check every word
            for w in $tok; do check_word "$w"; done;;
        *) check_word "$tok";;
    esac
done < /tmp/guide-tokens.$$

steps="$(grep -c '^[0-9]\+\.' "$guide")"
if [ "$steps" -ge 5 ]; then good; else bad "guide has only $steps numbered steps (need >= 5)"; fi
if grep -qiE '\.pfx|BEGIN .*PRIVATE KEY' "$guide"; then bad "guide mentions .pfx / private key"; else good; fi

echo "=== check-install-guide: $checked checks, guide=$guide ==="
if [ "$fail" -ne 0 ]; then echo "RESULT: FAIL"; exit 1; fi
echo "RESULT: PASS"
