# AI Drum VST

Scaffold for an AI-powered drum VST3 plugin built with [JUCE](https://juce.com).
This initial scaffold ships:

- A JUCE-based VST3/Standalone plugin target (CMake-driven, fetches JUCE 8 automatically).
- A minimal UI with a **Generate** button, **Variation** and **Density** knobs, and a **Groove / Fill** mode selector.
- A stub `AIBackend` (C++) and mirrored `ai_drum_backend.py` Python module that return deterministic drum patterns — ready to be swapped for a real model (Magenta MusicRNN, a diffusion drum model, etc.).
- An internal MIDI sequencer that loops the generated pattern, syncs to the host's BPM and PPQ position, and emits notes on MIDI channel 10 using the General MIDI drum map.

## Downloadable plugin binaries

Binaries are produced by the GitHub Actions workflow
(`.github/workflows/build.yml`) on every push and on every GitHub Release:

| Platform | Formats                     | Release asset                        |
|----------|-----------------------------|--------------------------------------|
| macOS    | Universal `.vst3` + `.component` (AU), drag-to-install | `AI-Drum-VST-macOS.dmg`        |
| Windows  | `.vst3`                     | `AI-Drum-VST-Windows-x64.zip`        |
| Linux    | `.vst3`                     | `AI-Drum-VST-Linux-x86_64.zip`       |

Cutting a GitHub Release (`git tag v0.1.0 && git push --tags` → *Create release from tag*)
runs the workflow and uploads all three assets — a real drag-to-install
macOS `.dmg`, a Windows `.vst3` zip, and a Linux `.vst3` zip — so anyone
can download them from the release page without a GitHub login.

### macOS `.dmg` contents

`scripts/package_macos_dmg.sh` produces a DMG that mounts to a volume
named **AI Drum VST** with:

- `AI Drum VST.vst3` — universal binary (arm64 + x86_64), loads in
  FL Studio, Ableton Live, Reaper, Cubase, Studio One, Bitwig, etc.
- `AI Drum VST.component` — Audio Unit, **required for Logic Pro and
  GarageBand** (Logic does not load VST3).
- `VST3 Plug-Ins` → symlink to `/Library/Audio/Plug-Ins/VST3`
- `Audio Unit Plug-Ins` → symlink to `/Library/Audio/Plug-Ins/Components`
- `README.txt` with install instructions.

Users drag the two bundles onto the matching symlinks, relaunch their DAW,
rescan plugins, done.

### Gatekeeper note

Builds are ad-hoc signed by default. The first time a DAW loads the
plugin, macOS Gatekeeper may block it. Fix:

```bash
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/AI Drum VST.vst3"
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/AI Drum VST.component"
```

For a fully trusted install, add an `APPLE_DEVELOPER_ID` repo secret (e.g.
`"Developer ID Application: Your Name (TEAMID)"`). The packaging script
already picks it up. Notarization via `notarytool` is a natural next
step — see the TODO comment in `scripts/package_macos_dmg.sh`.

### About AAX (Pro Tools)

AAX is intentionally **not** built. Producing a loadable AAX plugin requires:

1. Signing the Avid Developer NDA and downloading the AAX SDK.
2. PACE iLok code-signing each release of the plugin.

Once you have AAX SDK access, enable it in `CMakeLists.txt` by adding `AAX`
to `AIDRUM_FORMATS` and point JUCE at the SDK via `juce_set_aax_sdk_path(...)`.

## Layout

```
.
├── CMakeLists.txt             # Top-level CMake; fetches JUCE via FetchContent
├── Source/
│   ├── PluginProcessor.{h,cpp}  # AudioProcessor: sequencer + host sync + APVTS
│   ├── PluginEditor.{h,cpp}     # GUI: Generate button, knobs, mode combo
│   ├── AIBackend.{h,cpp}        # C++ stub AI backend (canned patterns)
│   └── MidiPattern.h            # Shared pattern/note data types
├── python_backend/
│   └── ai_drum_backend.py       # Python stub mirror; target for pybind11 bridge
└── resources/                  # Placeholder for future samples / model assets
```

## Build

### Prerequisites

- CMake >= 3.22
- A C++20 compiler (clang, MSVC 2022, or recent GCC)
- Linux: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev libcurl4-openssl-dev libwebkit2gtk-4.1-dev`
- macOS: Xcode command-line tools
- Windows: Visual Studio 2022

### Configure & build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

CMake fetches JUCE 8 the first time you configure (pin the version via
`-DJUCE_VERSION=8.0.4`). Artifacts land under `build/AIDrumVST_artefacts/Release/`:

- `VST3/AI Drum VST.vst3` — drop into your DAW's VST3 path.
- `Standalone/AI Drum VST` — runs as a standalone app for quick testing.

Standard VST3 install locations:

| OS       | Path                                                |
|----------|-----------------------------------------------------|
| macOS    | `~/Library/Audio/Plug-Ins/VST3`                     |
| Windows  | `C:\Program Files\Common Files\VST3`                |
| Linux    | `~/.vst3`                                           |

## Using the plugin

1. Load **AI Drum VST** as an instrument track in your DAW.
2. Route the plugin's MIDI output to a drum sampler (Battery, Addictive Drums, SitalaFree, etc.) or use a follow-up instrument channel. The stub emits General MIDI drums on channel 10.
3. Pick **Groove** or **Fill**, adjust **Variation** and **Density**, hit **Generate**. The pattern starts looping immediately and re-syncs to the host's PPQ on play.

## Roadmap

The stub backend is intentionally simple. Next steps:

1. **Replace the C++ stub** in `Source/AIBackend.cpp` with a call into `python_backend/ai_drum_backend.py` via [pybind11](https://github.com/pybind/pybind11). The Python module already returns a JSON-shaped dict that maps 1:1 to `aidrum::MidiPattern`.
2. **Train / wire a real model**: Magenta's `DrumRNN` or `GrooVAE` over the [Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) for MIDI generation; a diffusion model for raw drum-sample synthesis (cf. Emergent Drums 2).
3. **Contextual generation**: read the host's key (via `juce::AudioPlayHead::CurrentPositionInfo::timeSignatureNumerator/denominator`) and feed it into the model.
4. **Drag-and-drop MIDI export**: expose the current pattern as a temp `.mid` file and use `juce::DragAndDropContainer::performExternalDragDropOfFiles` so users can drag patterns straight into their DAW.
5. **Built-in sampler**: bundle a small drum kit (`juce::Synthesiser` + `juce::SamplerVoice`) so the plugin produces audio on its own.

## License

TBD — add a license before distributing.
