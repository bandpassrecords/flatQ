#!/usr/bin/env bash
# Builds dist/FlatQ-<version>-macOS.pkg from build/FlatQ_artefacts/Release (run scripts/build.sh first).
# Usage: installers/macos/build-pkg.sh [version]
# Optional signing: set DEVELOPER_ID_INSTALLER="Developer ID Installer: Name (TEAMID)" to productsign the result.
set -euo pipefail
cd "$(dirname "$0")/../.."
VERSION="${1:-0.0.0-dev}"; VERSION="${VERSION#v}"
A=build/FlatQ_artefacts/Release
W=build/pkg; rm -rf "$W"; mkdir -p "$W/root/vst3" "$W/root/au" "$W/root/app" "$W/pkgs" dist

ditto "$A/VST3/FlatQ.vst3"       "$W/root/vst3/FlatQ.vst3"
ditto "$A/AU/FlatQ.component"    "$W/root/au/FlatQ.component"
ditto "$A/Standalone/FlatQ.app"  "$W/root/app/FlatQ.app"
for b in "$W"/root/vst3/FlatQ.vst3 "$W"/root/au/FlatQ.component "$W"/root/app/FlatQ.app; do
  codesign --force --deep -s - "$b"
done

build_component() { # name identifier install-location
  local name=$1 id=$2 loc=$3 plist="$W/$1.plist"
  pkgbuild --analyze --root "$W/root/$name" "$plist" >/dev/null
  python3 - "$plist" <<'PY'     # install exactly where we say, never "update" a copy elsewhere (e.g. the build folder)
import plistlib, sys
p = sys.argv[1]; d = plistlib.load(open(p, "rb"))
for e in d: e["BundleIsRelocatable"] = False; e["BundleIsVersionChecked"] = False; e["BundleOverwriteAction"] = "upgrade"
plistlib.dump(d, open(p, "wb"))
PY
  pkgbuild --root "$W/root/$name" --component-plist "$plist" --identifier "$id" --version "$VERSION" \
           --install-location "$loc" "$W/pkgs/flatq-$name.pkg" >/dev/null
}
build_component vst3 com.bandpassrecords.flatq.vst3 "/Library/Audio/Plug-Ins/VST3"
build_component au   com.bandpassrecords.flatq.au   "/Library/Audio/Plug-Ins/Components"
build_component app  com.bandpassrecords.flatq.app  "/Applications"

sed "s/@VERSION@/$VERSION/g" installers/macos/distribution.xml > "$W/distribution.xml"
OUT="dist/FlatQ-$VERSION-macOS.pkg"
if [ -n "${DEVELOPER_ID_INSTALLER:-}" ]; then
  productbuild --distribution "$W/distribution.xml" --package-path "$W/pkgs" "$W/unsigned.pkg" >/dev/null
  productsign --sign "$DEVELOPER_ID_INSTALLER" "$W/unsigned.pkg" "$OUT"
else
  productbuild --distribution "$W/distribution.xml" --package-path "$W/pkgs" "$OUT" >/dev/null
fi
echo "Built $OUT"; pkgutil --check-signature "$OUT" 2>&1 | head -2 || true
