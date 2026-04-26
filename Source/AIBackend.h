#pragma once

#include "DrumKit.h"
#include "MidiPattern.h"

#include <cstdint>
#include <string>
#include <vector>

namespace aidrum
{
    enum class GenerationMode
    {
        Groove,
        Fill
    };

    // Logic-Drummer-style hi-hat voicing override.
    enum class HiHatMode : int
    {
        Dynamic = 0,  // use genre default (closed/ride/train/etc.)
        Closed,       // force GM closed hat (42)
        Open,         // force GM open hat  (46)
        Ride          // force GM ride      (51)
    };

    enum class Genre : int
    {
        Auto = 0,
        HardRock,
        ClassicRock,
        Shoegaze,
        Metal,
        Metalcore,
        Jazz,
        Funk,
        RnB,
        HipHop,
        Trap,
        Pop,
        Country,
        Count
    };

    // v1.6.0 \u2014 one of the bundled character kits. Each one has its own
    // kick/snare placement profile so the same COMPLEXITY value produces
    // slightly different feels between genres (PopRock = straight backbeat,
    // NuRock = syncopated, AltRock = laid-back, IndieLofi = half-time hip-hop,
    // Thrash = aggressive double-kick drive, HardRock = driving quarter-kick
    // with cracking backbeat, modelled on MODO Drum's "Hard Rock" preset).
    enum class BundledKit : int
    {
        PopRock = 0,
        NuRock,
        AltRock,
        IndieLofi,
        Thrash,
        HardRock,
        Count
    };

    const std::vector<std::string>& genreDisplayNames();

    struct GenerationRequest
    {
        GenerationMode mode          = GenerationMode::Groove;
        Genre          genre         = Genre::Auto;
        float          variation     = 0.5f;  // 0..1
        float          complexity    = 0.5f;  // 0..1
        float          velocity      = 1.0f;  // 0..1 — master velocity scale
        float          humanize      = 0.25f; // 0..1 — timing + velocity jitter
        double         tempoBpm      = 120.0;
        int            numerator     = 4;
        int            denominator   = 4;
        double         lengthInBeats = 4.0;   // 0.25 (1/16) … 8.0 (2 bars)
        std::uint64_t  seed          = 0;     // 0 = random

        // Logic-Drummer-style controls (v0.6.0)
        float          swing         = 0.0f;  // 0..1 — 0 = straight, 1 = full triplet feel on offbeats
        float          fillsProb     = 0.0f;  // 0..1 — chance a Groove call is reshaped into a Fill
        bool           halfTime      = false; // snare on 3 instead of 2+4
        HiHatMode      hiHatMode     = HiHatMode::Dynamic;

        // v1.5.0 — fill intricacy, independent of overall complexity. Drives the
        // density of a Fill pattern (number of stick hits, ghost notes, tom rolls,
        // grace-note rudiments). Groove patterns ignore this value.
        //
        // v1.6.1-rc.7 — superseded by fillIndex when fillIndex >= 0. The Fill
        // Complexity knob in the UI was replaced with a "FILL SELECTOR" cycler
        // that rotates through the 20+ user-supplied MIDI fills. When a specific
        // library fill is selected, we bypass the deterministic rudiment
        // generator and emit that fill verbatim.
        float          fillComplexity = 0.5f; // 0..1

        // v1.6.1-rc.7 — index into fillLibrary() (FillLibrary.generated.h).
        // When >= 0 and < library size, makeFill() returns that exact MIDI
        // pattern (velocity-scaled per the intensity knob). When -1 the old
        // procedural rudiment generator is used.
        int            fillIndex = -1;

        // v1.6.1-rc.7 — intensity knob (0..1, displayed as 0..127 / 0..100%).
        // Drives base velocity + per-hit fluctuation. Applied at MIDI emit
        // time so user-drawn note velocities remain editable while the
        // overall dynamic feel follows the knob.
        float          intensity      = 0.70f; // 0..1

        // Drumkit voicing (v0.7.0): remaps GM notes + velocity/ghost curves.
        DrumKit        kit           = DrumKit::LudwigSupraphonicClassicRock;

        // v1.6.0 \u2014 the selected bundled character kit. Controls kick/snare
        // placement profile on top of the Genre-driven cymbal/backbone feel.
        BundledKit     bundledKit    = BundledKit::PopRock;

        // v1.3.0 session-player state: counts the index of this region in the
        // arrangement so phrase-level dynamics (crescendos, bar 7/8 builds,
        // ghost drift, hat loosening) and unique fills can evolve over time.
        int            phraseBar     = 0;   // 0..N, monotonic across regions

        // v1.6.1-rc.8 — finest grid resolution the user wants the
        // generator to use when placing decorative hits. 16 = standard
        // 1/16 grooves only (the rc.7 default), 32 enables tasteful
        // 32nd-note hat ostinatos + ghost-drag rolls into fills, 64
        // unlocks 64th-note grace strokes (single accents + last-bar
        // pickup rolls only — never spam). The generator scales the
        // *probability* of decorations with the gap between 16 and the
        // chosen value, so the pad never just throws notes in for the
        // sake of being denser.
        int            stepsPerBar   = 16;
    };

    // Stub AI backend.
    //
    // Bridges to a Python module (see python_backend/ai_drum_backend.py)
    // once wired via pybind11. For now the C++ implementation ships a
    // per-genre rule-based generator so the plugin is fully playable
    // out of the box.
    class AIBackend
    {
    public:
        AIBackend();
        ~AIBackend();

        MidiPattern generate (const GenerationRequest& request);

    private:
        // Resolves Genre::Auto to a concrete genre using the RNG.
        static Genre resolveGenre (Genre requested, std::uint64_t seed);

        MidiPattern makeGroove (const GenerationRequest& r, Genre resolvedGenre) const;
        MidiPattern makeFill   (const GenerationRequest& r, Genre resolvedGenre) const;

        static void finalize (MidiPattern& pattern, const GenerationRequest& r, std::uint64_t seed);
    };
}
