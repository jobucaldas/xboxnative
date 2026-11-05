#!/bin/bash
# Off-device only. Ephemeral self-signed fixture; no system trust-store writes.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 2 \
    -subj '/CN=sysx-offdevice-untrusted' -set_serial 3405340 \
    -keyout "$work/key.pem" -outform DER -out "$work/certificate.der" \
    > "$work/openssl.log" 2>&1
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static certificate-test.cpp -o "$work/certificate-test.exe"
certificate_path=$(WINEDEBUG=-all timeout 10s winepath -w "$work/certificate.der")
result=0
WINEDEBUG=-all timeout 30s wine "$work/certificate-test.exe" "$certificate_path" \
    > "$work/wine.log" 2>&1 || result=$?
cat "$work/wine.log"
(( result == 0 )) || exit "$result"
if grep -Eq 'wine: Unhandled (page fault|exception)' "$work/wine.log"; then
    echo 'FAIL: Wine reported an unhandled fault despite exit 0' >&2
    exit 1
fi
