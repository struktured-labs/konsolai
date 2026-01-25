#!/bin/bash
# Konsolai Build Dependencies Installation Script

echo "Installing KDE Frameworks 6 dependencies..."

sudo apt-get update

sudo apt-get install -y \
  cmake \
  extra-cmake-modules \
  qtbase6-dev \
  qt6-base-dev \
  qt6-multimedia-dev \
  libkf6bookmarks-dev \
  libkf6config-dev \
  libkf6configwidgets-dev \
  libkf6coreaddons-dev \
  libkf6crash-dev \
  libkf6guiaddons-dev \
  libkf6i18n-dev \
  libkf6iconthemes-dev \
  libkf6kio-dev \
  libkf6newstuff-dev \
  libkf6notifications-dev \
  libkf6notifyconfig-dev \
  libkf6parts-dev \
  libkf6service-dev \
  libkf6textwidgets-dev \
  libkf6widgetsaddons-dev \
  libkf6windowsystem-dev \
  libkf6xmlgui-dev \
  libkf6pty-dev \
  libkf6dbusaddons-dev \
  libkf6statusnotifieritem-dev \
  tmux

echo ""
echo "Dependencies installed successfully!"
echo "Now run: cd build && cmake .. && make"
