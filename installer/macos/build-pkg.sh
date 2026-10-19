#!/usr/bin/env bash
# Builds a macOS installer package with VST3, AU and CLAP.
#   installer/macos/build-pkg.sh <version> <artefact folder> <output.pkg>
# Optional environment:
#   MACOS_SIGN_INSTALLER   "Developer ID Installer: ..." identity, signs the package
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD   notarise and staple the signed package
set -euo pipefail
trap 'echo "build-pkg.sh failed at line $LINENO: $BASH_COMMAND" >&2' ERR

VERSION="$1"
ART="$2"
OUT="$3"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

component() {   # name bundle install-location
    local name="$1" bundle="$2" location="$3"
    local root="$WORK/root-$name"
    mkdir -p "$root"
    cp -R "$ART/$bundle" "$root/"
    # keep the bundle where it is installed, even if another copy exists elsewhere
    pkgbuild --analyze --root "$root" "$WORK/$name.plist"
    # (only bundles pkgbuild recognises are listed; nothing to change otherwise)
    /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$WORK/$name.plist" 2>/dev/null || true
    pkgbuild --root "$root" --component-plist "$WORK/$name.plist" \
             --identifier "com.klingeaudio.klinge.$name" --version "$VERSION" \
             --install-location "$location" "$WORK/klinge-$name.pkg"
}

component vst3 VST3/KLINGE.vst3      "/Library/Audio/Plug-Ins/VST3"
component au   AU/KLINGE.component   "/Library/Audio/Plug-Ins/Components"
component clap CLAP/KLINGE.clap      "/Library/Audio/Plug-Ins/CLAP"

productbuild --synthesize \
             --package "$WORK/klinge-vst3.pkg" --package "$WORK/klinge-au.pkg" --package "$WORK/klinge-clap.pkg" \
             "$WORK/distribution.xml"
# title and a format choice for each plugin type
sed -i '' 's|<installer-gui-script minSpecVersion="[0-9]*">|&\
    <title>KLINGE '"$VERSION"'</title>|' "$WORK/distribution.xml"
sed -i '' 's|<options customize="never"|<options customize="allow"|' "$WORK/distribution.xml" || true

productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$WORK/unsigned.pkg"

if [ -n "${MACOS_SIGN_INSTALLER:-}" ]; then
    productsign --sign "$MACOS_SIGN_INSTALLER" "$WORK/unsigned.pkg" "$OUT"
    if [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ] && [ -n "${APPLE_APP_PASSWORD:-}" ]; then
        xcrun notarytool submit "$OUT" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
                                       --password "$APPLE_APP_PASSWORD" --wait
        xcrun stapler staple "$OUT"
    fi
else
    cp "$WORK/unsigned.pkg" "$OUT"
    echo "note: package is not signed (set MACOS_SIGN_INSTALLER to sign it)"
fi
echo "wrote $OUT"
