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

# v1.6.1-rc.29 — PRE-INSTALL CLEANUP scripts. User reported rc.28
# crashed FL Studio Mac on plugin load. Root cause is the same
# class of issue we fixed in rc.24: AU validation cache + plugin
# DB hold the prior binary's identity, then drop-in replace
# serves a half-stale / half-new bundle to the host. The rc.24
# fix bumped the plugin codes once; the rc.29 fix doubles down by
# (a) bumping codes again (Mk2 → Mk3 in CMakeLists.txt) AND (b)
# nuking every prior install path + the system AU cache before
# the new bundle hits disk, so a fresh re-validation is forced
# on first launch instead of a cached "approved" mismatch.
#
# Note: pkgbuild --scripts dir must contain a file named exactly
# "preinstall" (no extension) marked executable. The installer
# runs it as root before the payload is laid down.
VST3_SCRIPTS="$STAGE/vst3_scripts"
AU_SCRIPTS="$STAGE/au_scripts"
mkdir -p "$VST3_SCRIPTS" "$AU_SCRIPTS"

cat > "$VST3_SCRIPTS/preinstall" <<'PREINSTALL_VST3'
#!/bin/sh
# v1.6.1-rc.29 — wipe any prior HumHouse Drums.vst3 (system-wide
# + every per-user Library) so FL Studio's plugin DB doesn't see
# two competing bundles with the same path. Silent-fail on errors:
# we never want a missing path to abort the install.
rm -rf "/Library/Audio/Plug-Ins/VST3/HumHouse Drums.vst3" 2>/dev/null || true
for home in /Users/*; do
  [ -d "$home" ] || continue
  rm -rf "$home/Library/Audio/Plug-Ins/VST3/HumHouse Drums.vst3" 2>/dev/null || true
done
exit 0
PREINSTALL_VST3
chmod +x "$VST3_SCRIPTS/preinstall"

cat > "$AU_SCRIPTS/preinstall" <<'PREINSTALL_AU'
#!/bin/sh
# v1.6.1-rc.29 — wipe prior HumHouse Drums.component + flush the
# Audio Unit validation cache so Logic Pro / GarageBand / FL
# Studio re-validate the new bundle from scratch instead of
# serving a cached "approved" result keyed to the old binary.
# Killing AudioComponentRegistrar is the documented way to make
# auval re-scan on next launch (Apple devforums, JUCE forum).
rm -rf "/Library/Audio/Plug-Ins/Components/HumHouse Drums.component" 2>/dev/null || true
for home in /Users/*; do
  [ -d "$home" ] || continue
  rm -rf "$home/Library/Audio/Plug-Ins/Components/HumHouse Drums.component" 2>/dev/null || true
  rm -rf "$home/Library/Caches/AudioUnitCache" 2>/dev/null || true
done
rm -rf "/Library/Caches/AudioUnitCache" 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
PREINSTALL_AU
chmod +x "$AU_SCRIPTS/preinstall"

pkgbuild \
  --identifier "${BUNDLE_ID}.vst3" \
  --version "$VERSION" \
  --root "$STAGE/vst3_root" \
  --scripts "$VST3_SCRIPTS" \
  --install-location "/" \
  "$PKGDIR/vst3.pkg"

if [[ -d "$AU_ROOT/HumHouse Drums.component" ]]; then
  pkgbuild \
    --identifier "${BUNDLE_ID}.au" \
    --version "$VERSION" \
    --root "$STAGE/au_root" \
    --scripts "$AU_SCRIPTS" \
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
