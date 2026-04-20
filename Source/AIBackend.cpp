#include "AIBackend.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace aidrum
{
    namespace
    {
        // General MIDI drum map
        constexpr int kKick      = 36;
        constexpr int kSnare     = 38;
        constexpr int kClosedHat = 42;
        constexpr int kOpenHat   = 46;
        constexpr int kLowTom    = 41;
        constexpr int kMidTom    = 45;
        constexpr int kHighTom   = 48;
        constexpr int kCrash     = 49;

        // 16th-note grid size (in beats).
        constexpr double kSixteenth = 0.25;

        std::uint64_t resolveSeed (std::uint64_t s)
        {
            return s != 0 ? s : static_cast<std::uint64_t> (std::random_device{}());
        }
    }

    AIBackend::AIBackend()  = default;
    AIBackend::~AIBackend() = default;

    MidiPattern AIBackend::generate (const GenerationRequest& r)
    {
        const std::uint64_t seed = resolveSeed (r.seed);

        GenerationRequest seeded = r;
        seeded.seed = seed;

        MidiPattern pattern = (r.mode == GenerationMode::Fill)
                                ? makeFill   (seeded)
                                : makeGroove (seeded);

        finalize (pattern, seeded, seed ^ 0x9E3779B97F4A7C15ULL);
        return pattern;
    }

    MidiPattern AIBackend::makeGroove (const GenerationRequest& r) const
    {
        MidiPattern pattern;
        pattern.lengthInBeats = std::max (kSixteenth, r.lengthInBeats);

        std::mt19937_64 rng (r.seed);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);

        const int numSixteenths = static_cast<int> (std::round (pattern.lengthInBeats / kSixteenth));

        // Probability scales with complexity for each drum voice.
        const float kickProb   = 0.35f + 0.25f * r.complexity;
        const float snareProb  = 0.25f + 0.30f * r.complexity;
        const float ghostProb  = 0.05f + 0.35f * r.complexity;
        const float openHatProb = 0.05f + 0.20f * r.variation;

        for (int step = 0; step < numSixteenths; ++step)
        {
            const double beat         = kSixteenth * step;
            const int    sixteenthInBar = step % 16;
            const int    eighthInBar  = sixteenthInBar / 2;
            const int    quarterInBar = sixteenthInBar / 4;

            // Backbone: kick on beats 1 and 3 of each bar, snare on 2 and 4.
            const bool   isDownbeat = (sixteenthInBar % 4 == 0);
            const bool   isKickBeat = (quarterInBar == 0 || quarterInBar == 2);
            const bool   isSnareBeat = (quarterInBar == 1 || quarterInBar == 3);

            if (isDownbeat && isKickBeat)
                pattern.notes.push_back ({ kKick, 0.95f, beat, 0.25 });

            if (isDownbeat && isSnareBeat)
                pattern.notes.push_back ({ kSnare, 0.9f, beat, 0.25 });

            // Extra kick syncopation (probability rises with complexity).
            if (! isDownbeat && (sixteenthInBar % 4 == 3) && unit (rng) < kickProb * 0.6f)
                pattern.notes.push_back ({ kKick, 0.78f, beat, 0.25 });

            // Ghost snares between the backbones.
            if (! (isDownbeat && isSnareBeat)
                && sixteenthInBar % 4 != 0
                && unit (rng) < ghostProb)
            {
                pattern.notes.push_back ({ kSnare, 0.30f + 0.20f * unit (rng), beat, 0.125 });
            }

            // Hats on 8ths (always) with density-driven 16th doubling.
            if (sixteenthInBar % 2 == 0)
            {
                const float vel = 0.55f + 0.25f * unit (rng) * r.variation;
                pattern.notes.push_back ({ kClosedHat, vel, beat, 0.125 });
            }
            else if (unit (rng) < r.complexity * 0.85f)
            {
                pattern.notes.push_back ({ kClosedHat, 0.45f + 0.25f * unit (rng), beat, 0.0625 });
            }

            // Occasional open hat on the "and" of beats (scaled by variation).
            (void) eighthInBar;
            if (sixteenthInBar == 14 && unit (rng) < openHatProb)
                pattern.notes.push_back ({ kOpenHat, 0.65f, beat, 0.25 });

            (void) snareProb;
        }

        return pattern;
    }

    MidiPattern AIBackend::makeFill (const GenerationRequest& r) const
    {
        MidiPattern pattern;
        pattern.lengthInBeats = std::max (kSixteenth, r.lengthInBeats);

        std::mt19937_64 rng (r.seed);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);

        const int numSixteenths = static_cast<int> (std::round (pattern.lengthInBeats / kSixteenth));
        const int toms[] = { kHighTom, kMidTom, kLowTom };

        // Tom roll down the kit over the whole pattern.
        for (int step = 0; step < numSixteenths; ++step)
        {
            const double beat   = kSixteenth * step;
            const int    tomIdx = std::min<int> ((int) std::size (toms) - 1,
                                                 (step * (int) std::size (toms)) / std::max (1, numSixteenths));
            const int    tom    = toms[tomIdx];
            const float  baseV  = 0.60f + 0.25f * unit (rng);

            pattern.notes.push_back ({ tom, baseV, beat, 0.125 });

            // Snare interjections scaled by complexity.
            if (unit (rng) < r.complexity * 0.6f)
                pattern.notes.push_back ({ kSnare, 0.50f + 0.30f * unit (rng), beat, 0.125 });

            // Kick accents.
            if (step % 4 == 0)
                pattern.notes.push_back ({ kKick, 0.85f, beat, 0.25 });
        }

        // Cap the fill with a crash + kick.
        pattern.notes.push_back ({ kCrash, 1.0f, 0.0, 1.0 });
        pattern.notes.push_back ({ kKick,  0.95f, 0.0, 0.25 });
        return pattern;
    }

    void AIBackend::finalize (MidiPattern& pattern, const GenerationRequest& r, std::uint64_t seed)
    {
        std::mt19937_64 rng (seed);
        std::uniform_real_distribution<float> jitter (-1.0f, 1.0f);

        // Humanize: timing up to ±30ms-equivalent (≈ 0.036 beat at 120 BPM) and velocity ±20%.
        const double timingJitterBeats = 0.04 * static_cast<double> (r.humanize);
        const float  velJitter         = 0.20f * r.humanize;

        for (auto& note : pattern.notes)
        {
            note.velocity = std::clamp (note.velocity * r.velocity + velJitter * jitter (rng),
                                        0.01f, 1.0f);
            note.startBeat = std::clamp (note.startBeat + timingJitterBeats * jitter (rng),
                                         0.0, std::max (0.0, pattern.lengthInBeats - 0.001));
        }
    }
}
