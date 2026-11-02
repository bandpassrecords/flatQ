#!/usr/bin/env bash
# Ad-hoc signs and installs the built plugins for the current user (no Apple Developer account needed).
set -euo pipefail
cd "$(dirname "$0")/.."
A=build/FlatQ_artefacts; [ -d "$A/Release" ] && A="$A/Release"
install_one() { # src destdir
  [ -e "$1" ] || { echo "missing $1 (run scripts/build.sh first)"; return; }
  mkdir -p "$2"; rm -rf "$2/$(basename "$1")"; cp -R "$1" "$2/"
  codesign --force --deep -s - "$2/$(basename "$1")"; echo "installed $2/$(basename "$1")"
}
install_one "$A/VST3/FlatQ.vst3"       "$HOME/Library/Audio/Plug-Ins/VST3"
install_one "$A/AU/FlatQ.component"    "$HOME/Library/Audio/Plug-Ins/Components"
killall -9 AudioComponentRegistrar 2>/dev/null || true   # make hosts rescan AUs
