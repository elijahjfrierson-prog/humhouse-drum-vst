# HumHouse Vocals

All-in-one vocal processing plugin built with [JUCE](https://juce.com).
Insert it on any vocal channel strip for studio-grade pitch correction,
dynamics, saturation, spatial effects, and limiting — all with **zero
latency**.

Inspired by the Gamma Vocal Suite, Slate Digital MetaTune, iZotope
Nectar 4 Advanced, and Antares Auto-Tune.

Ships as:

- **VST3 plugin** — FL Studio, Ableton Live, Reaper, Cubase, Studio One, Bitwig
- **AU plugin** (macOS only) — Logic Pro, GarageBand
- **Standalone app** — runs without a DAW

## Signal Chain (12 Modules)

| # | Module | Description |
|---|--------|-------------|
| 1 | **Pitch Correction** | YIN pitch detection + PSOLA resynthesis. Scale snap, detune (400–500 Hz), retune speed, humanize, snap, sustain, note stabilizer. MetaTune-style "negative speed" for robotic tuning. |
| 2 | **4-Band EQ** | Analog-modeled parametric EQ with high-pass and low-pass filters. |
| 3 | **Compressor** | Threshold, ratio, attack, release, knee, makeup. THD modes (soft/hard), auto-gain, auto-level. |
| 4 | **De-Esser** | Band-pass sibilance detector (5–9 kHz) with dynamic attenuation. |
| 5 | **Saturation** | Three modes: Tube (even harmonics), Tape (asymmetric), Transformer (odd harmonics). |
| 6 | **Tape Emulation** | Wow/flutter, head bump (60–100 Hz boost), high-end roll-off, IPS control (15/30). |
| 7 | **Stereo Width** | Three modes: Mid/Side, Haas-effect micro-delay, frequency-dependent spread. |
| 8 | **Doubler** | Dual-voice detuned delay doubler with independent pan for natural thickness. |
| 9 | **Reverb** | Dual reverb (short plate + long hall) with ducking and post-reverb EQ. |
| 10 | **Delay** | Tempo-syncable with feedback, ducking, filtering, and ping-pong mode. |
| 11 | **Lo-Fi Cutoff** | Band-pass radio/telephone effect with optional bit-crush and downsampling. |
| 12 | **Output Limiter** | Transparent brick-wall limiter with look-ahead for clean output. |

## Features

- **Zero Latency** — entire chain processes in-place with no look-ahead buffering
- **Pitch HeatMap** — real-time visualizer showing detected vs. corrected pitch
- **Scale Selector** — 12 root notes × Major/Minor/Chromatic
- **Modular On/Off** — each module can be individually bypassed
- **Full Automation** — every parameter exposed via APVTS for DAW automation
- **Dry/Wet Mix** — global wet/dry control for parallel processing
- **Gold UI** — HumHouse signature sunburst gold aesthetic

## Downloadable Binaries

Binaries are produced by the GitHub Actions workflow on every push and release:

| Platform | Formats | Asset |
|----------|---------|-------|
| macOS | Universal `.vst3` + `.component` (AU) + `.app`, guided `.pkg` installer with EULA | `HumHouse-Vocals-macOS.dmg` / `.pkg` |
| Windows | `.vst3` + Standalone `.exe`, Inno Setup installer with EULA | `HumHouse-Vocals-Windows-x64.zip` |
| Linux | `.vst3` + Standalone | `HumHouse-Vocals-Linux-x86_64.zip` |

### Windows Installer

The Inno Setup installer (`installer/humhouse-vocals.iss`) presents a
EULA agreement and installs:
- VST3 to `C:\Program Files\Common Files\VST3\` (detected by all DAWs)
- Standalone to `C:\Program Files\HumHouse\HumHouse Vocals\`

### macOS Installer

The `.pkg` installer presents a welcome screen and EULA, then installs:
- VST3 to `/Library/Audio/Plug-Ins/VST3/`
- AU to `/Library/Audio/Plug-Ins/Components/`
- Standalone to `/Applications/`

## Layout

```
.
├── CMakeLists.txt                  # Top-level CMake; fetches JUCE via FetchContent
├── Source/
│   ├── PluginProcessor.{h,cpp}     # APVTS, signal chain routing, zero-latency engine
│   ├── PluginEditor.{h,cpp}        # GUI: module strips, pitch heatmap, gold knobs
│   ├── HumHouseLookAndFeel.h       # Gold sunburst visual theme
│   ├── PitchEngine.h               # YIN + PSOLA pitch correction
│   ├── VocalEQ.h                   # 4-band parametric EQ
│   ├── VocalCompressor.h           # Dynamics with THD & auto-level
│   ├── DeEsser.h                   # Sibilance reducer
│   ├── SaturationEngine.h          # Tube/Tape/Transformer saturation
│   ├── TapeEmulation.h             # Analog tape emulation
│   ├── StereoWidth.h               # M/S, Haas, freq spread
│   ├── VocalDoubler.h              # Detuned delay doubler
│   ├── VocalReverb.h               # Dual reverb with ducking
│   ├── VocalDelay.h                # Delay with ducking & ping-pong
│   ├── LoFiFilter.h                # Lo-fi signal cutoff
│   └── OutputLimiter.h             # Brick-wall output limiter
├── installer/
│   ├── humhouse-vocals.iss         # Inno Setup script (Windows)
│   └── eula.txt                    # EULA agreement
├── scripts/
│   ├── package_macos_dmg.sh        # macOS DMG packager
│   └── package_macos_pkg.sh        # macOS .pkg installer with EULA
└── .github/workflows/build.yml     # CI/CD for macOS, Windows, Linux
```

## Build

### Prerequisites

- CMake >= 3.22
- A C++20 compiler (clang, MSVC 2022, or recent GCC)
- Linux: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev`
- macOS: Xcode command-line tools
- Windows: Visual Studio 2022

### Configure & Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

Artifacts land under `build/HumHouseVocals_artefacts/Release/`:
- `VST3/HumHouse Vocals.vst3`
- `Standalone/HumHouse Vocals` (or `.exe` on Windows)

### Standard VST3 Install Locations

| OS | Path |
|----|------|
| macOS | `/Library/Audio/Plug-Ins/VST3/` |
| Windows | `C:\Program Files\Common Files\VST3\` |
| Linux | `~/.vst3/` |

## Gatekeeper Note (macOS)

Builds are ad-hoc signed by default. If macOS blocks the plugin:

```bash
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/HumHouse Vocals.vst3"
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/HumHouse Vocals.component"
```

## License

Copyright (c) 2024-2026 HumHouse. All Rights Reserved.
See `installer/eula.txt` for the End-User License Agreement.
