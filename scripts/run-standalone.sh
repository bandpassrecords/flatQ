#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
A=build/FreeQ_artefacts; [ -d "$A/Release" ] && A="$A/Release"
open "$A/Standalone/FreeQ.app"
