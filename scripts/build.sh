#!/usr/bin/env bash
# Local build. Usage: scripts/build.sh [Release|Debug] [target ...]
# Targets default to: FreeQ_VST3 FreeQ_AU FreeQ_Standalone (AU only on macOS).
set -euo pipefail
cd "$(dirname "$0")/.."
CONFIG="${1:-Release}"; shift || true
GEN=(); command -v ninja >/dev/null && GEN=(-G Ninja)
cmake -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE="$CONFIG" \
  ${FREEQ_UNIVERSAL:+-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"}
if [ "$#" -gt 0 ]; then TARGETS=("$@"); else
  TARGETS=(FreeQ_VST3 FreeQ_Standalone); [ "$(uname)" = Darwin ] && TARGETS+=(FreeQ_AU)
fi
for t in "${TARGETS[@]}"; do cmake --build build --config "$CONFIG" --target "$t" --parallel; done
cmake --build build --config "$CONFIG" --target lvls_dump design_test --parallel
ctest --test-dir build -C "$CONFIG" --output-on-failure
echo; echo "Artefacts:"; find build/FreeQ_artefacts -maxdepth 3 \( -name '*.vst3' -o -name '*.component' -o -name '*.app' \) 2>/dev/null
