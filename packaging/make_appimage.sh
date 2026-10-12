#!/usr/bin/env bash
# Empaqueta build/dist como AppImage (x86_64). Requiere haber ejecutado ./build.sh antes.
# Uso: packaging/make_appimage.sh [versión]
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION="${1:-dev}"
APPDIR="$ROOT/build/AppDir"
TOOL="$ROOT/build/appimagetool-x86_64.AppImage"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/lib/tenshu2"
cp -r "$ROOT/build/dist/." "$APPDIR/usr/lib/tenshu2/"
# Nunca empaquetar el juego ni las BIOS del usuario.
rm -rf "$APPDIR/usr/lib/tenshu2/game" "$APPDIR/usr/lib/tenshu2/bios" "$APPDIR/usr/lib/tenshu2/tenshu2.desktop"

cp "$ROOT/packaging/tenshu2.png" "$APPDIR/tenshu2.png"
cat > "$APPDIR/tenshu2.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Tenshu 2
Comment=Lanzador con núcleo SwanStation
Exec=tenshu2
Icon=tenshu2
Categories=Game;
DESKTOP
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/lib/tenshu2/tenshu2" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

if [ ! -x "$TOOL" ]; then
  curl -sSfL -o "$TOOL" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
  chmod +x "$TOOL"
fi
# Sin FUSE (p. ej. en GitHub Actions) la herramienta se extrae y se ejecuta sola.
OUT="$ROOT/Tenshu2-$VERSION-x86_64.AppImage"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$TOOL" --no-appstream "$APPDIR" "$OUT"
echo ">> AppImage: $OUT"
