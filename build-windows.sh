#!/usr/bin/env bash
# Compila Kage no Mon para Windows 10/11 (x86_64) desde Linux, con llvm-mingw.
# Uso: ./build-windows.sh            -> compila en build-win/dist
#      ./build-windows.sh --package  -> además crea KageNoMon-<versión>-windows-x64.zip
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"
TP="$ROOT/third_party"
JOBS="$(nproc)"

# Herramientas en versiones fijas (se comprueba su SHA-256).
LLVM_MINGW_VER="20260922"
LLVM_MINGW_SHA="bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21"
SDL_VER="2.32.10"
SDL_SHA="83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2"
SWAN_COMMIT="dd48056"   # libretro/swanstation (GPL-3.0)

mkdir -p "$TP/win-dl"

fetch() # url archivo sha256
{
  if [ ! -f "$2" ] || ! echo "$3  $2" | sha256sum -c --quiet - 2>/dev/null; then
    curl -sSfL -o "$2" "$1"
    echo "$3  $2" | sha256sum -c --quiet -
  fi
}

if [ ! -x "$TP/llvm-mingw/bin/x86_64-w64-mingw32-clang++" ]; then
  fetch "https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW_VER/llvm-mingw-$LLVM_MINGW_VER-ucrt-ubuntu-22.04-x86_64.tar.xz" \
        "$TP/win-dl/llvm-mingw.tar.xz" "$LLVM_MINGW_SHA"
  rm -rf "$TP/llvm-mingw" && mkdir -p "$TP/llvm-mingw"
  tar -xJf "$TP/win-dl/llvm-mingw.tar.xz" -C "$TP/llvm-mingw" --strip-components=1
fi
if [ ! -f "$TP/sdl2-win/x86_64-w64-mingw32/bin/SDL2.dll" ]; then
  fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-devel-$SDL_VER-mingw.tar.gz" \
        "$TP/win-dl/sdl2-mingw.tar.gz" "$SDL_SHA"
  rm -rf "$TP/sdl2-win" && mkdir -p "$TP/sdl2-win"
  tar -xzf "$TP/win-dl/sdl2-mingw.tar.gz" -C "$TP/sdl2-win" --strip-components=1
fi

# Fuentes (las mismas que build.sh).
if [ ! -d "$TP/swanstation" ]; then
  git clone https://github.com/libretro/swanstation.git "$TP/swanstation"
  git -C "$TP/swanstation" checkout "$SWAN_COMMIT"
fi
if ! git -C "$TP/swanstation" apply --reverse --check "$ROOT/patches/swanstation-kage-vram.patch" 2>/dev/null; then
  git -C "$TP/swanstation" apply "$ROOT/patches/swanstation-kage-vram.patch"
fi
[ -d "$TP/rcheevos" ] || git clone --depth 1 https://github.com/RetroAchievements/rcheevos.git "$TP/rcheevos"
[ -d "$TP/libretro-common" ] || git clone --depth 1 https://github.com/libretro/libretro-common.git "$TP/libretro-common"

export MINGW_PREFIX="$TP/llvm-mingw"
TOOLCHAIN="$ROOT/cmake/mingw-w64.cmake"

# --- Núcleo SwanStation (DLL con el runtime de C++ enlazado dentro) ---
# libc++ es más estricta que libstdc++ con las cabeceras: se incluye <cstdlib> en todo el núcleo.
if [ ! -f "$TP/build-swan-win/build.ninja" ]; then
  cmake -S "$TP/swanstation" -B "$TP/build-swan-win" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_CXX_FLAGS="-include cstdlib" -DCMAKE_SHARED_LINKER_FLAGS="-static"
fi
ninja -C "$TP/build-swan-win" -j"$JOBS"

# --- Launcher ---
cmake -S "$ROOT" -B "$ROOT/build-win" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
ninja -C "$ROOT/build-win" -j"$JOBS"
echo ">> Listo: $ROOT/build-win/dist/KageNoMon.exe"

if [ "${1:-}" = "--package" ]; then
  "$ROOT/packaging/make_windows_zip.sh" "${2:-}"
fi
