#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-fna.sh [outdir] -> pinned FNA plus XNA ABI forwarding assemblies.
# Requires git, xbuild, mcs and Mono 6.x. Source and generated assemblies are never committed.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=${1:-$root/out/fna}
for t in git xbuild mcs; do command -v "$t" >/dev/null || { echo "missing tool: $t (use nix shell nixpkgs#mono)" >&2; exit 2; }; done
repo_line() { grep -E "^$1[[:space:]]" "$root/deps.pin"; }
clone_pin() {
  local key=$1 dest=$2 line url rev tag
  line=$(repo_line "$key") || { echo "$key missing from deps.pin" >&2; exit 2; }
  read -r _ url rev tag <<<"$line"
  if [[ ! -d $dest/.git ]]; then
    mkdir -p "$(dirname "$dest")"
    git clone -q --no-checkout "$url" "$dest"
  fi
  git -C "$dest" fetch -q --depth=1 origin "$rev"
  git -C "$dest" checkout -q --detach "$rev"
  [[ $(git -C "$dest" rev-parse HEAD) == "$rev" ]] || { echo "$key checkout mismatch" >&2; exit 1; }
  if [[ -n ${tag:-} ]]; then git -C "$dest" fetch -q --depth=1 origin "refs/tags/$tag:refs/tags/$tag"; git -C "$dest" describe --tags --exact-match HEAD | grep -Fxq "$tag"; fi
}
cache="$root/third_party/fna"
mkdir -p "$cache" "$out"
fna="$cache/FNA"; netstub="$cache/FNA.NetStub-src"
clone_pin fna "$fna"
clone_pin fna-netstub "$netstub"
# Materialize only the submodules pinned by the FNA 26.11 git tree.
for entry in 'lib/FAudio fna-faudio' 'lib/FNA3D fna-fna3d' 'lib/SDL2-CS fna-sdl2-cs' 'lib/SDL3-CS fna-sdl3-cs' 'lib/Theorafile fna-theorafile' 'lib/dav1dfile fna-dav1dfile'; do
  read -r path key <<<"$entry"
  line=$(repo_line "$key") || { echo "$key missing from deps.pin" >&2; exit 2; }
  read -r _ url rev <<<"$line"
  dest="$fna/$path"
  if [[ ! -d $dest/.git ]]; then rm -rf "$dest"; git clone -q --no-checkout "$url" "$dest"; fi
  git -C "$dest" fetch -q --depth=1 origin "$rev"
  git -C "$dest" checkout -q --detach "$rev"
  [[ $(git -C "$dest" rev-parse HEAD) == "$rev" ]] || { echo "$key checkout mismatch" >&2; exit 1; }
done
# The ABI solution expects FNA.NetStub next to the FNA checkout.
rm -rf "$fna/../FNA.NetStub"
ln -s "$netstub" "$fna/../FNA.NetStub"
xbuild "$netstub/FNA.NetStub.csproj" /p:Configuration=Release /p:Platform=AnyCPU /verbosity:minimal
xbuild "$fna/FNA.csproj" /p:Configuration=Release /p:Platform=AnyCPU /verbosity:minimal
mkdir -p "$out"
cp "$fna/bin/Release/FNA.dll" "$out/"
cp "$netstub/bin/Release/FNA.NetStub.dll" "$out/"
# Build all official XNA 4 name forwarders against FNA and the pinned network stubs.
for name in Microsoft.Xna.Framework Microsoft.Xna.Framework.Avatar Microsoft.Xna.Framework.Game Microsoft.Xna.Framework.GamerServices Microsoft.Xna.Framework.Graphics Microsoft.Xna.Framework.Input.Touch Microsoft.Xna.Framework.Net Microsoft.Xna.Framework.Storage Microsoft.Xna.Framework.Video Microsoft.Xna.Framework.Xact; do
  mcs -nologo -target:library -r:"$out/FNA.dll" -r:"$out/FNA.NetStub.dll" \
    -out:"$out/$name.dll" "$fna/abi/$name.cs"
done
cp "$fna/licenses/LICENSE" "$out/LICENSE-FNA.txt"
cp "$fna/licenses/lzxdecoder.LICENSE" "$out/LICENSE-lzxdecoder.txt"
cp "$fna/licenses/monoxna.LICENSE" "$out/LICENSE-monoxna.txt"
cp "$netstub/LICENSE" "$out/LICENSE-FNA-NetStub.txt"
cp "$fna/lib/FAudio/LICENSE" "$out/LICENSE-FAudio.txt"
cp "$fna/lib/FAudio/csharp/LICENSE" "$out/LICENSE-FAudio-CSharp.txt"
cp "$fna/lib/FNA3D/LICENSE" "$out/LICENSE-FNA3D.txt"
cp "$fna/lib/SDL2-CS/LICENSE" "$out/LICENSE-SDL2-CS.txt"
cp "$fna/lib/SDL3-CS/LICENSE" "$out/LICENSE-SDL3-CS.txt"
cp "$fna/lib/Theorafile/csharp/LICENSE" "$out/LICENSE-Theorafile.txt"
cp "$fna/lib/dav1dfile/LICENSE" "$out/LICENSE-dav1dfile.txt"
printf 'FNA 26.11 source %s\nFNA.NetStub source %s\n' \
  "$(git -C "$fna" rev-parse HEAD)" "$(git -C "$netstub" rev-parse HEAD)" > "$out/SOURCE.txt"
printf 'FNA 26.11 assemblies -> %s\n' "$out"
