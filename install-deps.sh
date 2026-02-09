#!/bin/bash
set -e

DEPS=(
  extra-cmake-modules ninja-build
  qt6-base-dev qt6-multimedia-dev
  libkf6bookmarks-dev libkf6config-dev libkf6configwidgets-dev
  libkf6coreaddons-dev libkf6crash-dev libkf6guiaddons-dev
  libkf6i18n-dev libkf6iconthemes-dev libkf6kio-dev
  libkf6newstuff-dev libkf6notifications-dev libkf6notifyconfig-dev
  libkf6parts-dev libkf6service-dev libkf6textwidgets-dev
  libkf6widgetsaddons-dev libkf6windowsystem-dev libkf6xmlgui-dev
  libkf6pty-dev libkf6dbusaddons-dev libkf6globalaccel-dev
  libkf6doctools-dev libkf6statusnotifieritem-dev
  libicu-dev
)

# Install dependencies
echo "==> Installing dependencies..."
sudo apt-get install -y "${DEPS[@]}"

# Configure
echo "==> Configuring..."
cd "$(dirname "$0")"
rm -rf build
mkdir build
cd build
cmake -G Ninja ..

# Build
echo "==> Building..."
ninja -j4

# Test
echo "==> Running tests..."
ctest --output-on-failure

# Install
echo "==> Installing..."
sudo ninja install

echo "==> Done!"
