#!/usr/bin/env bash
# Package the macOS build into a drag-to-install .dmg containing:
#   * HumHouse Drums X.vst3   (Ableton, FL Studio, Reaper, Cubase, Studio One, …)
#   * HumHouse Drums X.component (Logic Pro, GarageBand)
#   * Symlinks to the system plug-in folders so the user can just drag & drop
#
# Must run on macOS (uses `hdiutil`, `codesign`).
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
ARTEFACTS="$BUILD_DIR/AIDrumVST_artefacts/Release"
VST3_SRC="$ARTEFACTS/VST3/HumHouse Drums X.vst3"
AU_SRC="$ARTEFACTS/AU/HumHouse Drums X.component"
APP_SRC="$ARTEFACTS/Standalone/HumHouse Drums X.app"
STAGING="$(mktemp -d)/HumHouse Drums X"
DMG_OUT="${DMG_OUT:-HumHouse-Drums-X-macOS.dmg}"

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
if [[ -d "$APP_SRC" ]]; then
  cp -R "$APP_SRC" "$STAGING/"
fi

# Drag-to-install targets.
ln -s "/Library/Audio/Plug-Ins/VST3"      "$STAGING/VST3 Plug-Ins"
ln -s "/Library/Audio/Plug-Ins/Components" "$STAGING/Audio Unit Plug-Ins"

# Readme inside the dmg.
cat > "$STAGING/README.txt" <<'EOF'
HumHouse Drums X
==============

Three ways to use it:

A) Standalone app (no DAW scanning needed):
   Double-click "HumHouse Drums X.app" — it runs by itself. Use the
   "Drag MIDI to DAW" handle or "Save MIDI..." button to pull
   patterns into FL Studio / Logic / Ableton / anything.

B) VST3 plugin (Ableton Live, FL Studio, Reaper, Cubase, Studio One, …):
   Drag "HumHouse Drums X.vst3" into "VST3 Plug-Ins" then rescan in your DAW.

C) AU plugin (Logic Pro, GarageBand — Logic does not accept VST3):
   Drag "HumHouse Drums X.component" into "Audio Unit Plug-Ins" then
   relaunch Logic.

First launch (unsigned build):
  macOS Gatekeeper may block the plugin the first time your DAW loads it.
  If that happens, run this once in Terminal:

      xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/HumHouse Drums X.vst3"
      xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/HumHouse Drums X.component"

  Then relaunch your DAW.
EOF

# Optional ad-hoc codesign (suppresses the noisiest Gatekeeper prompts).
# A real Developer ID + notarization still requires $APPLE_DEVELOPER_ID.
if [[ -n "${APPLE_DEVELOPER_ID:-}" ]]; then
  echo "Signing with Developer ID: $APPLE_DEVELOPER_ID"
  codesign --force --deep --options runtime --timestamp \
           --sign "$APPLE_DEVELOPER_ID" "$STAGING/HumHouse Drums X.vst3"
  codesign --force --deep --options runtime --timestamp \
           --sign "$APPLE_DEVELOPER_ID" "$STAGING/HumHouse Drums X.component"
else
  echo "No APPLE_DEVELOPER_ID set; ad-hoc signing (users may see Gatekeeper prompts)."
  codesign --force --deep --sign - "$STAGING/HumHouse Drums X.vst3"   || true
  codesign --force --deep --sign - "$STAGING/HumHouse Drums X.component" || true
  [[ -d "$STAGING/HumHouse Drums X.app" ]] && codesign --force --deep --sign - "$STAGING/HumHouse Drums X.app" || true
fi

rm -f "$DMG_OUT"
# v1.6.1-rc.12 — `hdiutil create` flakes with "Resource busy" on macOS
# CI runners every few builds (the runner's diskimages-helper is mid-
# cleanup of a previous job). Retry up to 5 times with a 10s backoff
# before giving up so a flaky helper doesn't tank the release build.
attempt=1
max_attempts=5
while (( attempt <= max_attempts )); do
  if hdiutil create \
        -volname "HumHouse Drums X" \
        -srcfolder "$STAGING" \
        -fs HFS+ \
        -format UDZO \
        -imagekey zlib-level=9 \
        "$DMG_OUT"; then
    echo "Created: $DMG_OUT (attempt $attempt)"
    exit 0
  fi
  echo "hdiutil create failed (attempt $attempt/$max_attempts); retrying after 10s..."
  rm -f "$DMG_OUT"
  sleep 10
  attempt=$(( attempt + 1 ))
done

echo "error: hdiutil create failed after $max_attempts attempts" >&2
exit 1
