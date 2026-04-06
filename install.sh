#!/bin/bash
# Konsolai system install script
# Requires sudo (installs to /usr, the standard KDE prefix)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# ── Pre-flight dependency checks ──────────────────────────────────────────────

MISSING=0

check_cmd() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERROR: '$1' not found. $2"
        MISSING=1
    fi
}

check_cmd cmake "Install with: sudo apt install cmake  (or: sudo dnf install cmake)"
check_cmd ninja "Install with: sudo apt install ninja-build  (or: sudo dnf install ninja-build)"

# Check for Qt6 development files
if ! pkg-config --exists Qt6Core 2>/dev/null; then
    if ! dpkg -s qt6-base-dev &>/dev/null 2>&1 && ! rpm -q qt6-qtbase-devel &>/dev/null 2>&1; then
        echo "ERROR: Qt6 development files not found."
        echo "  Ubuntu/Debian: sudo apt install qt6-base-dev"
        echo "  Fedora:        sudo dnf install qt6-qtbase-devel"
        echo "  Arch:          sudo pacman -S qt6-base"
        MISSING=1
    fi
fi

# Check for Extra CMake Modules
if ! pkg-config --exists ECM 2>/dev/null; then
    if ! dpkg -s extra-cmake-modules &>/dev/null 2>&1 && ! rpm -q extra-cmake-modules &>/dev/null 2>&1; then
        echo "ERROR: Extra CMake Modules (ECM) not found."
        echo "  Ubuntu/Debian: sudo apt install extra-cmake-modules"
        echo "  Fedora:        sudo dnf install extra-cmake-modules"
        echo "  Arch:          sudo pacman -S extra-cmake-modules"
        MISSING=1
    fi
fi

if [ "$MISSING" -ne 0 ]; then
    echo ""
    echo "Please install the missing dependencies above and re-run this script."
    echo "See README.md for full prerequisite instructions."
    exit 1
fi

echo "Pre-flight checks passed."

# ── Build and install ─────────────────────────────────────────────────────────

if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory not found, configuring..."
    cmake -B "$BUILD_DIR" -G Ninja "$SCRIPT_DIR"
fi

# Build (ninja is incremental — only rebuilds stale targets)
echo "Building Konsolai..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

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
