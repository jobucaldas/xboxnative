#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-fna-native.sh [runtime-dir] -> pinned x64 SDL3, FNA3D, FAudio and Theorafile DLLs.
# Requires CMake, Ninja, make, git, MinGW x64 gcc and objdump; output is never committed.
# Use a UCRT-flavoured MinGW toolchain; MCFGTHREAD_LIBDIR/MCFGTHREAD_INCDIR are
# needed only by toolchains whose libstdc++ uses the mcfgthread runtime.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=${1:-$root/out/fna-native}
for t in git cmake ninja make x86_64-w64-mingw32-gcc x86_64-w64-mingw32-objdump; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }
done
link_flags=(-static)
cxx_flags=(-static-libgcc -static-libstdc++)
if [[ -n ${MCFGTHREAD_LIBDIR:-} ]]; then
  [[ -d $MCFGTHREAD_LIBDIR ]] || { echo "MCFGTHREAD_LIBDIR not found: $MCFGTHREAD_LIBDIR" >&2; exit 2; }
  link_flags+=("-L$MCFGTHREAD_LIBDIR")
fi
if [[ -n ${MCFGTHREAD_INCDIR:-} ]]; then
  [[ -d $MCFGTHREAD_INCDIR ]] || { echo "MCFGTHREAD_INCDIR not found: $MCFGTHREAD_INCDIR" >&2; exit 2; }
  cxx_flags+=("-isystem" "$MCFGTHREAD_INCDIR")
fi
probe=$(mktemp -d)
trap 'rm -rf "$probe"' EXIT
printf 'int main(void){return 0;}\n' > "$probe/ucrt.c"
x86_64-w64-mingw32-gcc "${link_flags[@]}" "$probe/ucrt.c" -o "$probe/ucrt.exe" 2>"$probe/link.log" \
  || { cat "$probe/link.log" >&2; echo "cannot link with x86_64-w64-mingw32-gcc (set MCFGTHREAD_LIBDIR if needed)" >&2; exit 2; }
if x86_64-w64-mingw32-objdump -p "$probe/ucrt.exe" | grep -qi 'DLL Name: msvcrt\.dll'; then
  echo "x86_64-w64-mingw32-gcc links msvcrt.dll; use a UCRT-flavoured MinGW toolchain" >&2
  exit 2
fi
cmake_flags=("-DCMAKE_C_FLAGS=-static-libgcc" "-DCMAKE_CXX_FLAGS=${cxx_flags[*]}" \
  "-DCMAKE_EXE_LINKER_FLAGS=${link_flags[*]}" "-DCMAKE_SHARED_LINKER_FLAGS=${link_flags[*]}")
repo_line() { grep -E "^$1[[:space:]]" "$root/deps.pin"; }
cache="$root/third_party/fna-native"
mkdir -p "$cache" "$out"
checkout_pin() {
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
  if [[ -n ${tag:-} ]]; then
    git -C "$dest" fetch -q --depth=1 origin "refs/tags/$tag:refs/tags/$tag"
    git -C "$dest" describe --tags --exact-match HEAD | grep -Fxq "$tag"
  fi
}
sdl="$cache/SDL3"; faudio="$cache/FAudio"; fna3d="$cache/FNA3D"; mojo="$cache/MojoShader"; theorafile="$cache/Theorafile"
checkout_pin fna-sdl3-native "$sdl"
checkout_pin fna-faudio "$faudio"
checkout_pin fna-fna3d "$fna3d"
checkout_pin fna-mojoshader "$mojo"
checkout_pin fna-theorafile "$theorafile"
# FNA3D's git tree pins MojoShader as a submodule; materialize that exact source.
rm -rf "$fna3d/MojoShader"
git clone -q --no-checkout "$mojo" "$fna3d/MojoShader"
git -C "$fna3d/MojoShader" checkout -q --detach "$(git -C "$mojo" rev-parse HEAD)"
toolchain="$sdl/build-scripts/cmake-toolchain-mingw64-x86_64.cmake"
install="$cache/install"
cmake -S "$sdl" -B "$cache/build/SDL3" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release "${cmake_flags[@]}" \
  -DCMAKE_INSTALL_PREFIX="$install" -DSDL_SHARED=ON -DSDL_STATIC=OFF \
  -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=ON \
  -DSDL_INSTALL_TESTS=OFF -DSDL_INSTALL_DOCS=OFF -DSDL_INSTALL_CPACK=OFF
cmake --build "$cache/build/SDL3" --parallel
cmake --install "$cache/build/SDL3"
# FNA 26.11 defaults to SDL3. FAudio deliberately uses its SDL audio backend,
# not the Win32/XAudio2 backend; audio-device availability remains platform-defined.
cmake -S "$faudio" -B "$cache/build/FAudio" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release "${cmake_flags[@]}" \
  -DCMAKE_PREFIX_PATH="$install" -DCMAKE_INSTALL_PREFIX="$install" \
  -DBUILD_SDL3=ON -DPLATFORM_WIN32=OFF -DWMA_DECODING=OFF \
  -DBUILD_UTILS=OFF -DBUILD_TESTS=OFF -DBUILD_SHARED_LIBS=ON -DFAUDIO_INSTALL=ON
cmake --build "$cache/build/FAudio" --parallel
cmake --install "$cache/build/FAudio"
# On Windows FNA3D enables its D3D11 driver; SDL3 supplies the window/native-handle path.
cmake -S "$fna3d" -B "$cache/build/FNA3D" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release "${cmake_flags[@]}" \
  -DCMAKE_PREFIX_PATH="$install" -DCMAKE_INSTALL_PREFIX="$install" \
  -DBUILD_SDL3=ON -DBUILD_SHARED_LIBS=ON -DTRACING_SUPPORT=OFF
cmake --build "$cache/build/FNA3D" --parallel
cmake --install "$cache/build/FNA3D"
# Theorafile's Makefile builds only the DLL with `lib`; no test executable is staged.
make -C "$theorafile" lib CC=x86_64-w64-mingw32-gcc LDFLAGS="${link_flags[*]}"
for dll in SDL3.dll FAudio.dll FNA3D.dll; do
  [[ -f "$install/bin/$dll" ]] || { echo "missing native FNA output: $install/bin/$dll" >&2; exit 1; }
  cp "$install/bin/$dll" "$out/"
done
[[ -f "$theorafile/libtheorafile.dll" ]] || { echo "missing native FNA output: $theorafile/libtheorafile.dll" >&2; exit 1; }
cp "$theorafile/libtheorafile.dll" "$out/"
cp "$sdl/LICENSE.txt" "$out/LICENSE-SDL3.txt"
cp "$faudio/LICENSE" "$out/LICENSE-FAudio.txt"
cp "$fna3d/LICENSE" "$out/LICENSE-FNA3D.txt"
cp "$mojo/LICENSE.txt" "$out/LICENSE-MojoShader.txt"
cp "$theorafile/licenses/LICENSE" "$out/LICENSE-Theorafile.txt"
cp "$theorafile/licenses/libogg.LICENSE" "$out/LICENSE-libogg.txt"
cp "$theorafile/licenses/libtheora.LICENSE" "$out/LICENSE-libtheora.txt"
cp "$theorafile/licenses/libvorbis.LICENSE" "$out/LICENSE-libvorbis.txt"
printf 'SDL3 %s\nFAudio %s\nFNA3D %s (tag 26.11)\nMojoShader %s\nTheorafile %s\n' \
  "$(git -C "$sdl" rev-parse HEAD)" "$(git -C "$faudio" rev-parse HEAD)" \
  "$(git -C "$fna3d" rev-parse HEAD)" "$(git -C "$mojo" rev-parse HEAD)" \
  "$(git -C "$theorafile" rev-parse HEAD)" > "$out/SOURCE-fna-native.txt"
printf 'FNA native x64 DLLs -> %s\n' "$out"
