#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-kit.sh [OUT_ROOT]; see README.md for source/tool path overrides.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "${KIT_SOURCE_ROOT:-$here/../..}" && pwd)"
build_root="$(cd "${KIT_BUILD_ROOT:-$root}" && pwd)"
cd "$root"

out_root="${1:-${KIT_OUT_ROOT:-$root/out/kit}}"
mkdir -p "$out_root"
out_root="$(cd "$out_root" && pwd)"
# Unique directories prevent concurrent builds or same-second rebuilds colliding.
kitdir="$(mktemp -d "$out_root/$(date +%Y%m%d-%H%M%S).XXXXXX")"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/kit-build.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
py() { python3 "$here/kitlib.py" "$@"; }
manifest_rows="$tmp/rows.tsv"
: > "$manifest_rows"

# Read identity/version from the actual MSIX, not a possibly different source XML.
add_package() {
    local name="$1" msix="$2" cer="$3" metadata digest
    metadata="$(py package-identity "$msix")"
    digest="$(py sha256 "$msix")"
    cp "$msix" "$kitdir/$name.msix"
    cp "$cer" "$kitdir/$name.cer"
    printf '%s\t%s\t%s\t%s\tpackage\n' "$name" "$metadata" "$name.msix" "$digest" >> "$manifest_rows"
    echo "kit: added $name ($metadata)"
}
add_extra() {
    local name="$1" src="$2" digest
    digest="$(py sha256 "$src")"
    cp "$src" "$kitdir/$name"
    printf '%s\t\t\t%s\t%s\textra\n' "$name" "$name" "$digest" >> "$manifest_rows"
}

# Explicit package set: required builders must fail rather than silently omit a
# package. The native compatibility-mode experiment is deliberately not shipped.
host="$build_root/xbox-test/uwph/host/build.sh"
bash "$host" "$tmp/host-uwph" uwph
add_package uwph "$tmp/host-uwph/k32.msix" "$tmp/host-uwph/test.cer"
add_extra wxl-test.exe "$tmp/host-uwph/layout/wxl-test.exe"
bash "$host" "$tmp/host-wxl" wxl
add_package wxl "$tmp/host-wxl/k32.msix" "$tmp/host-wxl/test.cer"
bash "$host" "$tmp/host-hid" hid
add_package hid "$tmp/host-hid/k32.msix" "$tmp/host-hid/test.cer"
bash "$host" "$tmp/host-mlp" mlp
add_package mlp "$tmp/host-mlp/k32.msix" "$tmp/host-mlp/test.cer"
bash "$host" "$tmp/host-tls" tls
add_package tls "$tmp/host-tls/k32.msix" "$tmp/host-tls/test.cer"
add_extra tls-test.exe "$tmp/host-tls/layout/tls-test.exe"
add_extra tls-test.dll "$tmp/host-tls/layout/tls-test.dll"
add_extra lifecycle.dll "$tmp/host-tls/layout/lifecycle.dll"
bash "$host" "$tmp/host-swc" swc
add_package swc "$tmp/host-swc/k32.msix" "$tmp/host-swc/test.cer"
add_extra swc-test.exe "$tmp/host-swc/layout/swc-test.exe"
bash "$build_root/xbox-test/uwph/display/build.sh" "$tmp/display"
add_package w32u "$tmp/display/w32u.msix" "$tmp/display/test.cer"
bash "$build_root/xbox-test/vrx/build.sh" "$tmp/vrx"
add_package vrx "$tmp/vrx/vrx.msix" "$tmp/vrx/test.cer"

# Default to this source tree. An empty value explicitly omits the launcher;
# a nonempty override must exist and build successfully (no best-effort skip).
launcher_root="${LAUNCHER_SOURCE_ROOT-$build_root}"
if [ -n "$launcher_root" ]; then
    bash "$launcher_root/xbox-test/app/build.sh" "$tmp/launcher"
    add_package launcher "$tmp/launcher/xbtest.msix" "$tmp/launcher/test.cer"
fi

py write-manifest "$kitdir/manifest.json" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$(git rev-parse HEAD)" "$(git rev-parse --abbrev-ref HEAD)" < "$manifest_rows"
bash "$here/sync-readme-versions.sh" "$kitdir/manifest.json"
# Do not publish an unverified or partial build as latest. All existing gates
# retain their signature, hash, manifest, blockmap and guide checks.
bash "$here/verify-kit.sh" "$kitdir"
bash "$here/verify-kit-offline.sh" "$kitdir"
bash "$here/check-install-guide.sh"
ln -sfn "$(basename "$kitdir")" "$out_root/latest"
echo "kit: built and verified $kitdir"
