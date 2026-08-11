# Testing HumHouse Vocals Plugin

## Overview
HumHouse Vocals is a JUCE-based audio effect plugin (VST3/AU/Standalone) with AutoTune pitch correction, 12-module DSP chain, preset system, and UI scale control.

## Building
```bash
cd humhouse-vocals
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j$(nproc)
```
JUCE 8.0.4 is fetched automatically via CMake FetchContent — no manual clone needed.

## Launching the Standalone
```bash
DISPLAY=:0 "humhouse-vocals/build/HumHouseVocals_artefacts/Release/Standalone/HumHouse Vocals" &
```
- ALSA warnings (`open /dev/snd/seq failed`) are expected on VMs without audio hardware — they are non-fatal.
- The standalone shows "Audio input is muted to avoid feedback loop" banner — this is normal JUCE standalone behavior.
- Use `xdotool search --name "HumHouse"` then `xdotool windowactivate <id>` to bring the window to focus if it's behind other windows.
- `wmctrl` may not list JUCE windows; use `xdotool` instead.

## UI Layout
- **Top bar**: "HUMHOUSE VOCALS" title, pitch display, preset dropdown (ComboBox), Save/Del buttons, UI Scale controls
- **Scale controls**: Root note dropdown (C-B) + Scale dropdown (Major/Minor/Chromatic)
- **Row 1 modules**: PITCH, EQ, COMP, DE-ESS, SATURATE, TAPE
- **Row 2 modules**: WIDTH, DOUBLER, REVERB, DELAY, LO-FI, LIMITER
- **Bottom**: INPUT, OUTPUT, DRY/WET knobs
- **UI Scale**: Text box showing current scale (e.g. "1.0") with [-] and [+] buttons

## Key Test Scenarios

### Preset Loading
1. Open preset dropdown → select different presets (e.g. "Trap Hard AutoTune", "Nu Rock Dry Scream")
2. Verify knob positions change, module active states change, mode dropdowns change (THD, Saturation type, etc.)
3. Select "Default (Init)" to verify reset to baseline

### Save/Delete User Preset
1. Modify some parameters (e.g. activate an inactive module)
2. Click "Save" → dialog appears with text input → type name → click Save
3. Verify preset appears in dropdown
4. Switch to different preset, then back to saved preset → verify state restored
5. Click "Del" → verify preset removed from dropdown

### UI Scale
1. Click [+] button → scale increases by 0.1, window grows
2. Click [-] button → scale decreases by 0.1, window shrinks
3. Range: 0.5 to 2.0
4. All controls and labels scale proportionally

### Module Toggles
1. Click on a module header (e.g. "REVERB") to toggle it on/off
2. Active modules have bright purple headers; inactive modules have dimmed headers

## Factory Presets (47 total)
Categories: Trap, R&B, Pop, Rock, Lo-Fi, Creative, Genre-Specific, Signature (Drocett, Nu Rock), Mix-Ready

Signature presets to verify:
- Drocett Smooth Melodies, Drocett Trap Soul, Drocett Late Night, Drocett Falsetto Vibe
- Nu Rock Dry Scream, Nu Rock Raw Edge, Nu Rock Grit & Growl

## Limitations on Headless VMs
- **Audio processing cannot be tested** without an audio device. Pitch correction, DSP effects, and the pitch heatmap visualizer require live audio input.
- **Pitch heatmap stays static** without audio — this is expected, not a bug.
- To fully test audio: load the VST3/AU in a DAW (FL Studio, Logic, Ableton) with vocal audio on the channel.

## User Presets Location
User presets are saved as XML files to: `~/.HumHouse/HumHouse Vocals/Presets/`

## Devin Secrets Needed
None — the standalone runs without any secrets or authentication.
