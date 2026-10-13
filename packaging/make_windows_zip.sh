#!/usr/bin/env bash
# Empaqueta build-win/dist en KageNoMon-<versión>-windows-x64.zip (sin el juego ni la BIOS).
# Uso: packaging/make_windows_zip.sh [versión]   (por defecto, la de src/main.cpp)
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION="${1:-}"
[ -n "$VERSION" ] || VERSION="$(sed -n 's/.*kVersion = "\(v[0-9.]*\)".*/\1/p' src/main.cpp)"
NAME="KageNoMon-$VERSION-windows-x64"
OUT="$ROOT/build-win/$NAME"

rm -rf "$OUT" "$ROOT/$NAME.zip"
mkdir -p "$OUT/game" "$OUT/bios"
cp -r build-win/dist/KageNoMon.exe build-win/dist/SDL2.dll build-win/dist/core build-win/dist/assets \
  build-win/dist/licenses "$OUT/"
cp LICENSE "$OUT/licenses/KageNoMon-GPL-3.0.txt"
# Runtime de C++ (libc++, Apache 2.0 con excepciones LLVM) y de MinGW-w64, enlazados dentro del .exe
# y del núcleo.
cp third_party/llvm-mingw/LICENSE.TXT "$OUT/licenses/LLVM-libc++-Apache-2.0.txt"
cp third_party/llvm-mingw/x86_64-w64-mingw32/share/mingw32/COPYING.MinGW-w64-runtime.txt "$OUT/licenses/MinGW-w64-runtime.txt"
cp third_party/llvm-mingw/x86_64-w64-mingw32/share/mingw32/COPYING.winpthreads.txt "$OUT/licenses/MinGW-w64-winpthreads.txt"

crlf() { sed 's/$/\r/'; }
crlf > "$OUT/LEEME.txt" <<TXT
Kage no Mon $VERSION para Windows 10 y 11 (64 bits)
Lanzador de Tenchu 2 con el núcleo SwanStation. Software libre (GPL-3.0), sin fines de lucro.

CÓMO JUGAR
1. Copia la imagen de tu disco de Tenchu 2 (.cue + .bin, .chd...) en la carpeta "game".
2. Abre KageNoMon.exe.

La BIOS es opcional (viene OpenBIOS integrada). Para el modo hardcore, copia la BIOS de tu
PlayStation en la carpeta "bios".

Si Windows muestra "Windows protegió su PC", pulsa "Más información" y "Ejecutar de todas
formas": el programa no está firmado digitalmente.

No incluye el juego ni la BIOS: usa tu propia copia.
Más información: https://github.com/daetrox/kage-no-mon
TXT
echo "Copia aquí la imagen de tu disco de Tenchu 2 (.cue + .bin, .chd...)." | crlf > "$OUT/game/LEEME.txt"
echo "Opcional: copia aquí la BIOS de tu PlayStation (archivo de 512 KB)." | crlf > "$OUT/bios/LEEME.txt"

(cd "$ROOT/build-win" && cmake -E tar cf "$ROOT/$NAME.zip" --format=zip "$NAME")
echo ">> $ROOT/$NAME.zip"
