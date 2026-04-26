#!/usr/bin/env bash
# Builds HumHouse-Drums-macOS.pkg — a proper GUI installer with EULA that
# drops HumHouse Drums.vst3 into /Library/Audio/Plug-Ins/VST3/ and
# HumHouse Drums.component into /Library/Audio/Plug-Ins/Components/.
#
# Runs on macOS. Uses pkgbuild + productbuild (Xcode Command Line Tools).
#
# Requires build/AIDrumVST_artefacts/Release/{VST3,AU} to exist already.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"

VERSION="${VERSION:-0.9.0}"
BUNDLE_ID="com.humhouse.humhousedrums.installer"
PKG_OUT="${PKG_OUT:-HumHouse-Drums-macOS.pkg}"

ART="$REPO/build/AIDrumVST_artefacts/Release"
VST3_SRC="$ART/VST3/HumHouse Drums.vst3"
AU_SRC="$ART/AU/HumHouse Drums.component"

if [[ ! -d "$VST3_SRC" ]]; then
  echo "ERROR: $VST3_SRC not found. Run 'cmake --build build --config Release' first." >&2
  exit 1
fi

STAGE="$(mktemp -d)"
VST3_ROOT="$STAGE/vst3_root/Library/Audio/Plug-Ins/VST3"
AU_ROOT="$STAGE/au_root/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_ROOT" "$AU_ROOT"

cp -R "$VST3_SRC" "$VST3_ROOT/"
if [[ -d "$AU_SRC" ]]; then
  cp -R "$AU_SRC" "$AU_ROOT/"
fi

# Ad-hoc sign the bundles so Gatekeeper doesn't reject them outright.
# If APPLE_DEVELOPER_ID is exported we'd use it; otherwise ad-hoc (-).
SIGN_IDENTITY="${APPLE_DEVELOPER_ID:--}"
codesign --force --deep --sign "$SIGN_IDENTITY" \
  "$VST3_ROOT/HumHouse Drums.vst3" || true
if [[ -d "$AU_ROOT/HumHouse Drums.component" ]]; then
  codesign --force --deep --sign "$SIGN_IDENTITY" \
    "$AU_ROOT/HumHouse Drums.component" || true
fi

# Build two component packages, then wrap them in a productbuild archive
# that shows a Welcome + License wizard.
PKGDIR="$STAGE/pkgs"
mkdir -p "$PKGDIR"

pkgbuild \
  --identifier "${BUNDLE_ID}.vst3" \
  --version "$VERSION" \
  --root "$STAGE/vst3_root" \
  --install-location "/" \
  "$PKGDIR/vst3.pkg"

if [[ -d "$AU_ROOT/HumHouse Drums.component" ]]; then
  pkgbuild \
    --identifier "${BUNDLE_ID}.au" \
    --version "$VERSION" \
    --root "$STAGE/au_root" \
    --install-location "/" \
    "$PKGDIR/au.pkg"
fi

# Distribution XML — defines the installer wizard screens.
DIST="$STAGE/distribution.xml"
cat > "$DIST" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>HumHouse Drums $VERSION</title>
    <welcome    file="welcome.html"    mime-type="text/html"/>
    <license    file="license.txt"     mime-type="text/plain"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options    customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <choices-outline>
        <line choice="vst3"/>
$( [[ -f "$PKGDIR/au.pkg" ]] && echo '        <line choice="au"/>' )
    </choices-outline>
    <choice id="vst3" title="VST3 Plug-in" visible="true" start_selected="true">
        <pkg-ref id="${BUNDLE_ID}.vst3"/>
    </choice>
$( [[ -f "$PKGDIR/au.pkg" ]] && cat <<CH
    <choice id="au" title="Audio Unit (for Logic Pro)" visible="true" start_selected="true">
        <pkg-ref id="${BUNDLE_ID}.au"/>
    </choice>
CH
)
    <pkg-ref id="${BUNDLE_ID}.vst3" version="$VERSION" onConclusion="none">vst3.pkg</pkg-ref>
$( [[ -f "$PKGDIR/au.pkg" ]] && echo "    <pkg-ref id=\"${BUNDLE_ID}.au\" version=\"$VERSION\" onConclusion=\"none\">au.pkg</pkg-ref>" )
</installer-gui-script>
XML

# Wizard resources
RES="$STAGE/resources"
mkdir -p "$RES"
cp "$REPO/installer/LICENSE.txt" "$RES/license.txt"

cat > "$RES/welcome.html" <<'HTML'
<html><body style="font-family: -apple-system, system-ui; padding: 20px;">
<h2 style="margin-top:0;">Welcome to HumHouse Drums</h2>
<p>This installer will place the plug-ins in their standard locations so
every supported DAW can find them automatically:</p>
<ul>
  <li><b>VST3</b> &rarr; /Library/Audio/Plug-Ins/VST3/<br/>
      <small>FL Studio, Ableton, Reaper, Cubase, Studio One, Bitwig&hellip;</small></li>
  <li><b>Audio Unit</b> &rarr; /Library/Audio/Plug-Ins/Components/<br/>
      <small>Logic Pro, GarageBand, MainStage</small></li>
</ul>
<p>After installation, rescan plug-ins in your DAW. HumHouse Drums will
appear as an Instrument.</p>
</body></html>
HTML

cat > "$RES/conclusion.html" <<'HTML'
<html><body style="font-family: -apple-system, system-ui; padding: 20px;">
<h2 style="margin-top:0;">Installation complete.</h2>
<p>Now open your DAW and rescan plug-ins:</p>
<ul>
  <li><b>Logic Pro</b> &rarr; will detect the new AU automatically next
      time you add an instrument track.</li>
  <li><b>FL Studio</b> &rarr; Options &rarr; Manage Plug-ins &rarr;
      Find installed plug-ins.</li>
  <li><b>Ableton</b> &rarr; Preferences &rarr; Plug-Ins &rarr; Rescan.</li>
</ul>
<p>HumHouse Drums will appear under <b>Instruments</b> / <b>Generators</b>.</p>
</body></html>
HTML

UNSIGNED_PKG="$STAGE/HumHouse-Drums-unsigned.pkg"
productbuild \
  --distribution "$DIST" \
  --resources "$RES" \
  --package-path "$PKGDIR" \
  --version "$VERSION" \
  "$UNSIGNED_PKG"

# If a productsign identity is available, sign the flat pkg; else ship unsigned.
if [[ -n "${APPLE_INSTALLER_ID:-}" ]]; then
  productsign --sign "$APPLE_INSTALLER_ID" "$UNSIGNED_PKG" "$PKG_OUT"
else
  cp "$UNSIGNED_PKG" "$PKG_OUT"
fi

echo "Built: $PKG_OUT"
ls -la "$PKG_OUT"
