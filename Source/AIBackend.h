#pragma once

#include "MidiPattern.h"

#include <cstdint>
#include <string>

namespace aidrum
{
    enum class GenerationMode
    {
        Groove,
        Fill
    };

    struct GenerationRequest
    {
        GenerationMode mode          = GenerationMode::Groove;
        float          variation     = 0.5f;  // 0..1 — how much each generation deviates
        float          complexity    = 0.5f;  // 0..1 — sparse → busy
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
    // The production implementation will bridge to a Python module
    // (see python_backend/ai_drum_backend.py) via pybind11 or a
    // subprocess. For now this returns deterministic canned patterns
    // so the plugin can be wired end-to-end.
    class AIBackend
    {
    public:
        AIBackend();
        ~AIBackend();

        MidiPattern generate (const GenerationRequest& request);

    private:
        MidiPattern makeGroove (const GenerationRequest& r) const;
        MidiPattern makeFill   (const GenerationRequest& r) const;

        // Post-process: apply master velocity and humanize (timing + velocity jitter).
        static void finalize (MidiPattern& pattern, const GenerationRequest& r, std::uint64_t seed);
    };
}
