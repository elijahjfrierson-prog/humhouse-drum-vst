# HumHouse Drums X

A session-drummer instrument built with [JUCE](https://juce.com): one genre
family (rock / hard rock), one kit, and a performance that comes from real
recorded drummers rather than from probability tables. Ships as:

- **Standalone app** (macOS, Windows, Linux) — runs without a DAW.
- **VST3 plugin** — Ableton Live, FL Studio, Reaper, Cubase, Studio One, Bitwig.
- **AU plugin** (macOS only) — required for Logic Pro and GarageBand.

## How it works

Three layers, each replaceable on its own:

| Layer | Source | Job |
|-------|--------|-----|
| `GrooveCorpus` | `content/rock_corpus.hhc` | 3,600+ bar-aligned phrases sliced out of real human takes, indexed on a (complexity, intensity) plane. Selection is nearest-neighbour — the plugin picks a take somebody actually played, it never invents one note-by-note. |
| `PerformanceEngine` | pure C++, no JUCE | Tiles phrases into bars, swaps in a *different* real fill at the end of each phrase, then applies swing, feel/push-pull, ghost scaling, hat openness, ride substitution, lane masks and humanisation. Deterministic: the same settings + seed + bar always render identically, so the preview, playback and MIDI export can never disagree. |
| `KitEngine` | `Resources/KitX` | Velocity-layered, round-robin sampler with per-lane gain/pan, hi-hat choking and a per-piece **sample switch**. |

Three pages, mirroring the way Logic's Drummer is organised:

- **MAIN** — character list, the XY performance pad (soft↔loud against
  simple↔complex), Complexity / Intensity / Fills / Swing, numbered variation
  buttons for the rhythm and cymbal groups, and a live 4-bar performance view.
- **DETAILS** — Feel, Ghost Notes, Hat Openness, Humanize, Fill Complexity,
  fill length (1–2 bars), phrase length, time signature, host-sync or manual
  BPM, ride/half-time, and the manual step editor.
- **KIT** — every kit piece with on/off, sample switch, level and pan, plus
  kit folder loading, output level and UI scale.

Manual edits live in their own layer: moving a knob re-renders the generated
performance around them and never wipes what you placed by hand.

MIDI export writes the full arrangement or one file per kit piece, with tempo
and time-signature metadata attached.

## Content and licensing

- The groove corpus is compiled by `tools/corpusx/build_corpus.py` from the
  **Magenta Groove MIDI Dataset** (CC-BY 4.0). Attribution and the source
  file identifiers are preserved inside `rock_corpus.hhc`.
- The bundled kit is sliced from HumHouse's own drum recordings by
  `tools/kitx/slice_kit.py`.

## Downloadable binaries

Binaries are produced by the GitHub Actions workflow
(`.github/workflows/build.yml`) on every push and on every GitHub Release:

| Platform | Formats                                              | Release asset                        |
|----------|------------------------------------------------------|--------------------------------------|
| macOS    | Universal `.vst3` + `.component` (AU), drag-to-install | `HumHouse-Drums-X-macOS.dmg`           |
| Windows  | `.vst3` + Standalone `.exe`                          | `HumHouse-Drums-X-Windows-x64.zip`     |
| Linux    | `.vst3` + Standalone binary                          | `HumHouse-Drums-X-Linux-x86_64.zip`    |

Cutting a GitHub Release (`git tag vX.Y.Z && git push --tags` → *Create release from tag*)
runs the workflow and uploads all three assets — a real drag-to-install
macOS `.dmg`, a Windows zip, and a Linux zip — so anyone can download
them from the release page without a GitHub login.

### macOS `.dmg` contents

`scripts/package_macos_dmg.sh` produces a DMG that mounts to a volume
named **HumHouse Drums X** with:

- `HumHouse Drums X.vst3` — universal binary (arm64 + x86_64), loads in
  FL Studio, Ableton Live, Reaper, Cubase, Studio One, Bitwig, etc.
- `HumHouse Drums X.component` — Audio Unit, **required for Logic Pro and
  GarageBand** (Logic does not load VST3).
- `HumHouse Drums X.app` — standalone app.
- `VST3 Plug-Ins` → symlink to `/Library/Audio/Plug-Ins/VST3`
- `Audio Unit Plug-Ins` → symlink to `/Library/Audio/Plug-Ins/Components`
- `README.txt` with install instructions.

Users drag the two bundles onto the matching symlinks, relaunch their DAW,
rescan plugins, done.

### Gatekeeper note

Builds are ad-hoc signed by default. The first time a DAW loads the
plugin, macOS Gatekeeper may block it. Fix:

```bash
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/HumHouse Drums X.vst3"
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/HumHouse Drums X.component"
```

For a fully trusted install, add an `APPLE_DEVELOPER_ID` repo secret (e.g.
`"Developer ID Application: Your Name (TEAMID)"`). The packaging script
already picks it up. Notarization via `notarytool` is a natural next
step — see the TODO comment in `scripts/package_macos_dmg.sh`.

### About OBS

OBS hosts VST **2** plugins only, so a VST3 never appears in its filter list.
Record the standalone app instead and route it through a virtual audio device —
see [docs/OBS.md](docs/OBS.md).

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
├── SourceX/
│   ├── GrooveCorpus.{h,cpp}       # HHCX corpus reader + phrase selection (no JUCE)
│   ├── PerformanceEngine.{h,cpp}  # Phrase → bars, fills, feel, humanisation (no JUCE)
│   ├── KitEngine.{h,cpp}          # Velocity-layered round-robin sampler
│   ├── DrumsXProcessor.{h,cpp}    # APVTS, cached timeline, playback, MIDI export
│   ├── DrumsXEditor.{h,cpp}       # MAIN / DETAILS / KIT pages
│   └── DrumsXLookAndFeel.h        # Slate + gold styling, thin-arc knobs
├── content/rock_corpus.hhc      # Compiled human-performance corpus
├── Resources/KitX/              # The bundled rock kit (velocity layers + round robins)
├── tests/EngineTests.cpp        # Determinism / fills / lane masks / metre
├── tools/corpusx/build_corpus.py # Groove MIDI → rock_corpus.hhc
├── tools/kitx/slice_kit.py      # Raw drum recordings → sliced kit samples
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

- `VST3/HumHouse Drums X.vst3` — drop into your DAW's VST3 path.
- `Standalone/HumHouse Drums X` — runs as a standalone app for quick testing.

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
