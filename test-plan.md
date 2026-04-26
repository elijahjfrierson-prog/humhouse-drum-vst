# v1.6.0 Test Plan — PR #2

## What changed (user-visible)
- **STARTER GROOVES** dropdown populated with 119 hand-played templates; selecting one appends a new region.
- **COPY / PASTE** buttons duplicate the last arrangement region.
- **MANUAL** mode is now a 5-row step grid (CRASH / HI-HAT / TOM / SNARE / KICK) × 16 bars.
- **COMPLEXITY** knob deterministically drives pattern density (ghost notes, 1/8→1/16 hats, crashes).
- **VELOCITY**: true 0-100 range (velFloor=0.27, velCeil=1.05).
- Cymbal buses default to −6 dB; toms −1.5 dB.

## Primary flow — prove the new controls actually mutate state
Target env: local JUCE Standalone (`build/AIDrumVST_artefacts/Release/Standalone/HumHouse Drums`).

### Test 1 — STARTER GROOVE dropdown loads 119 entries and appends a region
- Steps: click the STARTER combobox (labelled "STARTER GROOVE ..."); scroll the dropdown.
- **Pass**: dropdown menu shows > 100 items, including at least one recognizable name like `Bar Band Basic Drumset 01` or `Funked Out Drumset 02`.
- **Fail**: only the placeholder item is visible, or dropdown is empty.
- Then click any groove, e.g. `Bar Band Basic Drumset 01`.
- **Pass**: the ARRANGEMENT strip below, previously empty, now shows a filled region block AND the STARTER combobox snaps back to the "STARTER GROOVE ..." placeholder.
- **Fail**: arrangement still empty, or the combobox stays on the selected name.
- *Would this look the same if broken?* No — a broken `appendStarterGroove` leaves the strip empty; a broken populate shows 0 items.
  - Evidence source: <ref_snippet file="/home/ubuntu/ai-drum-vst/Source/PluginEditor.cpp" lines="687-704" />

### Test 2 — COPY / PASTE duplicates the last region
- Pre-state: from Test 1, 1 region exists.
- Steps: click **COPY**, then click **PASTE**.
- **Pass**: a second visually identical region appears after the first in the arrangement strip (region count becomes 2).
- **Fail**: still 1 region after PASTE, or PASTE does nothing before COPY.
- Steps: click **PASTE** again.
- **Pass**: region count becomes 3.
- *Would this look the same if broken?* No — a no-op PASTE or broken clipboard leaves count at 1.
  - Evidence: <ref_snippet file="/home/ubuntu/ai-drum-vst/Source/PluginProcessor.cpp" lines="485-513" />, <ref_snippet file="/home/ubuntu/ai-drum-vst/Source/PluginEditor.cpp" lines="710-731" />

### Test 3 — Manual Mode shows exactly 5 rows (not 11) × 16 bars, and ADD TO ARRANGEMENT appends
- Steps: click **MANUAL** toggle.
- **Pass**: the arrangement strip area is replaced by a grid showing exactly 5 row labels top→bottom: `CRASH`, `HI-HAT`, `TOM`, `SNARE`, `KICK`.
- **Fail**: 11 rows visible, or different labels.
- Steps: click 4 cells in the KICK row (on beats 1, 5, 9, 13 of bar 1) and 2 cells in the SNARE row (beats 5 and 13 of bar 1). Then click the **APPEND TO ARR.** button (commitManualButton, shown in lower-left when MANUAL is on).
- **Pass**: a new region is appended to the arrangement (visible once MANUAL is toggled back off).
- **Fail**: no new region appears when MANUAL is toggled off.
- *Would this look the same if broken?* No — a broken 5-row change would still render 11 rows; a broken commit would not grow the arrangement.
  - Evidence: <ref_snippet file="/home/ubuntu/ai-drum-vst/Source/ManualGrid.h" lines="30-48" />, <ref_snippet file="/home/ubuntu/ai-drum-vst/Source/PluginEditor.cpp" lines="733-765" />

### Test 4 — COMPLEXITY & VELOCITY knobs mutate audible pattern density + dynamics
The VM has no real audio sink (ALSA not available), so audible playback can't be validated. Instead, validate via MIDI export:
- Steps: reset arrangement (click `CLEAR`), set COMPLEXITY knob to 0.0 (fully counterclockwise), VELOCITY knob to 0.0, click **APPEND** to generate a region, then click **SAVE MIDI** to dump a .mid file to disk. Inspect note count + velocity range via `mido` or `python -c` dump.
- Then set COMPLEXITY to 1.0, VELOCITY to 1.0, APPEND another region, SAVE MIDI again.
- **Pass**: high-complexity MIDI has **at least 2× the note count** of low-complexity MIDI, AND velocity max (high) ≥ 120 while velocity max (low) ≤ 60.
- **Fail**: note counts within 10% of each other, or velocity ranges overlap entirely.
- *Would this look the same if broken?* No — a knob wired to nothing would produce identical MIDI at both extremes.
  - Evidence: VELOCITY curve (velFloor=0.27, velCeil=1.05) + COMPLEXITY → ghostProb/hatEighthGate/hatSixteenthGate in pattern generator.

## Not covered (explicit)
- Live audio playback (VM has no ALSA — MIDI-export path is the adversarial substitute).
- Per-kit kick/snare differences (too subtle to evidence without audio).
- X-Y pad drive-vs-laid-back axis (deferred in this PR per user).

## Exit criteria
Tests 1–3 pass via UI screenshots; Test 4 passes via MIDI note-count + velocity inspection. Any single failure is a blocker to merge.
