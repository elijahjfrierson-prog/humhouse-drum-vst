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
        GenerationMode mode       = GenerationMode::Groove;
        float          variation  = 0.5f; // 0..1
        float          density    = 0.5f; // 0..1
        double         tempoBpm   = 120.0;
        int            numerator  = 4;
        int            denominator = 4;
        std::uint64_t  seed       = 0;    // 0 = random
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

        MidiPattern generate(const GenerationRequest& request);

    private:
        MidiPattern makeBasicGroove(const GenerationRequest& r) const;
        MidiPattern makeBasicFill  (const GenerationRequest& r) const;
    };
}
