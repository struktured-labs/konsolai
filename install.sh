#!/bin/bash
# Konsolai system install script
# Requires sudo (installs to /usr, the standard KDE prefix)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory not found at $BUILD_DIR"
    echo "Run cmake and build first."
    exit 1
fi

echo "Installing Konsolai to system (requires sudo)..."
sudo cmake --install "$BUILD_DIR"

# Install konsolai icons to hicolor theme
echo "Installing icons..."
for size in 16 22 32 48 64 128; do
    icon="$SCRIPT_DIR/data/icons/${size}-apps-konsolai.png"
    dest="/usr/share/icons/hicolor/${size}x${size}/apps/konsolai.png"
    if [ -f "$icon" ]; then
        sudo install -Dm644 "$icon" "$dest"
    fi
done

# Update icon cache
echo "Updating icon cache..."
sudo gtk-update-icon-cache /usr/share/icons/hicolor/ 2>/dev/null || true
sudo ldconfig

echo ""
echo "Konsolai installed successfully."
echo "You can launch it from the KDE application menu or run: konsolai"
