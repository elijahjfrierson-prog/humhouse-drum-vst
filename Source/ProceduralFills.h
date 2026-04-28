// v1.6.1-rc.14 — Procedural MIDI fill generator. Replaces the WAV-
// derived fillLibrary() patterns at splice time.
//
// User pain (rc.13): "the fill generated leaving a whole other bar
// left behind … fills are generated stiff, blocky, off the grid and
// out of time … UNUSABLE." The baked WAV-onset fill patterns covered
// only the first 1.5 beats of the bar in many archetypes, leaving
// 2.5 beats of dead air, and hits landed on raw analyser onsets
// (not gridded subdivisions) so the fills sounded slack.
//
// Solution: generate the fill on-the-fly. Every fill is GUARANTEED
// to span the full 4 beats (0..4) with notes on musical subdivisions
// (8th / 16th / 32nd / 64th / triplet), velocity-humanized, with a
// closing crash that lands on the very last 1/64 of the bar so the
// next region's downbeat carries the resolution. Density knob (0..1)
// scales how many subdivisions are populated — low density = sparse
// 8th-note ghost roll, high density = saturated 64th-spray.
//
// 22 archetypes, ordered light → sludge so the FILL dropdown still
// reads as a complexity ramp. Names stay aligned with FillLibrary.
// generated.h so the existing dropdown/cycler logic is untouched.
#pragma once

#include "MidiPattern.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace aidrum
{
    namespace fillgen
    {
        // Canonical 8-lane MIDI map (matches DrumKit.h / DrumSynth.h).
        // v1.6.1-rc.16 — added kMidTom (45) so fills can voice across
        // FOUR distinct toms (floor 41 / low 43 / mid 45 / high 48)
        // instead of leaning on snare-only rolls. User pain (rc.15):
        // "USE THE TOMS, SNARE, HATS AND CRASHES FOR FILLS NOT JUST
        // THAT STUPID SNAREEE … ADD A 3RD TOM OR SOMETHING".
        constexpr int kKick      = 36;
        constexpr int kSnare     = 38;
        constexpr int kFloorTom  = 41;
        constexpr int kClosedHat = 42;
        constexpr int kLowTom    = 43;
        constexpr int kPedalHat  = 44;
        constexpr int kMidTom    = 45;
        constexpr int kOpenHat   = 46;
        constexpr int kHighTom   = 48;
        constexpr int kCrashL    = 49;
        constexpr int kRide      = 51;
        constexpr int kCrashR    = 57;

        constexpr double kBar = 4.0;

        struct FillCtx
        {
            MidiPattern    p;
            std::mt19937_64 rng;
            float          density   = 0.5f;
            float          intensity = 0.5f;

            float roll() { return std::uniform_real_distribution<float> (0.0f, 1.0f) (rng); }

            // Add a note clamped inside the bar. Velocity is humanized
            // by ±5% so even the densest 64th rolls don't read as a
            // mechanical click track.
            void add (int note, double beat, float vel, double len = 0.18)
            {
                if (beat < 0.0 || beat >= kBar - 1e-9) return;
                MidiNote n;
                n.noteNumber = note;
                n.startBeat  = beat;
                n.lengthBeat = len;
                const float jitter = (roll() - 0.5f) * 0.10f;
                n.velocity         = std::clamp (vel + jitter, 0.05f, 1.0f);
                p.notes.push_back (n);
            }

            // Subtle micro-drag: shift a hit forward by up to ~3% of a
            // beat. Mirrors the ghost-note "drag" the user praised in
            // expandGrooveToEightBars(). Only used on ghost layers.
            double drag (double beat)
            {
                return beat + (roll() - 0.5f) * 0.06;
            }

            // Closing crash on the very last 1/64 so the next region's
            // downbeat hosts the resolution.
            void closeCrash (float velL = 0.95f, float velR = 0.85f)
            {
                add (kCrashL, kBar - 1.0 / 16.0, velL, 0.5);
                if (intensity > 0.55f)
                    add (kCrashR, kBar - 1.0 / 16.0, velR, 0.5);
            }

            // Quarter-note kick foundation across the bar — used by
            // most archetypes so fills land in the pocket instead of
            // sounding skeletal.
            void kickFloor (float vel = 0.78f)
            {
                for (int beat = 0; beat < 4; ++beat)
                    add (kKick, (double) beat, vel - (float) beat * 0.02f, 0.22);
            }
        };

        // ── Archetype generators ─────────────────────────────────────
        // Each fills 0..4 beats. The density knob picks which extra
        // subdivision layers to overlay: density >= 0.25 adds 16ths,
        // >= 0.55 adds 32nds, >= 0.80 adds 64ths.

        // 0 — Ghost Roll: snare ghost notes on every 16th, hat shimmer
        // overlaid on the 8ths, ride pings on phrase tops, low velocity,
        // closing snare-crash. Lightest fill in the library but already
        // voiced across FOUR instruments so the fill stops collapsing
        // to a snare-monopoly the user complained about in rc.15.
        inline void ghostRoll (FillCtx& f)
        {
            for (int s = 0; s < 16; ++s)
            {
                const double beat = s * 0.25;
                if (beat >= kBar) break;
                if (f.density >= 0.25f || f.roll() < 0.6f)
                    f.add (kSnare, f.drag (beat), 0.22f + 0.04f * (s % 4 == 2 ? 1.0f : 0.0f));
            }
            // Hat ostinato on every 8th — shimmer that breathes.
            for (int s = 0; s < 8; ++s)
                f.add (kClosedHat, s * 0.5, 0.32f + 0.06f * (s % 2 == 0));
            // Ride pings on beats 1 and 3 so the bar has some sparkle.
            f.add (kRide, 0.0, 0.45f);
            f.add (kRide, 2.0, 0.50f);
            // Tom lead-in into the closing crash — mid → floor.
            f.add (kMidTom,  3.50, 0.55f);
            f.add (kFloorTom,3.75, 0.65f);
            f.add (kSnare, kBar - 0.25, 0.55f, 0.25);
            f.closeCrash (0.78f, 0.55f);
        }

        // 1 — Half-Time Snare: snare on every quarter, hat shimmer,
        // kick anchor, tom punctuation on beat 4 leading into the crash.
        inline void halfTimeSnare (FillCtx& f)
        {
            for (int b = 0; b < 4; ++b)
                f.add (kSnare, b, 0.65f + 0.04f * (b == 1 || b == 3 ? 1.0f : 0.0f));
            for (int s = 0; s < 8; ++s)
                f.add (kClosedHat, s * 0.5, 0.32f);
            // Kick on 1 and 3 keeps the pocket.
            f.add (kKick, 0.0, 0.78f);
            f.add (kKick, 2.0, 0.72f);
            // Mid → floor tom run on beat 4 instead of yet-another snare hit.
            f.add (kMidTom,  3.25, 0.72f);
            f.add (kLowTom,  3.50, 0.78f);
            f.add (kFloorTom,3.75, 0.85f);
            f.closeCrash (0.85f, 0.65f);
        }

        // 2 — Eighth Snare Roll: snare-led but with hat 16ths under it,
        // ride accents on phrase tops, and a tom drop on beat 4 so the
        // last quarter routes off the snare. Density >=0.55 doubles the
        // 16ths between the snare 8ths.
        inline void eighthSnareRoll (FillCtx& f)
        {
            for (int s = 0; s < 6; ++s)  // first 3 beats snare on 8ths
                f.add (kSnare, s * 0.5, 0.55f + 0.05f * (s % 2));
            // Closed-hat 16ths under the snare — keeps the bar busy.
            for (int s = 0; s < 12; ++s)
                f.add (kClosedHat, s * 0.25, 0.28f + 0.08f * (s % 4 == 0));
            // Ride bell on beat 1 and 3.
            f.add (kRide, 0.0, 0.55f);
            f.add (kRide, 2.0, 0.55f);
            // Beat 4 — swap snare for a high→mid→low→floor 16th descent.
            f.add (kHighTom, 3.00, 0.78f);
            f.add (kMidTom,  3.25, 0.80f);
            f.add (kLowTom,  3.50, 0.85f);
            f.add (kFloorTom,3.75, 0.92f);
            if (f.density >= 0.55f)
                for (int s = 0; s < 6; ++s)
                    f.add (kSnare, s * 0.5 + 0.25, 0.35f);
            f.closeCrash (0.92f, 0.72f);
        }

        // 3 — Sixteenth Snare Roll: snare 16ths across beats 1–2, then
        // beat 3 hands off to mid+low toms and beat 4 closes with a
        // floor-tom doublet → crash. Open-hat sizzle on the "e" of every
        // beat keeps it lively and not snare-only.
        inline void sixteenthSnareRoll (FillCtx& f)
        {
            for (int s = 0; s < 8; ++s)  // beats 1–2 snare 16ths
                f.add (kSnare, s * 0.25,
                       (s % 4 == 0 ? 0.78f : (s % 2 == 0 ? 0.62f : 0.50f)));
            // Beat 3 — mid→low tom 16ths.
            f.add (kMidTom, 2.00, 0.78f);
            f.add (kMidTom, 2.25, 0.72f);
            f.add (kLowTom, 2.50, 0.82f);
            f.add (kLowTom, 2.75, 0.78f);
            // Beat 4 — floor tom doublet leading into close.
            f.add (kFloorTom, 3.00, 0.85f);
            f.add (kFloorTom, 3.25, 0.78f);
            f.add (kSnare,    3.50, 0.78f);
            f.add (kFloorTom, 3.75, 0.95f);
            // Open-hat splash on the "e" of every beat.
            for (int b = 0; b < 4; ++b)
                f.add (kOpenHat, b + 0.25, 0.30f);
            if (f.density >= 0.80f)
                for (int s = 0; s < 16; ++s)
                    f.add (kSnare, s * 0.125 + 0.0625, 0.28f);
            f.closeCrash (0.95f, 0.80f);
        }

        // 4 — Snare Crescendo: 16ths with a real velocity ramp
        // (0.42 → 1.0). Cymbal swell underneath — closed hat on the
        // first half, open hat on the back half, ride bell on every
        // downbeat. Toms break in on beat 4 so the climax isn't all snare.
        inline void snareCrescendo (FillCtx& f)
        {
            for (int s = 0; s < 12; ++s) // beats 1–3 snare crescendo
            {
                const float t = (float) s / 11.0f; // rc.16: divisor matches loop bound so velocity ramps to 1.0
                f.add (kSnare, s * 0.25, 0.42f + t * 0.55f);
            }
            // Closed hat under beats 1–2, open hat under beat 3 (swell).
            for (int s = 0; s < 8; ++s)
                f.add ((s < 4 ? kClosedHat : kOpenHat), s * 0.25, 0.30f + 0.05f * s);
            f.add (kRide, 0.0, 0.55f);
            f.add (kRide, 1.0, 0.62f);
            f.add (kRide, 2.0, 0.72f);
            // Beat 4 — high→mid→low→floor tom 16ths into the crash.
            f.add (kHighTom, 3.00, 0.85f);
            f.add (kMidTom,  3.25, 0.88f);
            f.add (kLowTom,  3.50, 0.92f);
            f.add (kFloorTom,3.75, 1.00f);
            if (f.density >= 0.55f)
                for (int s = 0; s < 12; ++s)
                    f.add (kSnare, s * 0.25 + 0.125, 0.30f + 0.50f * (s / 11.0f));
            f.closeCrash (0.98f, 0.85f);
        }

        // 5 — Tom Descent: real four-tom descent (high → mid → low →
        // floor), two 16ths per tom across each beat. Rc.16: was a
        // three-tom descent before, the new mid voice fills the gap so
        // the cascade reads continuously instead of jumping a register.
        inline void tomDescent (FillCtx& f)
        {
            f.kickFloor (0.65f);
            for (int s = 0; s < 4; ++s)
                f.add (kHighTom,  s * 0.25,            0.78f - 0.04f * s);
            for (int s = 0; s < 4; ++s)
                f.add (kMidTom,   1.0 + s * 0.25,      0.80f - 0.04f * s);
            for (int s = 0; s < 4; ++s)
                f.add (kLowTom,   2.0 + s * 0.25,      0.82f - 0.04f * s);
            for (int s = 0; s < 4; ++s)
                f.add (kFloorTom, 3.0 + s * 0.25,      0.86f - 0.04f * s);
            // Snare on the "and" of 4 stitches the descent into the
            // close so the final floor-tom hit doesn't feel orphaned.
            f.add (kSnare, 3.5, 0.65f);
            f.closeCrash (0.95f, 0.80f);
        }

        // 6 — Tom Roll Down: 16ths, full four-tom descent across the
        // bar (high→mid→low→floor, four hits each), kick on 1, snare
        // ghost on the "a" of every beat for forward motion.
        inline void tomRollDown (FillCtx& f)
        {
            f.add (kKick, 0.0, 0.85f);
            for (int s = 0; s < 16; ++s)
            {
                const double beat = s * 0.25;
                int note;
                if      (s < 4)  note = kHighTom;
                else if (s < 8)  note = kMidTom;
                else if (s < 12) note = kLowTom;
                else             note = kFloorTom;
                f.add (note, beat, 0.66f + (s % 2 == 0 ? 0.10f : 0.0f));
            }
            // Snare ghost on every "a" — keeps the roll feeling propelled.
            for (int b = 0; b < 4; ++b)
                f.add (kSnare, b + 0.75, 0.32f);
            f.closeCrash (0.95f, 0.80f);
        }

        // 7 — Tom-Snare Alternate: snare and tom alternating on 16ths,
        // toms cycling high→mid→low→floor across the bar so the snare
        // is never repeated against the same tom voice twice in a row.
        inline void tomSnareAlternate (FillCtx& f)
        {
            f.kickFloor (0.62f);
            const int tomOrder[4] = { kHighTom, kMidTom, kLowTom, kFloorTom };
            for (int s = 0; s < 16; ++s)
            {
                const double beat = s * 0.25;
                const int note = (s % 2 == 0)
                                     ? kSnare
                                     : tomOrder[(s / 2) % 4];
                f.add (note, beat, 0.62f + 0.06f * (s % 4 == 0));
            }
            f.add (kFloorTom, 3.5,  0.85f);
            f.add (kFloorTom, 3.75, 0.90f);
            f.closeCrash (0.95f, 0.85f);
        }

        // 8 — Flam Roll: snare 16ths with grace-note flams. Beat 4
        // breaks out into a tom flam (mid + floor together) so the bar
        // doesn't end on yet-another snare. Hat 8ths underneath glue
        // the flams into the pocket.
        inline void flamRoll (FillCtx& f)
        {
            for (int s = 0; s < 12; ++s) // first 3 beats snare flams
            {
                const double beat = s * 0.25;
                // Devin Review: at s=0 the unclamped grace beat is
                // -0.0625 which FillCtx::add silently drops, so the
                // first hit sounded like a plain strike. Clamp at 0.
                f.add (kSnare, std::max (0.0, beat - 1.0 / 16.0), 0.32f, 0.10);  // grace
                f.add (kSnare, beat, 0.72f + 0.06f * (s % 4 == 0));
            }
            // Hat ostinato on every 8th — keeps the flams in the pocket.
            for (int s = 0; s < 6; ++s)
                f.add (kClosedHat, s * 0.5, 0.30f);
            // Beat 4 tom flam climax — mid + floor together, then a
            // descending tom 16th into the crash.
            f.add (kMidTom,   3.00, 0.80f);
            f.add (kFloorTom, 3.00, 0.85f);   // flam
            f.add (kLowTom,   3.25, 0.82f);
            f.add (kFloorTom, 3.50, 0.92f);
            f.add (kFloorTom, 3.75, 0.98f);
            f.closeCrash (0.95f, 0.82f);
        }

        // 9 — Snare 32nd Roll: snare 32nds for beats 1–3, beat 4 hands
        // off to a four-tom 16th descent. Open-hat sizzle on every
        // backbeat for the swell, ride bell on each downbeat.
        inline void snare32ndRoll (FillCtx& f)
        {
            for (int s = 0; s < 24; ++s) // beats 1–3 only
            {
                const float v = 0.45f + 0.30f * (s / 23.0f); // rc.16: divisor matches loop bound
                f.add (kSnare, s * 0.125, v + (s % 4 == 0 ? 0.10f : 0.0f));
            }
            // Beat 4 = full four-tom 16th descent.
            f.add (kHighTom, 3.00, 0.85f);
            f.add (kMidTom,  3.25, 0.88f);
            f.add (kLowTom,  3.50, 0.92f);
            f.add (kFloorTom,3.75, 1.00f);
            // Ride bell + open-hat splash for the swell.
            f.add (kRide,   0.0, 0.65f);
            f.add (kRide,   1.0, 0.70f);
            f.add (kRide,   2.0, 0.78f);
            f.add (kOpenHat,1.5, 0.45f);
            f.add (kOpenHat,2.5, 0.55f);
            f.closeCrash (1.0f, 0.92f);
        }

        // 10 — Snare 64th Crescendo: snare 8ths for the first half,
        // 64th cascade in beat 3, beat 4 hands off to a tom triplet
        // descent into the crash. Hat 16ths under the early bars.
        inline void snare64thCrescendo (FillCtx& f)
        {
            for (int s = 0; s < 4; ++s)
                f.add (kSnare, s * 0.5, 0.55f);
            // Hat 16ths under beats 1–2 glue.
            for (int s = 0; s < 8; ++s)
                f.add (kClosedHat, s * 0.25, 0.30f + 0.06f * (s % 4 == 0));
            // 64th cascade on beat 3 — 16 hits in 1 beat.
            for (int s = 0; s < 16; ++s)
                f.add (kSnare, 2.0 + s * 0.0625, 0.70f + 0.25f * (s / 15.0f));
            // Beat 4 — tom triplet descent (4 voices, 4 hits).
            f.add (kHighTom, 3.00, 0.85f);
            f.add (kMidTom,  3.20, 0.88f);
            f.add (kLowTom,  3.40, 0.92f);
            f.add (kFloorTom,3.60, 0.98f);
            f.add (kFloorTom,3.80, 1.00f);
            f.closeCrash (1.0f, 0.92f);
        }

        // 11 — Triplet Snare Roll: snare 8th-note triplets for beats
        // 1–3, beat 4 hands off to a four-tom triplet descent so it
        // doesn't sound like a snare drum solo.
        inline void tripletSnareRoll (FillCtx& f)
        {
            // 9 triplets across beats 1–3.
            for (int s = 0; s < 9; ++s)
            {
                const double beat = s * (1.0 / 3.0);
                f.add (kSnare, beat, 0.55f + 0.10f * (s % 3 == 0));
            }
            // Hat triplets under beats 1–2 — fattens the snare line.
            for (int s = 0; s < 6; ++s)
                f.add (kClosedHat, s * (1.0 / 3.0) + (1.0 / 6.0), 0.30f);
            // Beat 4 = tom triplets across 4 voices.
            f.add (kHighTom, 3.00, 0.82f);
            f.add (kMidTom,  3.00 + 1.0 / 3.0, 0.86f);
            f.add (kLowTom,  3.00 + 2.0 / 3.0, 0.90f);
            f.add (kFloorTom,3.95, 0.95f);
            if (f.density >= 0.55f)
                for (int s = 0; s < 18; ++s)
                    f.add (kSnare, s * (1.0 / 6.0) + (1.0 / 12.0), 0.32f);
            f.closeCrash (0.95f, 0.82f);
        }

        // 12 — Tom Triplet Descent: 12 triplets across FOUR toms now
        // (3 hits per voice instead of 4-3-5 splits). Smoother register
        // drop than rc.15.
        inline void tomTripletDescent (FillCtx& f)
        {
            f.kickFloor (0.65f);
            for (int s = 0; s < 12; ++s)
            {
                const double beat = s * (1.0 / 3.0);
                int note;
                if      (s < 3) note = kHighTom;
                else if (s < 6) note = kMidTom;
                else if (s < 9) note = kLowTom;
                else            note = kFloorTom;
                f.add (note, beat, 0.70f + 0.06f * (s % 3 == 0));
            }
            f.closeCrash (0.95f, 0.85f);
        }

        // 13 — Buzz Roll: dense randomised 32nds (snare bed) with
        // tom-and-cymbal punctuation — ride bell ticks on every beat,
        // open-hat splashes on the "and", and a tom triplet kick-out
        // on beat 4.
        inline void buzzRoll (FillCtx& f)
        {
            for (int s = 0; s < 24; ++s) // beats 1–3 buzz
            {
                const double beat = s * 0.125;
                const float  v    = 0.30f + f.roll() * 0.30f;
                f.add (kSnare, beat, v + 0.15f * (s % 4 == 0));
            }
            for (int b = 0; b < 3; ++b)
            {
                f.add (kRide,    (double) b,        0.55f);
                f.add (kOpenHat, (double) b + 0.5,  0.40f);
            }
            // Beat 4 — high→mid→low→floor 16ths instead of more snare buzz.
            f.add (kHighTom, 3.00, 0.85f);
            f.add (kMidTom,  3.25, 0.90f);
            f.add (kLowTom,  3.50, 0.95f);
            f.add (kFloorTom,3.75, 1.00f);
            f.closeCrash (1.0f, 0.90f);
        }

        // 14 — Snare-Tom-Crash: classic four-bar shape but voiced
        // across all four toms now (beat 2 high, beat 3 mid+low,
        // beat 4 floor) and an L→R crash sweep on the last beat.
        inline void snareTomCrash (FillCtx& f)
        {
            f.add (kKick,  0.0, 0.85f);
            f.add (kSnare, 0.0, 0.85f);
            f.add (kSnare, 0.5, 0.55f);
            for (int s = 0; s < 4; ++s)
                f.add (kHighTom, 1.0 + s * 0.25, 0.78f);
            for (int s = 0; s < 2; ++s)
            {
                f.add (kMidTom, 2.0 + s * 0.25,         0.82f);
                f.add (kLowTom, 2.0 + s * 0.25 + 0.125, 0.80f);
            }
            f.add (kFloorTom, 3.0, 0.88f);
            f.add (kCrashL,   3.0, 0.92f);
            f.add (kFloorTom, 3.5, 0.95f);
            f.closeCrash (1.0f, 0.92f);
        }

        // 15 — Doubled Snare: kick + snare on every 8th, but every
        // "and" alternates between a tom voice (high→mid→low→floor) so
        // the bar feels like a band fill, not a metronome.
        inline void doubledSnare (FillCtx& f)
        {
            const int tomOrder[4] = { kHighTom, kMidTom, kLowTom, kFloorTom };
            for (int s = 0; s < 8; ++s)
            {
                f.add (kSnare, s * 0.5, 0.78f);
                f.add (kKick,  s * 0.5, 0.72f);
                // Tom on every "and" — cycles through 4 voices twice.
                f.add (tomOrder[s % 4], s * 0.5 + 0.25, 0.62f);
            }
            // Open-hat shimmer on phrase tops.
            f.add (kOpenHat, 0.0, 0.45f);
            f.add (kOpenHat, 2.0, 0.55f);
            if (f.density >= 0.55f)
                for (int s = 0; s < 8; ++s)
                    f.add (kSnare, s * 0.5 + 0.125, 0.32f);
            f.closeCrash (1.0f, 0.92f);
        }

        // 16 — Sludge Tom Flare: floor-tom 8ths anchor, low/mid toms
        // weave in on the "e/and/a" 16ths, kick + crash on every
        // downbeat. Snare on every backbeat keeps the spine.
        inline void sludgeTomFlare (FillCtx& f)
        {
            for (int s = 0; s < 8; ++s)
                f.add (kFloorTom, s * 0.5, 0.85f + 0.05f * (s % 2 == 0));
            // Low + mid 16th interleave.
            for (int s = 0; s < 4; ++s)
            {
                f.add (kLowTom, s + 0.25, 0.72f);
                f.add (kMidTom, s + 0.75, 0.70f);
            }
            // Snare on the backbeats.
            f.add (kSnare, 1.0, 0.85f);
            f.add (kSnare, 3.0, 0.92f);
            for (int s = 0; s < 4; ++s)
            {
                f.add (kKick, s, 0.85f);
                f.add ((s % 2 == 0 ? kCrashL : kCrashR), s, 0.85f);
            }
            if (f.density >= 0.55f)
                for (int s = 0; s < 16; ++s)
                    f.add (kFloorTom, s * 0.25 + 0.125, 0.55f);
            f.add (kCrashL, kBar - 1.0 / 16.0, 1.0f, 0.5);
            f.add (kCrashR, kBar - 1.0 / 16.0, 0.95f, 0.5);
        }

        // 17 — Kick Buildup: kick 16ths crescendoing across the bar.
        // Snare backbeats + tom punches on phrase tops + ride bell on
        // every quarter so it's not a kick metronome.
        inline void kickBuildup (FillCtx& f)
        {
            for (int s = 0; s < 14; ++s)
            {
                const float t = (float) s / 13.0f; // rc.16: divisor matches loop bound
                f.add (kKick, s * 0.25, 0.55f + 0.40f * t);
            }
            f.add (kSnare, 1.0, 0.72f);
            f.add (kSnare, 2.0, 0.78f);
            f.add (kSnare, 3.0, 0.85f);
            for (int b = 0; b < 4; ++b)
                f.add (kRide, (double) b, 0.50f + 0.10f * b);
            // Beat 4 — mid + floor toms cap the climb.
            f.add (kMidTom,  3.50, 0.85f);
            f.add (kFloorTom,3.75, 1.00f);
            f.closeCrash (1.0f, 0.92f);
        }

        // 18 — Kick-Tom Buildup: kick + tom alternating 16ths, toms
        // cycle through ALL FOUR voices (high→mid→low→floor).
        inline void kickTomBuildup (FillCtx& f)
        {
            const int tomOrder[4] = { kHighTom, kMidTom, kLowTom, kFloorTom };
            for (int s = 0; s < 16; ++s)
            {
                const double beat = s * 0.25;
                if (s % 2 == 0)
                    f.add (kKick, beat, 0.78f + 0.10f * (s / 15.0f));
                else
                    f.add (tomOrder[(s / 2) % 4], beat,
                           0.78f + 0.10f * (s / 15.0f));
            }
            // Snare punctuation on beats 2 and 4 to stitch it together.
            f.add (kSnare, 1.0, 0.65f);
            f.add (kSnare, 3.0, 0.78f);
            f.closeCrash (1.0f, 0.92f);
        }

        // 19 — 32nd Tom Cascade: 32nds split evenly across FOUR toms
        // (8 hits per voice). Smoother register drop than the 3-tom
        // version in rc.15.
        inline void tomCascade (FillCtx& f)
        {
            f.kickFloor (0.62f);
            for (int s = 0; s < 32; ++s)
            {
                const double beat = s * 0.125;
                int note;
                if      (s < 8)  note = kHighTom;
                else if (s < 16) note = kMidTom;
                else if (s < 24) note = kLowTom;
                else             note = kFloorTom;
                const float v = 0.55f + 0.30f * (s / 31.0f);
                f.add (note, beat, v + (s % 4 == 0 ? 0.08f : 0.0f));
            }
            f.add (kCrashL, kBar - 1.0 / 16.0, 1.0f, 0.5);
            f.add (kCrashR, kBar - 1.0 / 16.0, 0.95f, 0.5);
        }

        // 20 — Crash Spray: crashes scattered on every 8th, snare
        // backbone, tom punctuation on each beat (cycles all 4 voices),
        // open-hat splash to taste.
        inline void crashSpray (FillCtx& f)
        {
            f.kickFloor (0.78f);
            const int tomOrder[4] = { kHighTom, kMidTom, kLowTom, kFloorTom };
            for (int s = 0; s < 8; ++s)
                f.add (kSnare, s * 0.5, 0.65f);
            for (int s = 0; s < 8; ++s)
                f.add ((s % 2 == 0 ? kCrashL : kCrashR), s * 0.5, 0.78f);
            // Tom on the "and" of every beat — 4 different voices.
            for (int b = 0; b < 4; ++b)
                f.add (tomOrder[b], (double) b + 0.25, 0.72f);
            f.add (kOpenHat, 1.5, 0.45f);
            f.add (kOpenHat, 3.5, 0.55f);
            if (f.density >= 0.55f)
                for (int s = 0; s < 16; ++s)
                    f.add (kSnare, s * 0.25 + 0.125, 0.32f);
            f.add (kCrashL, kBar - 1.0 / 16.0, 1.0f, 0.5);
            f.add (kCrashR, kBar - 1.0 / 16.0, 1.0f, 0.5);
        }

        // 21 — Sludge Tom Flare Heavy: most aggressive — 32nd toms
        // across all FOUR voices, doubled snare on the "and/a", 16th
        // kicks anchoring, full L→R crash spray on every beat. Open-hat
        // splashes on the backbeats for the climax.
        inline void sludgeTomFlareHeavy (FillCtx& f)
        {
            for (int s = 0; s < 32; ++s)
            {
                const double beat = s * 0.125;
                int note;
                if      (s < 8)  note = kHighTom;
                else if (s < 16) note = kMidTom;
                else if (s < 24) note = kLowTom;
                else             note = kFloorTom;
                f.add (note, beat, 0.78f + 0.15f * (s / 31.0f));
            }
            for (int s = 0; s < 16; ++s)
                f.add (kKick, s * 0.25, 0.78f);
            for (int s = 0; s < 8; ++s)
                f.add (kSnare, s * 0.5 + 0.25, 0.62f);
            for (int s = 0; s < 4; ++s)
                f.add ((s % 2 == 0 ? kCrashL : kCrashR), s, 0.85f);
            f.add (kOpenHat, 1.0, 0.55f);
            f.add (kOpenHat, 3.0, 0.65f);
            f.add (kCrashL, kBar - 1.0 / 16.0, 1.0f, 0.5);
            f.add (kCrashR, kBar - 1.0 / 16.0, 1.0f, 0.5);
        }

        // ── Dispatcher ────────────────────────────────────────────────
        // The 22 archetypes, ordered light → sludge. The FILL dropdown
        // index maps directly into this table. Out-of-range indices
        // wrap so seedy callers can pass any int safely.
        inline MidiPattern generate (int archetypeIdx,
                                     float density,
                                     float intensity,
                                     std::uint64_t seed)
        {
            FillCtx f;
            f.p.lengthInBeats = kBar;
            f.p.isFill        = true;
            f.density         = std::clamp (density,   0.0f, 1.0f);
            f.intensity       = std::clamp (intensity, 0.0f, 1.0f);
            f.rng.seed (seed ^ 0xA1F53D9C7BFE2401ULL);

            // The 22 archetype generators — in the same order as
            // FillLibrary.generated.h "Fill 10 Intensity 1" → "Fill 100
            // Intensity 22" so the dropdown labels read as a complexity
            // ramp.
            switch (((archetypeIdx % 22) + 22) % 22)
            {
                case  0: ghostRoll          (f); break;
                case  1: halfTimeSnare      (f); break;
                case  2: eighthSnareRoll    (f); break;
                case  3: sixteenthSnareRoll (f); break;
                case  4: snareCrescendo     (f); break;
                case  5: tomDescent         (f); break;
                case  6: tomRollDown        (f); break;
                case  7: tomSnareAlternate  (f); break;
                case  8: flamRoll           (f); break;
                case  9: snare32ndRoll      (f); break;
                case 10: snare64thCrescendo (f); break;
                case 11: tripletSnareRoll   (f); break;
                case 12: tomTripletDescent  (f); break;
                case 13: buzzRoll           (f); break;
                case 14: snareTomCrash      (f); break;
                case 15: doubledSnare       (f); break;
                case 16: sludgeTomFlare     (f); break;
                case 17: kickBuildup        (f); break;
                case 18: kickTomBuildup     (f); break;
                case 19: tomCascade         (f); break;
                case 20: crashSpray         (f); break;
                case 21: sludgeTomFlareHeavy(f); break;
            }

            // Velocity scale by intensity: low intensity 0.65×, high
            // intensity 1.05× (clamped). Mirrors the user's per-region
            // intensity spec: "soft pre-chorus → intense chorus".
            const float velScale = 0.65f + 0.40f * f.intensity;
            for (auto& n : f.p.notes)
                n.velocity = std::clamp (n.velocity * velScale, 0.05f, 1.0f);

            // Sort by start beat so downstream consumers see a clean
            // chronological list.
            std::sort (f.p.notes.begin(), f.p.notes.end(),
                       [] (const MidiNote& a, const MidiNote& b)
                       { return a.startBeat < b.startBeat; });

            return std::move (f.p);
        }

        // Human-readable name table (22 entries, matches generate()).
        // The FILL dropdown reads from here so users see a complexity-
        // ramped list instead of "Fill 10 Intensity 1".
        inline const char* archetypeName (int idx)
        {
            static const char* kNames[22] = {
                "Ghost Roll",            // 0
                "Half-Time Snare",       // 1
                "Eighth Snare Roll",     // 2
                "Sixteenth Snare Roll",  // 3
                "Snare Crescendo",       // 4
                "Tom Descent",           // 5
                "Tom Roll Down",         // 6
                "Tom-Snare Alternate",   // 7
                "Flam Roll",             // 8
                "Snare 32nd Roll",       // 9
                "Snare 64th Crescendo",  // 10
                "Triplet Snare Roll",    // 11
                "Tom Triplet Descent",   // 12
                "Buzz Roll",             // 13
                "Snare-Tom-Crash",       // 14
                "Doubled Snare",         // 15
                "Sludge Tom Flare",      // 16
                "Kick Buildup",          // 17
                "Kick-Tom Buildup",      // 18
                "32nd Tom Cascade",      // 19
                "Crash Spray",           // 20
                "Sludge Tom Flare Heavy" // 21
            };
            return kNames[((idx % 22) + 22) % 22];
        }
    } // namespace fillgen
} // namespace aidrum
