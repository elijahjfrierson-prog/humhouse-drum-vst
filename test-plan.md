# HumHouse Drums v0.6.0 — Rename + Labels Visual Verification

## What changed
- Project renamed "AI Drum VST" → **HumHouse Drums** (title, binary, CMake, scripts).
- All controls now have explicit labels + hover tooltips (user request: "so i know which button does what").
- v0.6.0 features: arrangement strip grid under the + button, APPEND/UNDO/CLEAR buttons, Swing/Fills knobs, Hi-Hat combo, Half-Time toggle.

## Primary flow (visual)
1. Launch `build/AIDrumVST_artefacts/Release/Standalone/HumHouse Drums`.
2. Screenshot full window at rest.
3. Click **+** twice; screenshot to show arrangement grid growing with region dividers.
4. Hover over the **COMPLEXITY** knob to reveal tooltip; screenshot.

## Key assertions (pass/fail)
- Window title bar reads **"HumHouse Drums"** (not "AI Drum VST"). FAIL if old name appears.
- Title label inside app reads **"HUMHOUSE  DRUMS"** in tracked gothic serif. FAIL if old label.
- Below the title, 6 knobs are visible with labels: **VARIATION, COMPLEXITY, VELOCITY, HUMANIZE, SWING, FILLS**. FAIL if any label missing.
- Below knobs, 4 combos + 1 toggle visible: **GENRE, LENGTH, MODE, HI-HAT, HALF-TIME**. FAIL if any missing.
- Three action buttons around +: **UNDO, CLEAR, +, DRAG MIDI, SAVE MIDI**. FAIL if any missing.
- Arrangement strip (piano-roll grid) visible at bottom of window, spanning full width. FAIL if absent.
- After clicking + twice: arrangement strip shows **at least 2 region dividers** and header text indicates region count ≥ 2 (e.g. "Arrangement · 2 regions"). FAIL if only 1 region or grid unchanged.
- Hover over COMPLEXITY knob: tooltip appears reading "COMPLEXITY — pattern density..." (text starting with "COMPLEXITY"). FAIL if no tooltip after ~1s hover.
