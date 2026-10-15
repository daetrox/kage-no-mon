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
mkdir -p "$APPDIR/usr/lib/tenchu2"
cp -r "$ROOT/build/dist/." "$APPDIR/usr/lib/tenchu2/"
# Nunca empaquetar el juego ni las BIOS del usuario.
rm -rf "$APPDIR/usr/lib/tenchu2/game" "$APPDIR/usr/lib/tenchu2/bios" "$APPDIR/usr/lib/tenchu2/tenchu2.desktop"

cp "$ROOT/packaging/tenchu2.png" "$APPDIR/tenchu2.png"
cat > "$APPDIR/tenchu2.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Kage no Mon
Comment=La Puerta de las Sombras: lanzador para Tenchu 2
Exec=tenchu2
Icon=tenchu2
Categories=Game;
DESKTOP
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/lib/tenchu2/tenchu2" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

if [ ! -x "$TOOL" ]; then
  curl -sSfL -o "$TOOL" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
  chmod +x "$TOOL"
fi
# Sin FUSE (p. ej. en GitHub Actions) la herramienta se extrae y se ejecuta sola.
OUT="$ROOT/KageNoMon-$VERSION-x86_64.AppImage"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$TOOL" --no-appstream "$APPDIR" "$OUT"
echo ">> AppImage: $OUT"
