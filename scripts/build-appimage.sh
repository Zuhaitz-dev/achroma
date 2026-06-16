#!/bin/bash
# Build Achroma AppImage
# Requires: cmake, g++, Qt6, qtermwidget6, linuxdeploy-x86_64.AppImage
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/appimage"
APPDIR="$BUILD_DIR/AppDir"

echo "=== Building Achroma for AppImage ==="

# Clean build
rm -rf "$BUILD_DIR"
cmake -B "$BUILD_DIR" -S "$PROJECT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTS=OFF
make -j$(nproc) -C "$BUILD_DIR"

# Create AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"

cp "$BUILD_DIR/Achroma" "$APPDIR/usr/bin/achroma"
cp "$PROJECT_DIR/achroma-cli" "$APPDIR/usr/bin/"
cp "$PROJECT_DIR/Achroma.desktop" "$APPDIR/usr/share/applications/"
cp "$PROJECT_DIR/achroma.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/"

# Create AppRun
cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export PATH="$HERE/usr/bin:$PATH"
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$HERE/usr/lib/qt6/plugins"
export QTWEBENGINE_RESOURCES_PATH="$HERE/usr/share/qt6/resources"
export QTWEBENGINE_TRANSLATIONS_PATH="$HERE/usr/share/qt6/translations"
exec "$HERE/usr/bin/achroma" "$@"
EOF
chmod +x "$APPDIR/AppRun"

# Default icon for AppDir
cp "$PROJECT_DIR/achroma.svg" "$APPDIR/achroma.svg"
ln -sf achroma.svg "$APPDIR/.DirIcon"

echo ""
echo "=== AppDir ready at $APPDIR ==="
echo "To package as AppImage, install linuxdeploy:"
echo "  wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
echo "  chmod +x linuxdeploy-x86_64.AppImage"
echo "  ./linuxdeploy-x86_64.AppImage --appdir $APPDIR --output appimage"
echo ""
echo "Or run directly: $APPDIR/AppRun"
