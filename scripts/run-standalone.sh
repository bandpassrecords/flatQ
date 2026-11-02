#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
A=build/FlatQ_artefacts; [ -d "$A/Release" ] && A="$A/Release"
open "$A/Standalone/FlatQ.app"
