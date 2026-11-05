#!/bin/bash
# Mirror console LocalState\megabonk (old package) to a local dir with manifest+sha256. Needs DP_PASS in env.
# usage: pull-megabonk.sh [LOCALDIR] [REMOTE_SUBPATH]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
OUT="${1:-/workspace/megabonk-from-console}"; REM="${2:-\\LocalState\\megabonk}"
PKG="${PKG_OLD:-Joao.Boxedwine.DevMode_0.1.6.0_x64__8j5c92gwe8f3c}"
mkdir -p "$OUT"; : > "$OUT/.list"
walk() { # remote dir
  local d="$1" json
  json=$(dp_retry GET "/api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=$PKG&path=$(urlenc "$d")") || { echo "LISTFAIL $d" >&2; return 1; }
  echo "$json" | python3 -c '
import json,sys
for i in json.load(sys.stdin)["Items"]:
    print(("D" if i["Type"]&16 else "F")+"\t"+i["Name"]+"\t"+str(i.get("FileSize",0)))' | while IFS=$'\t' read -r t n s; do
    if [ "$t" = D ]; then walk "$d\\$n"; else echo "$d\\$n	$s" >> "$OUT/.list"; fi
  done
}
walk "$REM"
fail=0; n=0
while IFS=$'\t' read -r path size; do
  rel="${path#"$REM"\\}"; rel="${rel//\\//}"; dst="$OUT/$rel"; mkdir -p "$(dirname "$dst")"
  if [ -f "$dst" ] && [ "$(stat -c %s "$dst")" = "$size" ]; then n=$((n+1)); continue; fi
  dir="${path%\\*}"; name="${path##*\\}"
  ok=0; for a in 1 2 3 4; do
    dp_lock curl_dp "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$PKG&filename=$(urlenc "$name")&path=$(urlenc "$dir")" -o "$dst.part" -f && [ "$(stat -c %s "$dst.part")" = "$size" ] && { mv "$dst.part" "$dst"; ok=1; break; }
    sleep 2
  done
  [ $ok = 1 ] && n=$((n+1)) || { echo "FAIL $path" >&2; fail=$((fail+1)); rm -f "$dst.part"; }
done < "$OUT/.list"
( cd "$OUT" && find . -type f ! -name .list ! -name MANIFEST.sha256 -printf '%P\n' | sort | xargs -d '\n' sha256sum > MANIFEST.sha256 )
echo "files=$(wc -l < "$OUT/MANIFEST.sha256") listed=$(wc -l < "$OUT/.list") ok=$n fail=$fail bytes=$(du -sb "$OUT" | cut -f1)"
