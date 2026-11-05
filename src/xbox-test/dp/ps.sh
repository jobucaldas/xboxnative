#!/bin/bash
# ps.sh [grep-pattern] -- running processes (name, pid, working-set MB, cpu%, package)
# Field is WorkingSetSize (bytes) in the console's JSON, not "WorkingSet" -- verified live.
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/dp-lib.sh"
dp_lock curl_dp /api/resourcemanager/processes | python3 -c '
import json,sys,re
pat=sys.argv[1] if len(sys.argv)>1 else ""
for p in json.load(sys.stdin).get("Processes",[]):
    ws_mb=p.get("WorkingSetSize",0)/1048576.0
    s="%s pid=%s cpu=%.1f%% ws=%.1fMB pkg=%s"%(p.get("ImageName"),p.get("ProcessId"),p.get("CPUUsage",0),ws_mb,p.get("PackageFullName",""))
    if re.search(pat,s,re.I): print(s)' "$1"
