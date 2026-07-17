#!/usr/bin/env bash
# One-time, idempotent install of the pinned ESP-IDF toolchain.
# Never touches connected hardware. Safe to re-run.
#
# What it does:
#   1. checks host prerequisites (git, cmake, python3 >= 3.10)
#   2. clones ESP-IDF at the tag in .espidf-version (large: ~1.5 GB w/ submodules)
#   3. runs ESP-IDF's own tool installer for the esp32 target
#      (Xtensa toolchain + python env land in ~/.espressif)
#
# Follows the official flow: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/macos-setup.html
# (EIM is the interactive alternative; point BOOMBOX_IDF_PATH at an EIM install to reuse it.)
set -euo pipefail

# shellcheck source=scripts/env.sh
source "$(cd "$(dirname "$0")" && pwd)/env.sh"

echo "==> Boombox bootstrap: ESP-IDF $IDF_VERSION -> $IDF_INSTALL_DIR"

for tool in git cmake; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: '$tool' not found. On macOS: brew install cmake python" >&2
        exit 1
    fi
done
if ! python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null; then
    echo "error: python3 >= 3.10 required for ESP-IDF v6 (found: $(python3 --version 2>&1))." >&2
    echo "On macOS: brew install python, and ensure Homebrew python precedes /usr/bin in PATH." >&2
    exit 1
fi

if [ -d "$IDF_INSTALL_DIR" ]; then
    existing="$(git -C "$IDF_INSTALL_DIR" describe --tags --exact-match 2>/dev/null || echo unknown)"
    if [ "$existing" != "$IDF_VERSION" ]; then
        echo "error: $IDF_INSTALL_DIR exists but is at '$existing', not '$IDF_VERSION'." >&2
        echo "Remove it, or point BOOMBOX_IDF_PATH at an install matching the pin." >&2
        exit 1
    fi
    echo "==> ESP-IDF already present at the pinned tag; skipping clone"
else
    echo "==> Cloning ESP-IDF $IDF_VERSION (this downloads ~1.5 GB)"
    git clone -b "$IDF_VERSION" --recursive \
        https://github.com/espressif/esp-idf.git "$IDF_INSTALL_DIR"
fi

echo "==> Installing IDF tools for target: esp32"
cd "$IDF_INSTALL_DIR"
./install.sh esp32

echo "==> Bootstrap complete. Next: scripts/doctor.sh, then scripts/build.sh"
