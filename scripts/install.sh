#!/bin/bash
# Quick install script for Achroma
# Usage: ./scripts/install.sh [--uninstall]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PREFIX="${PREFIX:-/usr/local}"

if [ "${1:-}" = "--uninstall" ]; then
    echo "=== Uninstalling Achroma ==="
    cd "$PROJECT_DIR"
    if [ -f build/install_manifest.txt ]; then
        sudo make -C build uninstall
        echo "Uninstalled."
    else
        echo "No install manifest found. Run ./scripts/install.sh first."
    fi
    exit 0
fi

echo "=== Installing Achroma system-wide ==="
echo "Prefix: $PREFIX"
echo ""

cd "$PROJECT_DIR"

# Build if needed
if [ ! -f build/Achroma ]; then
    echo "Building..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_TESTS=OFF
    make -j$(nproc) -C build
fi

echo "Installing (requires sudo)..."
sudo make -C build install
echo ""
echo "Achroma installed!"
echo "  Run: achroma"
echo "  CLI: achroma-cli tabs"
echo "  Uninstall: ./scripts/install.sh --uninstall"
