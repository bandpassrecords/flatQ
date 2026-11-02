#!/usr/bin/env bash
# Installs the build tools needed locally (Xcode CLT + CMake + Ninja). JUCE is fetched by CMake.
set -euo pipefail
xcode-select -p >/dev/null 2>&1 || { echo "Install Xcode command line tools: xcode-select --install"; exit 1; }
command -v brew >/dev/null || { echo "Homebrew required: https://brew.sh"; exit 1; }
for pkg in cmake ninja; do command -v "$pkg" >/dev/null || brew install "$pkg"; done
cmake --version | head -1
