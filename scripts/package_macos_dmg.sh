#!/usr/bin/env bash
# Package the macOS build into a drag-to-install .dmg containing:
#   * AI Drum VST.vst3   (Ableton, FL Studio, Reaper, Cubase, Studio One, …)
#   * AI Drum VST.component (Logic Pro, GarageBand)
#   * Symlinks to the system plug-in folders so the user can just drag & drop
#
# Must run on macOS (uses `hdiutil`, `codesign`).
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
ARTEFACTS="$BUILD_DIR/AIDrumVST_artefacts/Release"
VST3_SRC="$ARTEFACTS/VST3/AI Drum VST.vst3"
AU_SRC="$ARTEFACTS/AU/AI Drum VST.component"
STAGING="$(mktemp -d)/AI Drum VST"
DMG_OUT="${DMG_OUT:-AI-Drum-VST-macOS.dmg}"

if [[ ! -d "$VST3_SRC" ]]; then
  echo "error: VST3 bundle not found at $VST3_SRC" >&2
  exit 1
fi
if [[ ! -d "$AU_SRC" ]]; then
  echo "error: AU component not found at $AU_SRC" >&2
  exit 1
fi

mkdir -p "$STAGING"
cp -R "$VST3_SRC" "$STAGING/"
cp -R "$AU_SRC"   "$STAGING/"

# Drag-to-install targets.
ln -s "/Library/Audio/Plug-Ins/VST3"      "$STAGING/VST3 Plug-Ins"
ln -s "/Library/Audio/Plug-Ins/Components" "$STAGING/Audio Unit Plug-Ins"

# Readme inside the dmg.
cat > "$STAGING/README.txt" <<'EOF'
AI Drum VST
===========

To install:
  1. Drag "AI Drum VST.vst3"       into "VST3 Plug-Ins"
     (loads in Ableton Live, FL Studio, Reaper, Cubase, Studio One, …)
  2. Drag "AI Drum VST.component"  into "Audio Unit Plug-Ins"
     (loads in Logic Pro and GarageBand — Logic does not accept VST3.)
  3. Relaunch your DAW and rescan plugins.

First launch (unsigned build):
  macOS Gatekeeper may block the plugin the first time your DAW loads it.
  If that happens, run this once in Terminal:

      xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/AI Drum VST.vst3"
      xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/AI Drum VST.component"

  Then relaunch your DAW.
EOF

# Optional ad-hoc codesign (suppresses the noisiest Gatekeeper prompts).
# A real Developer ID + notarization still requires $APPLE_DEVELOPER_ID.
if [[ -n "${APPLE_DEVELOPER_ID:-}" ]]; then
  echo "Signing with Developer ID: $APPLE_DEVELOPER_ID"
  codesign --force --deep --options runtime --timestamp \
           --sign "$APPLE_DEVELOPER_ID" "$STAGING/AI Drum VST.vst3"
  codesign --force --deep --options runtime --timestamp \
           --sign "$APPLE_DEVELOPER_ID" "$STAGING/AI Drum VST.component"
else
  echo "No APPLE_DEVELOPER_ID set; ad-hoc signing (users may see Gatekeeper prompts)."
  codesign --force --deep --sign - "$STAGING/AI Drum VST.vst3"   || true
  codesign --force --deep --sign - "$STAGING/AI Drum VST.component" || true
fi

rm -f "$DMG_OUT"
hdiutil create \
  -volname "AI Drum VST" \
  -srcfolder "$STAGING" \
  -fs HFS+ \
  -format UDZO \
  -imagekey zlib-level=9 \
  "$DMG_OUT"

echo "Created: $DMG_OUT"
