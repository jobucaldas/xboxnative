#!/bin/bash
# build-kit.sh [OUT_ROOT=/workspace/x64-run/kit]
#
# Builds every current signed test package into OUT_ROOT/<timestamp>/:
# the merged uwph host (k32+ws+d3dt+wxl-mapper checks), the isolated wxl
# identity, the w32u display-adapter spike, the vrx W^X probe, and (read-only,
# from the lane/dl worktree) the Grok.XboxTest launcher, if that worktree is
# present. Each package gets its own self-signed .msix + public .cer (never
# a .pfx). Writes manifest.json (identity, version, sha256 per package) and
# updates OUT_ROOT/latest to point at this run.
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
cd "$root"

out_root="${1:-/workspace/x64-run/kit}"
stamp="$(date +%Y%m%d-%H%M%S)"
kitdir="$out_root/$stamp"
mkdir -p "$kitdir"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/kit-build.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

py() { python3 "$here/kitlib.py" "$@"; }

manifest_rows="$tmp/rows.tsv"
: > "$manifest_rows"

# add_package NAME MSIX CERT MANIFEST_XML
add_package() {
    local name="$1" msix="$2" cer="$3" manifest_xml="$4"
    local identity version digest
    IFS=$'\t' read -r identity version < <(py identity "$manifest_xml")
    digest="$(py sha256 "$msix")"
    cp "$msix" "$kitdir/$name.msix"
    cp "$cer" "$kitdir/$name.cer"
    printf '%s\t%s\t%s\t%s\t%s\tpackage\n' "$name" "$identity" "$version" "$name.msix" "$digest" >> "$manifest_rows"
    echo "kit: added $name ($identity $version)"
}

# add_extra NAME SRC_FILE DEST_FILE
add_extra() {
    local name="$1" src="$2" dest="$3"
    local digest
    digest="$(py sha256 "$src")"
    cp "$src" "$kitdir/$dest"
    printf '%s\t\t\t%s\t%s\textra\n' "$name" "$dest" "$digest" >> "$manifest_rows"
    echo "kit: added extra $name"
}

echo "=== kit: uwph (merged host: k32+ws+d3dt+wxl-mapper) ==="
bash xbox-test/uwph/host/build.sh "$tmp/host-uwph" uwph
add_package uwph "$tmp/host-uwph/k32.msix" "$tmp/host-uwph/test.cer" xbox-test/uwph/host/AppxManifest.xml
# wxl_test.exe is the guest the on-device WXL mapper check loads from
# LocalState (README documents the put-file step); ship it once, it is the
# same binary regardless of which manifest variant packaged the host.
add_extra wxl-test.exe "$tmp/host-uwph/layout/wxl-test.exe" wxl-test.exe

echo "=== kit: wxl (isolated mapper identity) ==="
bash xbox-test/uwph/host/build.sh "$tmp/host-wxl" wxl
add_package wxl "$tmp/host-wxl/k32.msix" "$tmp/host-wxl/test.cer" xbox-test/uwph/host/AppxManifest.Wxl.xml

echo "=== kit: w32u (CoreWindow display adapter) ==="
bash xbox-test/uwph/display/build.sh "$tmp/display"
add_package w32u "$tmp/display/w32u.msix" "$tmp/display/test.cer" xbox-test/uwph/display/AppxManifest.xml

echo "=== kit: vrx (W^X / RWX probe) ==="
bash xbox-test/vrx/build.sh "$tmp/vrx"
add_package vrx "$tmp/vrx/vrx.msix" "$tmp/vrx/test.cer" xbox-test/vrx/AppxManifest.xml

# Any future test package with its own AppxManifest.xml (e.g. a merged hid
# identity) is picked up automatically, as long as its build.sh follows the
# existing "build.sh OUTDIR" convention. Known directories above are skipped;
# anything else found is built best-effort and never aborts the whole kit.
known_manifests="xbox-test/uwph/host/AppxManifest.xml
xbox-test/uwph/host/AppxManifest.Wxl.xml
xbox-test/uwph/display/AppxManifest.xml
xbox-test/vrx/AppxManifest.xml"
while IFS= read -r manifest_xml; do
    grep -qxF "$manifest_xml" <<< "$known_manifests" && continue
    dir="$(dirname "$manifest_xml")"
    [ -f "$dir/build.sh" ] || continue
    name="$(basename "$dir")"
    echo "=== kit: auto-detected extra package '$name' ($dir) ==="
    build_out="$tmp/extra-$name"
    if ! bash "$dir/build.sh" "$build_out"; then
        echo "kit: WARNING: $dir/build.sh failed, skipping '$name'" >&2
        continue
    fi
    msix="$(find "$build_out" -maxdepth 1 -name '*.msix' ! -name '*-unsigned.msix' | head -1)"
    cer="$(find "$build_out" -maxdepth 1 -name '*.cer' | head -1)"
    if [ -z "$msix" ] || [ -z "$cer" ]; then
        echo "kit: WARNING: $dir/build.sh did not produce a signed .msix/.cer, skipping '$name'" >&2
        continue
    fi
    add_package "$name" "$msix" "$cer" "$manifest_xml"
done < <(find xbox-test -mindepth 2 -maxdepth 3 -name AppxManifest.xml | sort)

dl_worktree="/workspace/wt/dl"
dl_build="$dl_worktree/xbox-test/app/build.sh"
if [ -f "$dl_build" ]; then
    echo "=== kit: launcher (read-only build from lane/dl) ==="
    bash "$dl_build" "$tmp/launcher"
    add_package launcher "$tmp/launcher/xbtest.msix" "$tmp/launcher/test.cer" "$dl_worktree/xbox-test/app/AppxManifest.xml"
else
    echo "kit: launcher not found at $dl_build, skipped (lane/dl worktree absent)"
fi

generated_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
commit="$(git rev-parse HEAD)"
branch="$(git rev-parse --abbrev-ref HEAD)"
py write-manifest "$kitdir/manifest.json" "$generated_at" "$commit" "$branch" < "$manifest_rows"

ln -sfn "$stamp" "$out_root/latest"

echo "=== kit: built $kitdir ==="
ls -l "$kitdir"
echo "kit: $out_root/latest -> $stamp"
