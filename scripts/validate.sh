#!/usr/bin/env bash
# Validates the AU with Apple's auval (macOS only). Install first: scripts/install-macos.sh
set -euo pipefail
auval -v aufx FqPl FrEQ
