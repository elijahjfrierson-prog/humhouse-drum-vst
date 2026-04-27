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
        constexpr int kKick      = 36;
        constexpr int kSnare     = 38;
        constexpr int kFloorTom  = 41;
        constexpr int kClosedHat = 42;
        constexpr int kLowTom    = 43;
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

        // 0 — Ghost Roll: snare ghost notes on every 16th, low velocity,
        // closing snare-crash. Lightest fill in the library.
        inline void ghostRoll (FillCtx& f)
        {
            for (int s = 0; s < 16; ++s)
            {
                const double beat = s * 0.25;
                if (beat >= kBar) break;
                if (f.density >= 0.25f || f.roll() < 0.6f)
                    f.add (kSnare, f.drag (beat), 0.22f + 0.04f * (s % 4 == 2 ? 1.0f : 0.0f));
            }
            f.add (kSnare, kBar - 0.25, 0.55f, 0.25);
            f.closeCrash (0.78f, 0.55f);
        }

        // 1 — Half-Time Snare: snare on every quarter, light hat
        // shimmer, closing crash.
        inline void halfTimeSnare (FillCtx& f)
        {
            for (int b = 0; b < 4; ++b)
                f.add (kSnare, b, 0.65f + 0.04f * (b == 1 || b == 3 ? 1.0f : 0.0f));
            for (int s = 0; s < 8; ++s)
                f.add (kClosedHat, s * 0.5, 0.32f);
            f.closeCrash (0.85f, 0.65f);
        }

        // 2 — Eighth Snare Roll: snare on every 8th, simple lead-in.
        inline void eighthSnareRoll (FillCtx& f)
        {
            for (int s = 0; s < 8; ++s)
                f.add (kSnare, s * 0.5, 0.55f + 0.05f * (s % 2));
            if (f.density >= 0.55f)
                for (int s = 0; s < 8; ++s)
                    f.add (kSnare, s * 0.5 + 0.25, 0.35f);
            f.closeCrash (0.92f, 0.72f);
        }

        // 3 — Sixteenth Snare Roll: 16ths across the bar with
        // subtle accent on each downbeat.
        inline void sixteenthSnareRoll (FillCtx& f)
        {
            for (int s = 0; s < 16; ++s)
                f.add (kSnare, s * 0.25,
                       (s % 4 == 0 ? 0.78f : (s % 2 == 0 ? 0.62f : 0.50f)));
            if (f.density >= 0.80f)
                for (int s = 0; s < 32; ++s)
                    f.add (kSnare, s * 0.125 + 0.0625, 0.28f);
            f.closeCrash (0.95f, 0.80f);
        }

        // 4 — Snare 16th Crescendo: 16ths, velocity ramps 0.40 → 1.0.
        inline void snareCrescendo (FillCtx& f)
        {
            for (int s = 0; s < 16; ++s)
            {
                const float t = (float) s / 15.0f;
                f.add (kSnare, s * 0.25, 0.42f + t * 0.55f);
            }
            if (f.density >= 0.55f)
                for (int s = 0; s < 16; ++s)
                    f.add (kSnare, s * 0.25 + 0.125, 0.30f + 0.50f * (s / 15.0f));
            f.closeCrash (0.98f, 0.85f);
        }

        // 5 — Tom Descent: high → mid → floor across 4 beats, two
        // 16ths per tom.
        inline void tomDescent (FillCtx& f)
        {
            f.kickFloor (0.65f);
            // Beat 0..1 high tom, beat 1..2 high→low, beat 2..3 low→floor,
            // beat 3..4 floor with snare lead-in.
            for (int s = 0; s < 4; ++s)
                f.add (kHighTom, s * 0.25, 0.78f - 0.04f * s);
            for (int s = 0; s < 4; ++s)
                f.add (kLowTom, 1.0 + s * 0.25, 0.78f - 0.04f * s);
            for (int s = 0; s < 4; ++s)
                f.add (kFloorTom, 2.0 + s * 0.25, 0.82f - 0.04f * s);
            f.add (kSnare, 3.0, 0.70f);
            f.add (kSnare, 3.5, 0.78f);
            f.closeCrash (0.95f, 0.80f);
        }

        // 6 — Tom Roll Down: 16ths, high→low→floor with kick on 1.
        inline void tomRollDown (FillCtx& f)
        {
            f.add (kKick, 0.0, 0.85f);
            for (int s = 0; s < 16; ++s)
            {
                const double beat = s * 0.25;
                int note = kHighTom;
                if (s >= 6 && s < 11) note = kLowTom;
                else if (s >= 11)     note = kFloorTom;
                f.add (note, beat, 0.66f + (s % 2 == 0 ? 0.10f : 0.0f));
            }
            f.closeCrash (0.95f, 0.80f);
        }

        // 7 — Tom-Snare Alternate: snare and tom alternating on 16ths.
        inline void tomSnareAlternate (FillCtx& f)
        {
            f.kickFloor (0.62f);
            for (int s = 0; s < 16; ++s)
            {
                const double beat = s * 0.25;
                const int note = (s % 2 == 0) ? kSnare : (s < 8 ? kHighTom : kLowTom);
                f.add (note, beat, 0.62f + 0.06f * (s % 4 == 0));
            }
            f.add (kFloorTom, 3.5, 0.85f);
            f.add (kFloorTom, 3.75, 0.90f);
            f.closeCrash (0.95f, 0.85f);
        }

        // 8 — Flam Roll: snare on every 16th with grace-note flam (a
        // ghost snare 1/64 ahead).
        inline void flamRoll (FillCtx& f)
        {
            for (int s = 0; s < 16; ++s)
            {
                const double beat = s * 0.25;
                f.add (kSnare, beat - 1.0 / 16.0, 0.32f, 0.10);  // grace
                f.add (kSnare, beat, 0.72f + 0.06f * (s % 4 == 0));
            }
            f.closeCrash (0.95f, 0.82f);
        }

        // 9 — Snare 32nd Roll: 32nds across the bar.
        inline void snare32ndRoll (FillCtx& f)
        {
            for (int s = 0; s < 32; ++s)
            {
                const float v = 0.45f + 0.30f * (s / 31.0f);
                f.add (kSnare, s * 0.125, v + (s % 4 == 0 ? 0.10f : 0.0f));
            }
            f.closeCrash (1.0f, 0.92f);
        }

        // 10 — Snare 64th Crescendo: last beat goes 64ths.
        inline void snare64thCrescendo (FillCtx& f)
        {
            for (int s = 0; s < 8; ++s)
                f.add (kSnare, s * 0.5, 0.55f);
            for (int s = 0; s < 16; ++s)
                f.add (kSnare, 2.0 + s * 0.0625, 0.70f + 0.25f * (s / 15.0f));
            f.closeCrash (1.0f, 0.92f);
        }

        // 11 — Triplet Snare Roll: 8th-note triplets across the bar.
        inline void tripletSnareRoll (FillCtx& f)
        {
            // 12 8th-note triplets per bar (3 per beat).
            for (int s = 0; s < 12; ++s)
            {
                const double beat = s * (1.0 / 3.0);
                f.add (kSnare, beat, 0.55f + 0.10f * (s % 3 == 0));
            }
            if (f.density >= 0.55f)
                for (int s = 0; s < 24; ++s)
                    f.add (kSnare, s * (1.0 / 6.0) + (1.0 / 12.0), 0.32f);
            f.closeCrash (0.95f, 0.82f);
        }

        // 12 — Tom Triplet Descent: triplets across high→low→floor.
        inline void tomTripletDescent (FillCtx& f)
        {
            f.kickFloor (0.65f);
            for (int s = 0; s < 12; ++s)
            {
                const double beat = s * (1.0 / 3.0);
                int note = (s < 4) ? kHighTom : (s < 8 ? kLowTom : kFloorTom);
                f.add (note, beat, 0.70f + 0.06f * (s % 3 == 0));
            }
            f.closeCrash (0.95f, 0.85f);
        }

        // 13 — Buzz Roll: dense randomised 32nds with low velocity.
        inline void buzzRoll (FillCtx& f)
        {
            for (int s = 0; s < 32; ++s)
            {
                const double beat = s * 0.125;
                const float  v    = 0.30f + f.roll() * 0.30f;
                f.add (kSnare, beat, v + 0.15f * (s % 4 == 0));
            }
            f.add (kSnare, 3.875, 0.95f);
            f.closeCrash (1.0f, 0.90f);
        }

        // 14 — Snare-Tom-Crash: snare beat 1, tom beats 2-3, crash 4.
        inline void snareTomCrash (FillCtx& f)
        {
            f.add (kKick, 0.0, 0.85f);
            f.add (kSnare, 0.0, 0.85f);
            f.add (kSnare, 0.5, 0.55f);
            for (int s = 0; s < 4; ++s)
                f.add (kHighTom, 1.0 + s * 0.25, 0.78f);
            for (int s = 0; s < 4; ++s)
                f.add (kLowTom, 2.0 + s * 0.25, 0.80f);
            f.add (kFloorTom, 3.0, 0.85f);
            f.add (kFloorTom, 3.5, 0.92f);
            f.closeCrash (1.0f, 0.92f);
        }

        // 15 — Doubled Snare: snare on every 8th, kick doubled with snare.
        inline void doubledSnare (FillCtx& f)
        {
            for (int s = 0; s < 8; ++s)
            {
                f.add (kSnare, s * 0.5, 0.78f);
                f.add (kKick,  s * 0.5, 0.72f);
            }
            if (f.density >= 0.55f)
                for (int s = 0; s < 8; ++s)
                    f.add (kSnare, s * 0.5 + 0.25, 0.45f);
            f.closeCrash (1.0f, 0.92f);
        }

        // 16 — Sludge Tom Flare: heavy floor tom + crash spray.
        inline void sludgeTomFlare (FillCtx& f)
        {
            for (int s = 0; s < 8; ++s)
                f.add (kFloorTom, s * 0.5, 0.85f + 0.05f * (s % 2 == 0));
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

        // 17 — Kick Buildup: 16th kicks accelerating into closing crash.
        inline void kickBuildup (FillCtx& f)
        {
            for (int s = 0; s < 16; ++s)
            {
                const float t = (float) s / 15.0f;
                f.add (kKick, s * 0.25, 0.55f + 0.40f * t);
            }
            f.add (kSnare, 3.5, 0.78f);
            f.add (kSnare, 3.75, 0.95f);
            f.closeCrash (1.0f, 0.92f);
        }

        // 18 — Kick-Tom Buildup: kick + tom alternating 16ths.
        inline void kickTomBuildup (FillCtx& f)
        {
            for (int s = 0; s < 16; ++s)
            {
                const double beat = s * 0.25;
                if (s % 2 == 0)
                    f.add (kKick, beat, 0.78f + 0.10f * (s / 15.0f));
                else
                    f.add ((s < 8 ? kHighTom : (s < 12 ? kLowTom : kFloorTom)),
                           beat, 0.78f + 0.10f * (s / 15.0f));
            }
            f.closeCrash (1.0f, 0.92f);
        }

        // 19 — 32nd Tom Cascade: 32nds across high→low→floor.
        inline void tomCascade (FillCtx& f)
        {
            f.kickFloor (0.62f);
            for (int s = 0; s < 32; ++s)
            {
                const double beat = s * 0.125;
                int note;
                if (s < 11)      note = kHighTom;
                else if (s < 21) note = kLowTom;
                else             note = kFloorTom;
                const float v = 0.55f + 0.30f * (s / 31.0f);
                f.add (note, beat, v + (s % 4 == 0 ? 0.08f : 0.0f));
            }
            f.add (kCrashL, kBar - 1.0 / 16.0, 1.0f, 0.5);
            f.add (kCrashR, kBar - 1.0 / 16.0, 0.95f, 0.5);
        }

        // 20 — Crash Spray: crashes scattered, snare backbone.
        inline void crashSpray (FillCtx& f)
        {
            f.kickFloor (0.78f);
            for (int s = 0; s < 8; ++s)
                f.add (kSnare, s * 0.5, 0.65f);
            for (int s = 0; s < 8; ++s)
                f.add ((s % 2 == 0 ? kCrashL : kCrashR), s * 0.5, 0.78f);
            if (f.density >= 0.55f)
                for (int s = 0; s < 16; ++s)
                    f.add (kSnare, s * 0.25 + 0.125, 0.32f);
            f.add (kCrashL, kBar - 1.0 / 16.0, 1.0f, 0.5);
            f.add (kCrashR, kBar - 1.0 / 16.0, 1.0f, 0.5);
        }

        // 21 — Sludge Tom Flare Heavy: most aggressive — 32nd toms +
        // doubled snare + 16th kicks + crash spray.
        inline void sludgeTomFlareHeavy (FillCtx& f)
        {
            for (int s = 0; s < 32; ++s)
            {
                const double beat = s * 0.125;
                int note;
                if (s < 11)      note = kHighTom;
                else if (s < 21) note = kLowTom;
                else             note = kFloorTom;
                f.add (note, beat, 0.78f + 0.15f * (s / 31.0f));
            }
            for (int s = 0; s < 16; ++s)
                f.add (kKick, s * 0.25, 0.78f);
            for (int s = 0; s < 8; ++s)
                f.add (kSnare, s * 0.5 + 0.25, 0.62f);
            for (int s = 0; s < 4; ++s)
                f.add ((s % 2 == 0 ? kCrashL : kCrashR), s, 0.85f);
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
