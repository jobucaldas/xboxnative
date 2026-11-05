#!/bin/bash
# Off-device only. Local TCP server; no external network or console access.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
server_pid=
trap 'if [[ -n "$server_pid" ]]; then kill "$server_pid" 2>/dev/null || true; wait "$server_pid" 2>/dev/null || true; fi; rm -rf "$work"' EXIT
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static native-test.cpp -o "$work/native-test.exe"
python3 - "$work/port" <<'PY' &
import http.server, pathlib, sys
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        assert self.path == '/sysx', self.path
        self.send_response(200)
        self.send_header('Content-Length', '4')
        self.end_headers()
        self.wfile.write(b'sysx')
    def log_message(self, *args): pass
server = http.server.HTTPServer(('127.0.0.1', 0), Handler)
pathlib.Path(sys.argv[1]).write_text(str(server.server_port))
server.serve_forever()
PY
server_pid=$!
for ((i=0; i<100; i++)); do
    [[ -s "$work/port" ]] && break
    sleep .05
done
if [[ ! -s "$work/port" ]]; then
    echo 'FAIL: local HTTP listener did not become ready' >&2
    exit 1
fi
result=0
WINEDEBUG=-all timeout --kill-after=5s 30s wine "$work/native-test.exe" "$(cat "$work/port")" > "$work/wine.log" 2>&1 || result=$?
cat "$work/wine.log"
(( result == 0 )) || exit "$result"
# Wine can report a worker-thread fault during shutdown yet return zero.
# A preceding PASS marker must not turn that execution into clean evidence.
if grep -Eq 'wine: Unhandled (page fault|exception)' "$work/wine.log"; then
    echo 'FAIL: Wine reported an unhandled fault despite exit 0' >&2
    exit 1
fi
