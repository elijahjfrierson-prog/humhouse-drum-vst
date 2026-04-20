#include "AIBackend.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace aidrum
{
    namespace
    {
        // General MIDI drum map
        constexpr int kKick       = 36;
        constexpr int kSideStick  = 37;
        constexpr int kSnare      = 38;
        constexpr int kClap       = 39;
        constexpr int kLowTom     = 41;
        constexpr int kClosedHat  = 42;
        constexpr int kMidTom     = 45;
        constexpr int kOpenHat    = 46;
        constexpr int kHighTom    = 48;
        constexpr int kCrash      = 49;
        constexpr int kRide       = 51;
        constexpr int kChina      = 52;
        constexpr int kRideBell   = 53;

        constexpr double kSixteenth = 0.25;
        constexpr double kEighth    = 0.5;

        std::uint64_t resolveSeed (std::uint64_t s)
        {
            return s != 0 ? s : static_cast<std::uint64_t> (std::random_device{}());
        }

        inline void addNote (MidiPattern& p, int note, float vel, double beat, double len)
        {
            p.notes.push_back ({ note, vel, beat, len });
        }

        // Genre bundles the small set of "hit probabilities" that distinguish
        // the styles without duplicating a full generator for each.
        struct GenreProfile
        {
            int   mainCymbal   = kClosedHat;   // primary time-keeping voice
            int   altCymbal    = kOpenHat;     // open / accent cymbal
            int   rideCymbal   = kRide;
            int   crashCymbal  = kCrash;
            int   snareMain    = kSnare;
            int   snareAlt     = kSideStick;   // cross-stick / rim
            int   clap         = kClap;
            bool  useClap      = false;
            bool  triplets     = false;        // swing/jazz triplet hat feel
            bool  rideNotHat   = false;        // jazz/funk ride-bell time keeping
            bool  doubleKick   = false;        // metal double-pedal
            bool  fourOnFloor  = false;        // pop/dance
            bool  trainBeat    = false;        // country train beat
            bool  boomBap      = false;        // hip-hop two-bar feel
            bool  trapHats     = false;        // 32nd-note trap rolls
            float kickProbBase = 0.35f;
            float kickProbSync = 0.60f;
            float ghostProb    = 0.20f;
            float openHatProb  = 0.15f;
            float velBase      = 0.82f;
            float velAccent    = 0.98f;
            // Fill character:
            int   fillBaseTom  = kHighTom;
            bool  fillUsesRoll = true;
            bool  fillUsesChina = false;
        };

        GenreProfile profileFor (Genre g)
        {
            GenreProfile p;
            switch (g)
            {
                case Genre::HardRock:
                    p.mainCymbal = kClosedHat; p.crashCymbal = kCrash;
                    p.kickProbSync = 0.65f; p.ghostProb = 0.12f;
                    p.openHatProb = 0.25f;  p.velAccent = 1.0f;
                    break;

                case Genre::ClassicRock:
                    p.mainCymbal = kRide;   p.rideNotHat = true;
                    p.kickProbSync = 0.35f; p.ghostProb = 0.08f;
                    p.openHatProb = 0.05f;  p.velAccent = 0.95f;
                    break;

                case Genre::Shoegaze:
                    p.mainCymbal = kClosedHat; p.altCymbal = kOpenHat;
                    p.kickProbSync = 0.20f;    p.ghostProb = 0.05f;
                    p.openHatProb = 0.55f; // washy, lots of open hat
                    p.velBase = 0.70f;     p.velAccent = 0.88f;
                    break;

                case Genre::Metal:
                    p.mainCymbal = kClosedHat; p.doubleKick = true;
                    p.kickProbBase = 0.9f;     p.kickProbSync = 0.9f;
                    p.ghostProb = 0.05f;       p.openHatProb = 0.10f;
                    p.velBase = 0.95f;         p.velAccent = 1.0f;
                    p.fillUsesChina = true;
                    break;

                case Genre::Metalcore:
                    p.mainCymbal = kClosedHat; p.doubleKick = true;
                    p.kickProbBase = 0.7f;     p.kickProbSync = 0.8f;
                    p.ghostProb = 0.10f;       p.openHatProb = 0.15f;
                    p.velBase = 0.9f;          p.velAccent = 1.0f;
                    p.fillUsesChina = true;
                    break;

                case Genre::Jazz:
                    p.mainCymbal = kRide;   p.rideNotHat = true; p.triplets = true;
                    p.snareAlt   = kSideStick;
                    p.kickProbBase = 0.25f; p.kickProbSync = 0.35f;
                    p.ghostProb = 0.55f;    p.openHatProb = 0.10f;
                    p.velBase = 0.65f;      p.velAccent = 0.85f;
                    p.fillUsesRoll = false;
                    break;

                case Genre::Funk:
                    p.mainCymbal = kClosedHat;
                    p.kickProbBase = 0.55f; p.kickProbSync = 0.85f;
                    p.ghostProb = 0.55f;    p.openHatProb = 0.45f;
                    p.velBase = 0.80f;      p.velAccent = 0.95f;
                    break;

                case Genre::RnB:
                    p.mainCymbal = kClosedHat; p.useClap = true;
                    p.kickProbBase = 0.40f;    p.kickProbSync = 0.45f;
                    p.ghostProb = 0.35f;       p.openHatProb = 0.20f;
                    p.velBase = 0.72f;         p.velAccent = 0.90f;
                    break;

                case Genre::HipHop:
                    p.mainCymbal = kClosedHat; p.useClap = true; p.boomBap = true;
                    p.kickProbBase = 0.60f;    p.kickProbSync = 0.55f;
                    p.ghostProb = 0.15f;       p.openHatProb = 0.20f;
                    p.velBase = 0.85f;         p.velAccent = 1.0f;
                    break;

                case Genre::Trap:
                    p.mainCymbal = kClosedHat; p.trapHats = true; p.useClap = true;
                    p.kickProbBase = 0.45f;    p.kickProbSync = 0.35f;
                    p.ghostProb = 0.05f;       p.openHatProb = 0.10f;
                    p.velBase = 0.82f;         p.velAccent = 1.0f;
                    break;

                case Genre::Pop:
                    p.mainCymbal = kClosedHat; p.useClap = true; p.fourOnFloor = true;
                    p.kickProbBase = 0.80f;    p.kickProbSync = 0.40f;
                    p.ghostProb = 0.08f;       p.openHatProb = 0.15f;
                    p.velBase = 0.82f;         p.velAccent = 0.96f;
                    break;

                case Genre::Country:
                    p.mainCymbal = kClosedHat; p.trainBeat = true;
                    p.snareAlt = kSideStick;
                    p.kickProbBase = 0.55f;    p.kickProbSync = 0.30f;
                    p.ghostProb = 0.35f;       p.openHatProb = 0.15f;
                    p.velBase = 0.78f;         p.velAccent = 0.92f;
                    break;

                case Genre::Auto:
                case Genre::Count:
                default:
                    break;
            }
            return p;
        }
    } // namespace

    const std::vector<std::string>& genreDisplayNames()
    {
        static const std::vector<std::string> names {
            "Auto (multi-genre)",
            "Hard Rock",
            "Classic Rock",
            "Shoegaze",
            "Metal",
            "Metalcore",
            "Jazz",
            "Funk",
            "R&B",
            "Hip Hop",
            "Trap",
            "Pop",
            "Country"
        };
        return names;
    }

    AIBackend::AIBackend()  = default;
    AIBackend::~AIBackend() = default;

    Genre AIBackend::resolveGenre (Genre requested, std::uint64_t seed)
    {
        if (requested != Genre::Auto)
            return requested;

        std::mt19937_64 rng (seed);
        std::uniform_int_distribution<int> pick (1, static_cast<int> (Genre::Count) - 1);
        return static_cast<Genre> (pick (rng));
    }

    MidiPattern AIBackend::generate (const GenerationRequest& r)
    {
        const std::uint64_t seed    = resolveSeed (r.seed);
        const Genre         resolved = resolveGenre (r.genre, seed);

        GenerationRequest seeded = r;
        seeded.seed  = seed;
        seeded.genre = resolved;

        MidiPattern pattern = (r.mode == GenerationMode::Fill)
                                ? makeFill   (seeded, resolved)
                                : makeGroove (seeded, resolved);

        finalize (pattern, seeded, seed ^ 0x9E3779B97F4A7C15ULL);
        return pattern;
    }

    MidiPattern AIBackend::makeGroove (const GenerationRequest& r, Genre genre) const
    {
        const GenreProfile g = profileFor (genre);

        MidiPattern pattern;
        pattern.lengthInBeats = std::max (kSixteenth, r.lengthInBeats);

        std::mt19937_64 rng (r.seed);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);

        const int numSixteenths = static_cast<int> (std::round (pattern.lengthInBeats / kSixteenth));
        const int timeKeeper = g.rideNotHat ? g.rideCymbal : g.mainCymbal;

        // Scale sparse genres back when Complexity is low.
        const float kickSync  = g.kickProbSync * (0.5f + 0.5f * r.complexity);
        const float ghost     = g.ghostProb    * (0.3f + 0.7f * r.complexity);
        const float openHat   = g.openHatProb  * (0.4f + 0.6f * r.variation);

        // ---- Time-keeping layer ------------------------------------------
        if (g.triplets)
        {
            // Swing/triplet feel: ride on triplets per beat.
            const int beats = std::max (1, (int) std::round (pattern.lengthInBeats));
            for (int beat = 0; beat < beats; ++beat)
            {
                const double b = static_cast<double> (beat);
                addNote (pattern, g.rideCymbal, g.velBase, b,                  kEighth);
                // Jazz ride pattern: "spang, a-lang" = quarter, 8th-triplet, 8th-triplet
                if ((beat % 2) == 1)
                {
                    addNote (pattern, g.rideCymbal, g.velBase * 0.75f, b + 2.0 / 3.0, 1.0 / 3.0);
                    addNote (pattern, kRideBell,   g.velBase * 0.5f,  b + 1.0 / 3.0, 1.0 / 3.0);
                }
            }
        }
        else if (g.trapHats)
        {
            // Trap: rolling closed hats with 32nd/triplet bursts based on complexity.
            for (int step = 0; step < numSixteenths; ++step)
            {
                const double beat = kSixteenth * step;
                // Base 16th hat
                addNote (pattern, g.mainCymbal, g.velBase, beat, 0.0625);
                // Rolls: subdivide into 32nds
                if (unit (rng) < 0.25f + 0.65f * r.complexity)
                    addNote (pattern, g.mainCymbal, g.velBase * 0.7f, beat + 0.125, 0.0625);
                // Occasional 16th-triplet burst
                if (unit (rng) < r.complexity * 0.2f)
                {
                    for (int t = 1; t <= 2; ++t)
                        addNote (pattern, g.mainCymbal, g.velBase * 0.6f,
                                 beat + kSixteenth * (t / 3.0), 0.0625);
                }
            }
        }
        else
        {
            for (int step = 0; step < numSixteenths; ++step)
            {
                const double beat = kSixteenth * step;
                const int    s16  = step % 16;

                // 8th-note main cymbal, with complexity-driven 16th doubling.
                if (s16 % 2 == 0)
                {
                    const float vel = g.velBase + 0.10f * unit (rng) * r.variation;
                    addNote (pattern, timeKeeper, vel, beat, 0.125);
                }
                else if (unit (rng) < r.complexity * 0.85f)
                {
                    addNote (pattern, timeKeeper, g.velBase * 0.75f, beat, 0.0625);
                }
            }
        }

        // ---- Kick + snare backbone ---------------------------------------
        const int beatsTotal = std::max (1, (int) std::round (pattern.lengthInBeats));
        for (int beat = 0; beat < beatsTotal; ++beat)
        {
            const double b = static_cast<double> (beat);
            const int    beatOfBar = beat % 4;

            const bool isKickDownbeat = g.fourOnFloor
                                        ? true
                                        : (beatOfBar == 0 || beatOfBar == 2);
            const bool isSnareBackbeat = (beatOfBar == 1 || beatOfBar == 3);

            if (isKickDownbeat && unit (rng) < g.kickProbBase + 0.5f * (1.0f - r.complexity))
                addNote (pattern, kKick, g.velAccent, b, 0.25);

            if (isSnareBackbeat)
            {
                const int snare = (g.trainBeat && unit (rng) < 0.3f) ? g.snareAlt : g.snareMain;
                addNote (pattern, snare, g.velAccent, b, 0.25);

                if (g.useClap)
                    addNote (pattern, g.clap, g.velAccent * 0.9f, b, 0.25);
            }

            // Kick syncopation on the "e" or "a" of each beat.
            if (unit (rng) < kickSync * 0.5f)
                addNote (pattern, kKick, g.velBase, b + 0.75, 0.25);

            // Country train beat: snare on every quarter.
            if (g.trainBeat && ! isSnareBackbeat)
                addNote (pattern, g.snareAlt, g.velBase * 0.8f, b, 0.25);

            // Metal double-kick: sixteenth-note gallop under each beat.
            if (g.doubleKick && unit (rng) < 0.4f + 0.55f * r.complexity)
            {
                addNote (pattern, kKick, g.velBase, b + 0.25, 0.25);
                addNote (pattern, kKick, g.velBase, b + 0.75, 0.25);
            }

            // Ghost snares on 16ths between backbeats.
            for (int sub = 1; sub < 4; ++sub)
            {
                if (unit (rng) < ghost * (isSnareBackbeat ? 0.3f : 1.0f))
                    addNote (pattern, g.snareMain, 0.25f + 0.25f * unit (rng),
                             b + sub * kSixteenth, 0.125);
            }

            // Open hat accent on the "and" of every other beat.
            if (! g.rideNotHat && (beatOfBar % 2 == 1) && unit (rng) < openHat)
                addNote (pattern, g.altCymbal, g.velBase * 0.9f, b + 0.5, 0.25);
        }

        // Boom-bap specifics: kick on the "and" of 2 in the first bar.
        if (g.boomBap && pattern.lengthInBeats >= 4.0)
            addNote (pattern, kKick, g.velAccent * 0.9f, 2.5, 0.25);

        return pattern;
    }

    MidiPattern AIBackend::makeFill (const GenerationRequest& r, Genre genre) const
    {
        const GenreProfile g = profileFor (genre);

        MidiPattern pattern;
        pattern.lengthInBeats = std::max (kSixteenth, r.lengthInBeats);

        std::mt19937_64 rng (r.seed);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);

        const int numSixteenths = static_cast<int> (std::round (pattern.lengthInBeats / kSixteenth));
        const int toms[] = { kHighTom, kMidTom, kLowTom };
        const int numToms = 3;

        if (! g.fillUsesRoll)
        {
            // Jazz-style fill: snare ghost triplets + ride/kick punctuation.
            for (int step = 0; step < numSixteenths; ++step)
            {
                const double beat = kSixteenth * step;
                if (unit (rng) < 0.55f + 0.35f * r.complexity)
                    addNote (pattern, g.snareMain, 0.45f + 0.35f * unit (rng), beat, 0.125);
                if (step % 3 == 0)
                    addNote (pattern, g.rideCymbal, g.velBase, beat, 0.25);
                if (unit (rng) < 0.3f)
                    addNote (pattern, kKick, g.velBase, beat, 0.25);
            }
        }
        else
        {
            // Tom roll walks down the kit proportionally to pattern length.
            for (int step = 0; step < numSixteenths; ++step)
            {
                const double beat   = kSixteenth * step;
                const int    tomIdx = std::min (numToms - 1,
                                                (step * numToms) / std::max (1, numSixteenths));
                const int    tom    = toms[tomIdx];
                addNote (pattern, tom, 0.65f + 0.25f * unit (rng), beat, 0.125);

                if (unit (rng) < 0.35f + 0.45f * r.complexity)
                    addNote (pattern, g.snareMain, 0.55f + 0.30f * unit (rng), beat, 0.125);

                if (step % 4 == 0)
                    addNote (pattern, kKick, g.velAccent, beat, 0.25);

                // Metal/metalcore: double-kick gallops under the roll.
                if (g.doubleKick)
                    addNote (pattern, kKick, g.velBase, beat + 0.125, 0.125);
            }
        }

        // Cap with a crash (or china for metal genres) + kick on beat 1.
        const int capCymbal = g.fillUsesChina ? kChina : g.crashCymbal;
        addNote (pattern, capCymbal, 1.0f, 0.0, 1.0);
        addNote (pattern, kKick,     g.velAccent, 0.0, 0.25);

        return pattern;
    }

    void AIBackend::finalize (MidiPattern& pattern, const GenerationRequest& r, std::uint64_t seed)
    {
        std::mt19937_64 rng (seed);
        std::uniform_real_distribution<float> jitter (-1.0f, 1.0f);

        const double timingJitterBeats = 0.04 * static_cast<double> (r.humanize);
        const float  velJitter         = 0.20f * r.humanize;

        for (auto& note : pattern.notes)
        {
            note.velocity  = std::clamp (note.velocity * r.velocity + velJitter * jitter (rng),
                                         0.01f, 1.0f);
            note.startBeat = std::clamp (note.startBeat + timingJitterBeats * jitter (rng),
                                         0.0, std::max (0.0, pattern.lengthInBeats - 0.001));
        }
    }
}
