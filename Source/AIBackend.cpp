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

    namespace
    {
        // Apply Logic-Drummer-style swing + hi-hat override as a post-process.
        void applyDrummerPost (MidiPattern& p, const GenerationRequest& r)
        {
            // Hi-hat override: remap time-keeping cymbals to the chosen voice.
            if (r.hiHatMode != HiHatMode::Dynamic)
            {
                int target = kClosedHat;
                switch (r.hiHatMode)
                {
                    case HiHatMode::Closed:  target = kClosedHat; break;
                    case HiHatMode::Open:    target = kOpenHat;   break;
                    case HiHatMode::Ride:    target = kRide;      break;
                    case HiHatMode::Dynamic: break;
                }
                for (auto& n : p.notes)
                {
                    if (n.noteNumber == kClosedHat
                        || n.noteNumber == kOpenHat
                        || n.noteNumber == kRide
                        || n.noteNumber == kRideBell)
                    {
                        n.noteNumber = target;
                    }
                }
            }

            // Swing: shift off-beat 16ths (steps 1, 3, 5 …) forward by up to
            // a third of a sixteenth — that's the classic triplet feel.
            if (r.swing > 0.001f)
            {
                const double shift = (double) r.swing * (kSixteenth * (1.0 / 3.0));
                for (auto& n : p.notes)
                {
                    const double step = n.startBeat / kSixteenth;
                    const int    s    = static_cast<int> (std::round (step));
                    if ((s % 2) == 1 && std::abs (step - s) < 0.25)
                    {
                        n.startBeat = std::min (n.startBeat + shift,
                                                p.lengthInBeats - 0.001);
                    }
                }
            }
        }

        // Remap GM notes to the selected DrumKit and apply its velocity /
        // ghost / accent curves. Runs after the genre generator + drummer
        // post so the kit always has the final say over timbre/tone.
        void applyKit (MidiPattern& p, const GenerationRequest& r, GenerationMode effectiveMode)
        {
            const auto& kit = drumKitProfile (r.kit);

            for (auto& n : p.notes)
            {
                // Snare family first — low-velocity ghost snares become the
                // kit's ghost voice (typically sidestick / cross-stick / clap).
                if (n.noteNumber == kSnare)
                {
                    if (n.velocity <= kit.ghostThreshold
                        && kit.ghostSnare != kit.snare)
                        n.noteNumber = kit.ghostSnare;
                    else
                        n.noteNumber = kit.snare;
                }
                else if (n.noteNumber == kSideStick) n.noteNumber = kit.sideStick;
                else if (n.noteNumber == kClap)      n.noteNumber = kit.clap;
                else if (n.noteNumber == kKick)      n.noteNumber = kit.kick;
                else if (n.noteNumber == kClosedHat) n.noteNumber = kit.closedHat;
                else if (n.noteNumber == kOpenHat)   n.noteNumber = kit.openHat;
                else if (n.noteNumber == kRide)      n.noteNumber = kit.ride;
                else if (n.noteNumber == kRideBell)  n.noteNumber = kit.rideBell;
                else if (n.noteNumber == kChina)     n.noteNumber = kit.china;
                else if (n.noteNumber == kCrash)
                {
                    // Metal kits swap crash → china on Fills for the
                    // signature thrash "china accent" sound.
                    n.noteNumber = (effectiveMode == GenerationMode::Fill
                                     && kit.preferChinaForFill)
                                       ? kit.china : kit.crash;
                }
                else if (n.noteNumber == kLowTom)  n.noteNumber = kit.lowTom;
                else if (n.noteNumber == kMidTom)  n.noteNumber = kit.midTom;
                else if (n.noteNumber == kHighTom) n.noteNumber = kit.highTom;

                // Global velocity scale (metal = hot, jazz = soft).
                n.velocity = std::clamp (n.velocity * kit.velocityScale, 0.01f, 1.0f);
            }
        }
    } // namespace

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

        applyDrummerPost (pattern, seeded);
        applyKit         (pattern, seeded, r.mode);
        finalize         (pattern, seeded, seed ^ 0x9E3779B97F4A7C15ULL);
        return pattern;
    }

    // v1.6.0 \u2014 per-bundled-kit kick/snare character profile. Each of the
    // five character kits (PopRock / NuRock / AltRock / IndieLofi / Thrash)
    // gets its own backbone flavour on top of the genre-driven cymbal feel.
    //
    // NONE of these fields introduce per-bar randomness \u2014 the generator is
    // fully deterministic for a fixed (genre, kit, complexity, variation,
    // phraseBar) tuple. The ONLY thing that makes the pattern busier is the
    // COMPLEXITY knob.
    struct KitGrooveProfile
    {
        // Extra kick placements (in 16th steps across a bar, 0..15). Each is
        // only triggered above the kick's complexity threshold.
        struct KickHit { int step16; float velScale; float complexityGate; };
        std::vector<KickHit> extraKicks;

        // Snare backbeat displacement in beats (negative = lay back).
        double snareLayback = 0.0;
        // Kick backbeat displacement in beats.
        double kickLayback  = 0.0;
        // Complexity gate above which ghost snares start appearing.
        float  ghostGate    = 0.30f;
        // Ghost snare density scale.
        float  ghostScale   = 1.0f;
        // If true, kit always plays a half-time backbeat (snare on 3 only).
        bool   halfTimeFeel = false;
        // If true, layer a 16th-note double-kick gallop above complexity 0.55.
        bool   doubleKick   = false;
        // Hi-hat 1/16 threshold (complexity knob must exceed this to upgrade
        // 1/8 hat to 1/16 hat). PopRock opens up earlier than AltRock.
        float  hatSixteenthGate = 0.55f;
        // Hat eighth-note threshold (complexity knob must exceed this to
        // upgrade 1/4 hat to 1/8 hat).
        float  hatEighthGate    = 0.18f;
        // Snare alternate voice probability (sidestick / clap / rim) gated by
        // variation, not by randomness per bar.
        float  variationSideStick = 0.0f;
    };

    static KitGrooveProfile kitProfileFor (BundledKit k)
    {
        KitGrooveProfile p;
        switch (k)
        {
            case BundledKit::PopRock:
                // Classic straight backbeat: kick on 1 + 3, snare on 2 + 4.
                // As complexity rises, add the "and of 3" kick (classic pop-rock
                // pickup) then the "e of 4" ghost.
                p.extraKicks = { { 10, 0.90f, 0.45f }, { 8,  0.85f, 0.75f } };
                p.hatEighthGate    = 0.12f;
                p.hatSixteenthGate = 0.60f;
                p.ghostGate        = 0.35f;
                p.ghostScale       = 1.0f;
                break;

            case BundledKit::NuRock:
                // Syncopated: kick on 1, "and of 2", 3, "and of 3".
                p.extraKicks = { { 6,  0.95f, 0.25f },   // "and of 2"
                                 { 10, 0.90f, 0.35f },   // "and of 3"
                                 { 14, 0.80f, 0.65f } }; // "and of 4"
                p.hatEighthGate    = 0.10f;
                p.hatSixteenthGate = 0.45f;
                p.ghostGate        = 0.30f;
                p.ghostScale       = 1.1f;
                p.variationSideStick = 0.25f;
                break;

            case BundledKit::AltRock:
                // Laid-back grunge: snare pulled late, kick a hair behind,
                // sparse ghosts. Less 1/16 hat even at high complexity.
                p.extraKicks = { { 10, 0.85f, 0.55f } }; // "and of 3" comes in late
                p.snareLayback = -0.012;
                p.kickLayback  = -0.006;
                p.hatEighthGate    = 0.20f;
                p.hatSixteenthGate = 0.75f;   // rarely goes to 1/16
                p.ghostGate        = 0.45f;
                p.ghostScale       = 0.75f;
                break;

            case BundledKit::IndieLofi:
                // Half-time hip-hop feel: kick on 1, snare on 3, loads of room.
                p.extraKicks = { { 10, 0.80f, 0.50f }, // "and of 3"
                                 { 3,  0.75f, 0.60f }  // "e of 1" pickup
                               };
                p.halfTimeFeel = true;
                p.snareLayback = -0.018; // deep pocket
                p.kickLayback  = -0.004;
                p.hatEighthGate    = 0.30f;
                p.hatSixteenthGate = 0.85f;
                p.ghostGate        = 0.25f;
                p.ghostScale       = 1.2f; // ghost-heavy D'Angelo feel
                p.variationSideStick = 0.35f;
                break;

            case BundledKit::Thrash:
                // Aggressive: kick on every beat + gallop as complexity rises.
                p.extraKicks = { { 2,  0.95f, 0.10f },   // "e of 1"
                                 { 6,  0.95f, 0.15f },   // "and of 2"
                                 { 10, 0.95f, 0.20f },   // "and of 3"
                                 { 14, 0.95f, 0.25f } }; // "and of 4"
                p.doubleKick       = true;
                p.hatEighthGate    = 0.05f; // tight hats always
                p.hatSixteenthGate = 0.35f; // opens up fast
                p.ghostGate        = 0.60f;
                p.ghostScale       = 0.6f;
                break;

            case BundledKit::Count:
                break;
        }
        return p;
    }

    MidiPattern AIBackend::makeGroove (const GenerationRequest& r, Genre genre) const
    {
        const GenreProfile     g  = profileFor (genre);
        const KitGrooveProfile kp = kitProfileFor (r.bundledKit);

        MidiPattern pattern;
        pattern.lengthInBeats = std::max (kSixteenth, r.lengthInBeats);

        const int numSixteenths = static_cast<int> (std::round (pattern.lengthInBeats / kSixteenth));
        const int beatsTotal    = std::max (1, (int) std::round (pattern.lengthInBeats));

        const int timeKeeper = g.rideNotHat ? g.rideCymbal : g.mainCymbal;
        const float cx  = std::clamp (r.complexity, 0.0f, 1.0f);
        const float var = std::clamp (r.variation, 0.0f, 1.0f);
        const bool  halfTime = r.halfTime || kp.halfTimeFeel;

        // --------------------------------------------------------------
        // 1) CYMBAL LAYER \u2014 accents only, rules-based.
        //
        // User rule (v1.6.0): "the cymbals should not be so randomized and
        // constant. only based on complexity should they hit. focus on 1-bar,
        // 1/2-bar and 1/4 hits for all cymbals. The user can edit the rest
        // in MIDI."
        //
        // Mapping (deterministic, no RNG):
        //   - cx  < hatEighthGate       : hat on every 1/4 (downbeats only)
        //   - cx  < hatSixteenthGate    : hat on every 1/8
        //   - cx >= hatSixteenthGate    : hat on every 1/16
        // Open-hat / ride-bell accents fall on 1/2-bar boundaries as variation
        // rises. A crash lands on the first downbeat if this is the first
        // region of a phrase (phraseBar % 8 == 0).
        // --------------------------------------------------------------
        int hatDivisor = 4; // 1 hat per quarter
        if (cx >= kp.hatEighthGate)    hatDivisor = 2; // 1/8
        if (cx >= kp.hatSixteenthGate) hatDivisor = 1; // 1/16

        if (g.triplets)
        {
            // Jazz ride stays on triplets regardless (characteristic voicing).
            for (int beat = 0; beat < beatsTotal; ++beat)
            {
                const double b = static_cast<double> (beat);
                addNote (pattern, g.rideCymbal, g.velBase, b, kEighth);
                if ((beat % 2) == 1)
                {
                    addNote (pattern, g.rideCymbal, g.velBase * 0.75f, b + 2.0 / 3.0, 1.0 / 3.0);
                    addNote (pattern, kRideBell,    g.velBase * 0.5f,  b + 1.0 / 3.0, 1.0 / 3.0);
                }
            }
        }
        else
        {
            for (int step = 0; step < numSixteenths; step += hatDivisor)
            {
                const double beat = kSixteenth * step;
                // Accent downbeats (every 1/4) a bit harder than off-beats.
                const bool isQuarterAccent = (step % 4) == 0;
                const float vel = isQuarterAccent ? g.velAccent * 0.85f
                                                  : g.velBase  * 0.75f;
                addNote (pattern, timeKeeper, vel, beat,
                         hatDivisor == 1 ? 0.0625 : (hatDivisor == 2 ? 0.125 : 0.25));
            }

            // 1/2-bar open-hat accent (on the "and of 2" of each bar), gated by
            // complexity + variation. Only a single hit per half-bar.
            if (! g.rideNotHat && cx > 0.4f)
            {
                for (int barStart = 0; barStart < numSixteenths; barStart += 16)
                {
                    const double beat = kSixteenth * (barStart + 6); // and-of-2
                    if (beat < pattern.lengthInBeats)
                        addNote (pattern, g.altCymbal, g.velBase * 0.9f, beat, 0.25);
                }
            }

            // 1-bar crash accent: first downbeat of the first bar of a phrase.
            if ((r.phraseBar % 8) == 0 && pattern.lengthInBeats >= 2.0)
                addNote (pattern, g.crashCymbal, g.velAccent, 0.0, 1.0);
        }

        // --------------------------------------------------------------
        // 2) KICK / SNARE BACKBONE \u2014 deterministic "boom boom bap".
        //
        // User rule: "all instruments besides toms should consistently stay
        // with 1/4 and 1-bar hits with the ability to add the complexity to
        // make it turn into 1/16 hit. you should never start fast but a
        // simple pattern like 'boom boom bap'."
        //
        // Mapping:
        //   - Base: kick on beats 0 & 2, snare on beats 1 & 3 (half-time: kick
        //     on 0 only, snare on 2 only).
        //   - COMPLEXITY gates additional kicks at per-kit 1/16 positions
        //     (see kitProfileFor) and ghost snare notes.
        //   - Per-kit laybacks shift kick/snare a hair early or late.
        // --------------------------------------------------------------
        const double kickLay  = kp.kickLayback;
        const double snareLay = kp.snareLayback;

        for (int beat = 0; beat < beatsTotal; ++beat)
        {
            const double b         = static_cast<double> (beat);
            const int    beatOfBar = beat % 4;

            bool isKickDownbeat  = (beatOfBar == 0 || beatOfBar == 2);
            bool isSnareBackbeat = (beatOfBar == 1 || beatOfBar == 3);
            if (halfTime)
            {
                isKickDownbeat  = (beatOfBar == 0);
                isSnareBackbeat = (beatOfBar == 2);
            }
            if (g.fourOnFloor) isKickDownbeat = true;

            if (isKickDownbeat)
                addNote (pattern, kKick, g.velAccent, b + kickLay, 0.25);

            if (isSnareBackbeat)
            {
                const bool sideStickPick = (var >= 0.65f) && (kp.variationSideStick > 0.0f)
                                            && ((beat % 8) == 1);
                const int snareNote = sideStickPick ? g.snareAlt : g.snareMain;
                addNote (pattern, snareNote, g.velAccent, b + snareLay, 0.25);
                if (g.useClap)
                    addNote (pattern, g.clap, g.velAccent * 0.9f, b + snareLay, 0.25);
            }
        }

        // Per-kit extra kicks: placed at quantised 1/16 steps and gated by
        // complexity. No RNG \u2014 strictly rules-based.
        for (const auto& k : kp.extraKicks)
        {
            if (cx < k.complexityGate) continue;
            for (int bar = 0; bar < numSixteenths; bar += 16)
            {
                const int step = bar + k.step16;
                if (step >= numSixteenths) break;
                const double beat = kSixteenth * step;
                addNote (pattern, kKick, g.velBase * k.velScale, beat + kickLay, 0.125);
            }
        }

        // Double-kick gallop (Thrash only): at high complexity, fill in the
        // "e" and "a" positions of every beat.
        if (kp.doubleKick && cx > 0.55f)
        {
            for (int beat = 0; beat < beatsTotal; ++beat)
            {
                const double b = static_cast<double> (beat);
                addNote (pattern, kKick, g.velBase * 0.85f, b + kSixteenth,     0.125);
                addNote (pattern, kKick, g.velBase * 0.85f, b + kSixteenth * 3, 0.125);
            }
        }

        // Snare ghost notes: appear deterministically once complexity crosses
        // the kit's ghostGate. Placement = "e" + "a" of every beat, attenuated.
        if (cx > kp.ghostGate)
        {
            const float ghostVel = 0.25f + 0.25f * (cx - kp.ghostGate) / std::max (0.05f, 1.0f - kp.ghostGate);
            const float scaled   = ghostVel * kp.ghostScale;
            for (int beat = 0; beat < beatsTotal; ++beat)
            {
                const int beatOfBar = beat % 4;
                const bool isBackbeat = halfTime ? (beatOfBar == 2)
                                                 : (beatOfBar == 1 || beatOfBar == 3);
                if (isBackbeat) continue; // don't ghost over the main snare hit
                const double b = static_cast<double> (beat);
                addNote (pattern, g.snareMain, scaled, b + kSixteenth,     0.125);
                if (cx > kp.ghostGate + 0.25f)
                    addNote (pattern, g.snareMain, scaled * 0.8f, b + kSixteenth * 3, 0.125);
            }
        }

        return pattern;
    }

    // v1.3.0 Rich fill bank.
    //
    // Instead of two generic "roll vs jazz ghost" fills, the generator now
    // picks from 10 distinct fill templates modelled on how real session
    // drummers actually play. Template choice is driven by (phraseBar, seed,
    // complexity) so the same groove never fills the same way twice, and
    // bars 7/8/16/24 of a phrase will sound fresh every loop-around.
    //
    // Every fill ends with a crash/china + downbeat kick so the next region
    // always lands cleanly, matching how Nirvana/Deftones/Superheaven drums
    // resolve their fills into the next verse.
    enum class FillTemplate
    {
        QuarterTriplets,       // 1 bar: 1/4 triplets across toms
        SixteenthHerta,        // 1 bar: 16th snare herta (LRLL RLRR) across kit
        FlamAccent,            // 1 bar: rudiment-style flams on 2+4
        TomsAndGhosts,         // 1 bar: tom roll with snare ghost filler
        SnareRollCrescendo,    // 1 bar: 32nd snare roll building in velocity
        OffbeatChinaStabs,     // 1/2 bar: china hits on offbeats
        HatToCrashBuild,       // 1/2 bar: open-hat then crash lead-in
        LinearKickSnareTom,    // 1 bar: no two hits at same time — linear
        JazzRideComping,       // 1 bar: ride comping + kick punctuation
        TrapHatRoll            // 1/2 bar: 32nd-note hat roll + kick
    };

    MidiPattern AIBackend::makeFill (const GenerationRequest& r, Genre genre) const
    {
        const GenreProfile g = profileFor (genre);

        MidiPattern pattern;
        pattern.lengthInBeats = std::max (kSixteenth, r.lengthInBeats);

        std::mt19937_64 rng (r.seed ^ (0xABCDEF123456789ULL
                                       + static_cast<std::uint64_t> (r.phraseBar) * 0x9E3779B97F4A7C15ULL));
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);

        const int numSixteenths = static_cast<int> (std::round (pattern.lengthInBeats / kSixteenth));
        const int toms[] = { kHighTom, kMidTom, kLowTom };
        const int numToms = 3;

        // Build a candidate pool biased by genre + complexity.
        std::vector<FillTemplate> pool;
        pool.push_back (FillTemplate::TomsAndGhosts);
        pool.push_back (FillTemplate::SnareRollCrescendo);
        pool.push_back (FillTemplate::LinearKickSnareTom);
        if (r.fillComplexity > 0.3f) pool.push_back (FillTemplate::SixteenthHerta);
        if (r.fillComplexity > 0.5f) pool.push_back (FillTemplate::FlamAccent);
        if (r.fillComplexity > 0.45f) pool.push_back (FillTemplate::QuarterTriplets);
        pool.push_back (FillTemplate::HatToCrashBuild);
        if (g.fillUsesChina) pool.push_back (FillTemplate::OffbeatChinaStabs);
        if (g.triplets || genre == Genre::Jazz) pool.push_back (FillTemplate::JazzRideComping);
        if (g.trapHats) pool.push_back (FillTemplate::TrapHatRoll);

        // On bar-boundary fills we bias toward intricate templates; mid-phrase
        // pickups stay simpler.
        const bool isPhraseCap = (r.phraseBar % 8) == 7 || (r.phraseBar % 8) == 3;
        if (isPhraseCap && r.fillComplexity > 0.4f)
        {
            pool.push_back (FillTemplate::SnareRollCrescendo);
            pool.push_back (FillTemplate::FlamAccent);
            if (g.fillUsesChina) pool.push_back (FillTemplate::OffbeatChinaStabs);
        }

        const auto templ = pool[std::uniform_int_distribution<size_t> (0, pool.size() - 1) (rng)];

        auto randomTom = [&](float bias) -> int
        {
            // bias in [0,1]: 0 = high tom heavy, 1 = floor tom heavy
            const float pick = std::clamp (bias + 0.4f * (unit (rng) - 0.5f), 0.0f, 0.999f);
            return toms[static_cast<int> (pick * numToms)];
        };

        switch (templ)
        {
            case FillTemplate::QuarterTriplets:
            {
                // 1/4-note triplets across the toms: 3 hits per beat.
                const int beats = std::max (1, (int) std::round (pattern.lengthInBeats));
                for (int beat = 0; beat < beats; ++beat)
                    for (int t = 0; t < 3; ++t)
                    {
                        const float bias = (beat / (float) beats) * 0.7f + 0.15f;
                        addNote (pattern, randomTom (bias),
                                 0.7f + 0.2f * unit (rng),
                                 beat + t / 3.0, 1.0 / 3.0);
                    }
                if (unit (rng) < 0.5f)
                    for (int beat = 0; beat < beats; beat += 2)
                        addNote (pattern, kKick, g.velBase, beat, 0.25);
                break;
            }

            case FillTemplate::SixteenthHerta:
            {
                // Herta rudiment: snare + kick doubles across the bar.
                for (int step = 0; step < numSixteenths; ++step)
                {
                    const double beat = kSixteenth * step;
                    const int s16 = step % 4;
                    const float base = 0.55f + 0.3f * unit (rng);
                    // LRLL RLRR — snare/tom alternation
                    if (s16 == 0)      addNote (pattern, g.snareMain, base, beat, 0.125);
                    else if (s16 == 1) addNote (pattern, g.snareMain, base * 0.85f, beat, 0.125);
                    else if (s16 == 2) addNote (pattern, randomTom (step / (float) numSixteenths),
                                                base, beat, 0.125);
                    else               addNote (pattern, randomTom (step / (float) numSixteenths),
                                                base * 0.9f, beat, 0.125);

                    if (step % 4 == 0) addNote (pattern, kKick, g.velAccent, beat, 0.25);
                }
                break;
            }

            case FillTemplate::FlamAccent:
            {
                // Rudiment flam-accent: grace-note pairs leading to accented hits.
                const int beats = std::max (1, (int) std::round (pattern.lengthInBeats));
                for (int beat = 0; beat < beats; ++beat)
                {
                    const double b = beat;
                    // Grace note 18ms before the accent (~1/48 beat)
                    addNote (pattern, g.snareMain, 0.35f, b - 0.04, 0.0625);
                    addNote (pattern, g.snareMain, 0.95f, b, 0.25);
                    // Floor tom on the "and"
                    if (beat % 2 == 0)
                        addNote (pattern, kLowTom, 0.7f + 0.2f * unit (rng), b + 0.5, 0.25);
                    if (unit (rng) < 0.6f)
                        addNote (pattern, kKick, g.velBase, b, 0.25);
                }
                break;
            }

            case FillTemplate::TomsAndGhosts:
            {
                // Classic tom-around with snare ghosts woven in.
                for (int step = 0; step < numSixteenths; ++step)
                {
                    const double beat = kSixteenth * step;
                    const float prog = step / (float) std::max (1, numSixteenths);
                    addNote (pattern, randomTom (prog),
                             0.7f + 0.25f * unit (rng), beat, 0.125);
                    if (unit (rng) < 0.45f + 0.4f * r.fillComplexity)
                        addNote (pattern, g.snareMain, 0.25f + 0.25f * unit (rng),
                                 beat + 0.0625, 0.0625);
                    if (step % 4 == 0) addNote (pattern, kKick, g.velAccent, beat, 0.25);
                }
                break;
            }

            case FillTemplate::SnareRollCrescendo:
            {
                // 32nd-note snare roll with velocity ramp from pp to ff.
                const int steps32 = numSixteenths * 2;
                for (int s = 0; s < steps32; ++s)
                {
                    const double beat = s * 0.125;
                    const float  t    = s / (float) std::max (1, steps32 - 1);
                    const float  vel  = 0.30f + 0.65f * t;   // crescendo
                    addNote (pattern, g.snareMain, vel, beat, 0.0625);
                }
                addNote (pattern, kKick, g.velAccent, 0.0, 0.25);
                break;
            }

            case FillTemplate::OffbeatChinaStabs:
            {
                // Metal/metalcore china stabs on "and of 1 / 2 / 3 / 4".
                const int beats = std::max (1, (int) std::round (pattern.lengthInBeats));
                for (int beat = 0; beat < beats; ++beat)
                {
                    addNote (pattern, kKick, g.velAccent, beat, 0.25);
                    addNote (pattern, kChina, 0.9f + 0.1f * unit (rng), beat + 0.5, 0.5);
                    if (g.doubleKick) addNote (pattern, kKick, g.velBase, beat + 0.5, 0.25);
                    addNote (pattern, g.snareMain, 0.5f + 0.3f * unit (rng), beat + 0.75, 0.125);
                }
                break;
            }

            case FillTemplate::HatToCrashBuild:
            {
                // Open hats rise, crash lands at end — classic pickup.
                const int beats = std::max (1, (int) std::round (pattern.lengthInBeats));
                for (int step = 0; step < numSixteenths - 1; ++step)
                {
                    const double beat = kSixteenth * step;
                    const float t = step / (float) std::max (1, numSixteenths - 1);
                    addNote (pattern, g.altCymbal, 0.45f + 0.5f * t, beat, 0.125);
                    if (step % 2 == 0) addNote (pattern, kKick, g.velBase, beat, 0.25);
                    if (step % 4 == 3) addNote (pattern, g.snareMain, 0.8f, beat, 0.125);
                }
                (void) beats;
                break;
            }

            case FillTemplate::LinearKickSnareTom:
            {
                // Linear fill: no two drums at the same time — kick, snare,
                // tom alternating 16ths Gavin-Harrison-style.
                int cycle[4] = { 0, 1, 2, 1 }; // K S T S
                for (int step = 0; step < numSixteenths; ++step)
                {
                    const double beat = kSixteenth * step;
                    const int k = cycle[step % 4];
                    const float vel = 0.7f + 0.25f * unit (rng);
                    if (k == 0) addNote (pattern, kKick, g.velAccent, beat, 0.125);
                    else if (k == 1) addNote (pattern, g.snareMain, vel, beat, 0.125);
                    else addNote (pattern, randomTom (step / (float) numSixteenths), vel, beat, 0.125);
                }
                break;
            }

            case FillTemplate::JazzRideComping:
            {
                // Jazz fill: ride spang-a-lang plus snare comp + kick punctuation.
                const int beats = std::max (1, (int) std::round (pattern.lengthInBeats));
                for (int beat = 0; beat < beats; ++beat)
                {
                    const double b = beat;
                    addNote (pattern, g.rideCymbal, g.velBase, b, 0.5);
                    addNote (pattern, g.rideCymbal, g.velBase * 0.7f, b + 2.0 / 3.0, 1.0 / 3.0);
                    if (unit (rng) < 0.65f)
                        addNote (pattern, g.snareMain, 0.4f + 0.3f * unit (rng),
                                 b + 1.0 / 3.0, 1.0 / 3.0);
                    if (unit (rng) < 0.4f)
                        addNote (pattern, kKick, g.velBase * 0.8f, b + 0.5, 0.25);
                }
                break;
            }

            case FillTemplate::TrapHatRoll:
            {
                // Trap: rolling 32nd hats, 808 kick pattern, rim snare on 3.
                const int steps32 = numSixteenths * 2;
                for (int s = 0; s < steps32; ++s)
                {
                    const double beat = s * 0.125;
                    const float t = (s % 8) / 8.0f;
                    const float vel = 0.55f + 0.3f * t;
                    addNote (pattern, g.mainCymbal, vel, beat, 0.0625);
                }
                addNote (pattern, kKick, g.velAccent, 0.0, 0.25);
                addNote (pattern, kKick, g.velAccent * 0.9f, 0.75, 0.25);
                addNote (pattern, g.snareMain, 0.85f, 1.0, 0.25);
                break;
            }
        }

        // Cap with a crash (or china for metal genres) on downbeat + kick.
        const int capCymbal = g.fillUsesChina && (r.phraseBar % 4 == 3) ? kChina : g.crashCymbal;
        addNote (pattern, capCymbal, 1.0f, 0.0, 1.0);
        if (pattern.notes.empty() || templ != FillTemplate::SnareRollCrescendo)
            addNote (pattern, kKick, g.velAccent, 0.0, 0.25);

        return pattern;
    }

    void AIBackend::finalize (MidiPattern& pattern, const GenerationRequest& r, std::uint64_t seed)
    {
        // v1.3.0 Asymmetric per-drum human-feel engine.
        //
        // Real drummers don't apply uniform random jitter across all limbs.
        // Hi-hats tend to push slightly ahead of the click; kicks sit right
        // on or just behind; the snare backbeat gets pulled late for a
        // "fatter" feel; toms vary most. Ghost notes drop out occasionally.
        // Flams (grace notes ~20ms before accented snares) add wrist-style
        // wobble. Velocity breathes across 8-bar phrases.
        std::mt19937_64 rng (seed);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);

        struct Offset { double push; double spread; };
        auto drumOffset = [] (int note) -> Offset
        {
            // push in beats (negative = pulls late, positive = pushes early)
            switch (note)
            {
                case kKick:      return { -0.002, 0.010 }; // behind the beat, tight
                case kSnare:     return { -0.012, 0.018 }; // pulls late, looser
                case kSideStick: return { -0.006, 0.012 };
                case kClap:      return { -0.004, 0.014 };
                case kClosedHat: return {  0.004, 0.014 }; // pushes slightly ahead
                case kOpenHat:   return {  0.006, 0.018 };
                case kRide:      return {  0.002, 0.016 };
                case kRideBell:  return {  0.000, 0.014 };
                case kCrash:     return { -0.001, 0.010 };
                case kChina:     return { -0.002, 0.012 };
                case kHighTom:
                case kMidTom:
                case kLowTom:    return { -0.004, 0.022 }; // toms have the most wrist wobble
                default:         return {  0.000, 0.016 };
            }
        };

        const float hz  = std::clamp (r.humanize, 0.0f, 1.0f);
        const float velScale   = 0.95f + 0.25f * hz;    // humanize opens up velocity range
        const float timingGain = 0.4f + 1.6f * hz;      // humanize amplifies jitter
        const float ghostDropoutProb = 0.05f + 0.08f * hz;

        // v1.6.0 \u2014 VELOCITY knob is now a true "bedroom \u2192 stadium" dial.
        //
        // User rule: "VELOCITY 0 = bedroom whisper playing, 100 = stadium
        // crashing playing". We remap r.velocity (0..1) onto a curve that
        // significantly compresses at the low end (jazz-brush territory)
        // and overdrives slightly at the top. Applied BEFORE the phrase-level
        // velScale / phraseVel / humanize jitter so those still breathe on
        // top of the master intensity.
        const float intensity = std::clamp (r.velocity, 0.0f, 1.0f);
        const float velFloor  = 0.27f;                   // ~35 MIDI velocity
        const float velCeil   = 1.05f;                   // slight overdrive
        const float intensityCurve = velFloor + (velCeil - velFloor) * intensity;

        // Phrase-level velocity breathing: crescendo/decrescendo across 8-bar
        // phrases. Drummers don't play every bar the same dynamic; they rise
        // into fills and lay back mid-phrase.
        const int phraseMod = r.phraseBar % 8;
        float phraseVel = 1.0f;
        if (phraseMod == 0) phraseVel = 0.92f;               // settle in
        else if (phraseMod == 1) phraseVel = 0.95f;
        else if (phraseMod == 2) phraseVel = 0.97f;
        else if (phraseMod == 3) phraseVel = 1.00f;          // mid-phrase
        else if (phraseMod == 4) phraseVel = 0.96f;          // breathe
        else if (phraseMod == 5) phraseVel = 0.99f;
        else if (phraseMod == 6) phraseVel = 1.02f;          // build
        else                       phraseVel = 1.06f;        // crest before cap/fill

        // Flam probability on accented snares: grace note ~20ms before hit.
        const float flamProb = 0.08f + 0.10f * hz;

        // Build a flam buffer separately so we don't iterate while pushing.
        std::vector<MidiNote> flams;

        for (auto it = pattern.notes.begin(); it != pattern.notes.end();)
        {
            auto& note = *it;
            const Offset off = drumOffset (note.noteNumber);

            // Asymmetric timing: base push + zero-centred spread scaled by humanize.
            const double j = static_cast<double> (unit (rng)) - 0.5;
            const double shift = (off.push + off.spread * 2.0 * j) * timingGain;

            note.velocity  = std::clamp (note.velocity * intensityCurve * phraseVel * velScale
                                          + 0.10f * hz * (unit (rng) - 0.5f),
                                         0.01f, 1.0f);
            note.startBeat = std::clamp (note.startBeat + shift,
                                         0.0, std::max (0.0, pattern.lengthInBeats - 0.001));

            // Flam: accented snares get a pre-hit ghost ~20ms earlier.
            if (note.noteNumber == kSnare && note.velocity > 0.85f && unit (rng) < flamProb)
            {
                flams.push_back ({ kSnare,
                                   std::clamp (note.velocity * 0.35f, 0.01f, 1.0f),
                                   std::max (0.0, note.startBeat - 0.025),
                                   0.0625 });
            }

            // Ghost-note dropout: occasionally a very quiet snare gets missed.
            if (note.noteNumber == kSnare && note.velocity < 0.40f && unit (rng) < ghostDropoutProb)
            {
                it = pattern.notes.erase (it);
                continue;
            }

            ++it;
        }

        pattern.notes.insert (pattern.notes.end(), flams.begin(), flams.end());
    }
}
