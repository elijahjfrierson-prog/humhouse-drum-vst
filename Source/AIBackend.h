#pragma once

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
