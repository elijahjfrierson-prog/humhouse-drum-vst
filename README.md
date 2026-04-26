# HumHouse Drums

A gothic, Logic-Drummer-style generative drum instrument built with
[JUCE](https://juce.com). Ships as:

- **Standalone app** (macOS, Windows, Linux) — runs without a DAW.
- **VST3 plugin** — Ableton Live, FL Studio, Reaper, Cubase, Studio One, Bitwig.
- **AU plugin** (macOS only) — required for Logic Pro and GarageBand.

Features a **+** (append) button for Logic-Drummer-style groove chaining,
a live multi-region piano-roll visualizer, 12 genres + Auto, Velocity /
Humanize / Complexity / Swing / Fills knobs, Half-Time toggle and a
Hi-Hat override combo. Drag the handle to drop the full arrangement
as a `.mid` directly into your DAW.

## Downloadable binaries

Binaries are produced by the GitHub Actions workflow
(`.github/workflows/build.yml`) on every push and on every GitHub Release:

| Platform | Formats                                              | Release asset                        |
|----------|------------------------------------------------------|--------------------------------------|
| macOS    | Universal `.vst3` + `.component` (AU), drag-to-install | `HumHouse-Drums-macOS.dmg`           |
| Windows  | `.vst3` + Standalone `.exe`                          | `HumHouse-Drums-Windows-x64.zip`     |
| Linux    | `.vst3` + Standalone binary                          | `HumHouse-Drums-Linux-x86_64.zip`    |

Cutting a GitHub Release (`git tag vX.Y.Z && git push --tags` → *Create release from tag*)
runs the workflow and uploads all three assets — a real drag-to-install
macOS `.dmg`, a Windows zip, and a Linux zip — so anyone can download
them from the release page without a GitHub login.

### macOS `.dmg` contents

`scripts/package_macos_dmg.sh` produces a DMG that mounts to a volume
named **HumHouse Drums** with:

- `HumHouse Drums.vst3` — universal binary (arm64 + x86_64), loads in
  FL Studio, Ableton Live, Reaper, Cubase, Studio One, Bitwig, etc.
- `HumHouse Drums.component` — Audio Unit, **required for Logic Pro and
  GarageBand** (Logic does not load VST3).
- `HumHouse Drums.app` — standalone app.
- `VST3 Plug-Ins` → symlink to `/Library/Audio/Plug-Ins/VST3`
- `Audio Unit Plug-Ins` → symlink to `/Library/Audio/Plug-Ins/Components`
- `README.txt` with install instructions.

Users drag the two bundles onto the matching symlinks, relaunch their DAW,
rescan plugins, done.

### Gatekeeper note

Builds are ad-hoc signed by default. The first time a DAW loads the
plugin, macOS Gatekeeper may block it. Fix:

```bash
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/HumHouse Drums.vst3"
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/HumHouse Drums.component"
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
│   ├── PluginProcessor.{h,cpp}  # Arrangement engine + host sync + APVTS
│   ├── PluginEditor.{h,cpp}     # Gothic GUI: +, arrangement grid, knobs
│   ├── AIBackend.{h,cpp}        # Rule-based genre-aware drum generator
│   ├── ArrangementStrip.h       # Multi-region piano-roll visualizer
│   ├── GothicLookAndFeel.h      # Black / purple / bone palette + styling
│   └── MidiPattern.h            # Shared pattern/note data types
├── python_backend/
│   └── ai_drum_backend.py       # Python stub mirror; target for pybind11 bridge
└── scripts/package_macos_dmg.sh # macOS DMG packager
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

- `VST3/HumHouse Drums.vst3` — drop into your DAW's VST3 path.
- `Standalone/HumHouse Drums` — runs as a standalone app for quick testing.

Standard VST3 install locations:

| OS       | Path                                                |
|----------|-----------------------------------------------------|
| macOS    | `~/Library/Audio/Plug-Ins/VST3`                     |
| Windows  | `C:\Program Files\Common Files\VST3`                |
| Linux    | `~/.vst3`                                           |

## Using the plugin

1. Press **+** (APPEND) to append a new generated region to the arrangement.
2. Press **UNDO** to remove the last region or **CLEAR** to start over.
3. Adjust **Genre / Length / Mode** plus **Swing / Fills / Half-Time / Hi-Hat**
   to shape the next region before you append it.
4. Drag the **DRAG MIDI → DAW** handle onto a track in your DAW to drop the
   entire arrangement as a single MIDI file. **SAVE MIDI** writes it to
   disk instead.
