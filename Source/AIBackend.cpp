#include "AIBackend.h"

#include <random>

namespace aidrum
{
    namespace
    {
        // General MIDI drum map
        constexpr int kKick        = 36;
        constexpr int kSnare       = 38;
        constexpr int kClosedHat   = 42;
        constexpr int kOpenHat     = 46;
        constexpr int kLowTom      = 41;
        constexpr int kMidTom      = 45;
        constexpr int kHighTom     = 48;
        constexpr int kCrash       = 49;
    }

    AIBackend::AIBackend()  = default;
    AIBackend::~AIBackend() = default;

    MidiPattern AIBackend::generate(const GenerationRequest& r)
    {
        return r.mode == GenerationMode::Fill ? makeBasicFill(r) : makeBasicGroove(r);
    }

    MidiPattern AIBackend::makeBasicGroove(const GenerationRequest& r) const
    {
        MidiPattern pattern;
        pattern.lengthInBeats = static_cast<double>(r.numerator);

        const std::uint64_t seed = r.seed != 0 ? r.seed : std::random_device{}();
        std::mt19937_64 rng(seed);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);

        // Kick on 1 and 3, snare on 2 and 4, hats on every 8th.
        const int steps = r.numerator * 2; // 8th notes
        for (int step = 0; step < steps; ++step)
        {
            const double beat = 0.5 * step;

            if (step % 4 == 0)
                pattern.notes.push_back({ kKick, 0.95f, beat, 0.25 });

            if (step % 4 == 2)
                pattern.notes.push_back({ kSnare, 0.9f, beat, 0.25 });

            // Hats every 8th with slight velocity variation tied to `variation`.
            const float vel = 0.55f + 0.25f * unit(rng) * r.variation;
            pattern.notes.push_back({ kClosedHat, vel, beat, 0.125 });

            // Ghost snares scaled by density.
            if (unit(rng) < r.density * 0.2f && step % 4 != 2 && step % 4 != 0)
                pattern.notes.push_back({ kSnare, 0.35f, beat, 0.125 });
        }

        // Occasional open hat on the "and" of 4, scaled by variation.
        if (unit(rng) < r.variation)
            pattern.notes.push_back({ kOpenHat, 0.7f, static_cast<double>(r.numerator) - 0.5, 0.25 });

        return pattern;
    }

    MidiPattern AIBackend::makeBasicFill(const GenerationRequest& r) const
    {
        MidiPattern pattern;
        pattern.lengthInBeats = static_cast<double>(r.numerator);

        const std::uint64_t seed = r.seed != 0 ? r.seed : std::random_device{}();
        std::mt19937_64 rng(seed);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);

        // Tom rolls down the kit over the last bar, finish with a crash.
        const int steps = r.numerator * 4; // 16ths
        const int toms[] = { kHighTom, kMidTom, kLowTom };

        for (int step = 0; step < steps; ++step)
        {
            const double beat = 0.25 * step;
            const int    tom  = toms[(step / 2) % 3];
            const float  vel  = 0.6f + 0.3f * unit(rng);
            pattern.notes.push_back({ tom, vel, beat, 0.125 });

            if (unit(rng) < r.density * 0.5f)
                pattern.notes.push_back({ kSnare, 0.5f + 0.3f * unit(rng), beat, 0.125 });
        }

        pattern.notes.push_back({ kCrash, 1.0f, 0.0, 1.0 });
        return pattern;
    }
}
