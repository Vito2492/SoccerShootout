#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APPDIR="$ROOT/build/SoccerShootout.AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib"

cp "$ROOT/soccer-shootout" "$APPDIR/usr/bin/soccer-shootout"
chmod +x "$APPDIR/usr/bin/soccer-shootout"

if [ -f "$ROOT/snes9x_libretro.so" ]; then
    cp "$ROOT/snes9x_libretro.so" "$APPDIR/usr/bin/snes9x_libretro.so"
elif [ -n "${SNES9X_SO:-}" ] && [ -f "$SNES9X_SO" ]; then
    cp "$SNES9X_SO" "$APPDIR/usr/bin/snes9x_libretro.so"
fi

cp "$ROOT/packaging/linux/soccer-shootout.desktop" "$APPDIR/soccer-shootout.desktop"
cp "$ROOT/packaging/linux/soccer-shootout.svg" "$APPDIR/soccer-shootout.svg"

cat > "$APPDIR/AppRun" <<'EOF'
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "${0}")")"
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
exec "$HERE/usr/bin/soccer-shootout" "$@"
EOF
chmod +x "$APPDIR/AppRun"

if command -v appimagetool >/dev/null 2>&1; then
    ARCH=x86_64 appimagetool "$APPDIR" "$ROOT/SoccerShootout-x86_64.AppImage"
fi
