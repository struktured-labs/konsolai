# Konsolai Build Instructions

This file is kept for historical compatibility. For up-to-date build instructions,
prerequisites, and package install commands, see the **[README](README.md#prerequisites)**.

## Quick Start

```bash
mkdir build && cd build
cmake ..
ninja -j4
sudo cmake --install .
```

Or use the install script which includes pre-flight dependency checks:

```bash
./install.sh
```
