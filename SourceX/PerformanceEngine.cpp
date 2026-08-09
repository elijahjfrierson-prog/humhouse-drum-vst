#include "PerformanceEngine.h"

#include <algorithm>
#include <cmath>

namespace hhx
{
    namespace
    {
        std::uint64_t mix (std::uint64_t x)
        {
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }

        /** Deterministic 0..1 draw from a seed and a couple of salts. */
        float rand01 (std::uint64_t seed, std::uint64_t a, std::uint64_t b = 0)
        {
            return (float) ((mix (seed ^ mix (a * 0x2545F4914F6CDD1Dull + b)) >> 11)
                            * (1.0 / 9007199254740992.0));
        }

        bool isRhythmLane (int lane)
        {
            return lane == LaneKick || lane == LaneSnare
                || lane == LaneSnareRim || lane == LaneSideStick;
        }

        bool isTomLane (int lane)
        {
            return lane == LaneTomHi || lane == LaneTomMid || lane == LaneTomFloor;
        }

        float snapDeviation (float beat, float grid)
        {
            const float snapped = std::round (beat / grid) * grid;
            return beat - snapped;
        }
    }

    std::vector<Hit> PerformanceEngine::renderPhrase (const PerformanceSettings& s,
                                                      int  phraseIndex,
                                                      bool includeFill) const
    {
        std::vector<Hit> out;
        if (corpus == nullptr || ! corpus->isLoaded())
            return out;

        const int   bars        = std::max (1, s.phraseBars);
        const float beatsPerBar = std::max (1.0f, s.beatsPerBar);
        const float phraseBeats = beatsPerBar * (float) bars;
        const std::uint64_t seed = mix (s.seed ^ mix ((std::uint64_t) phraseIndex + 1));

        // --- source takes -------------------------------------------------
        // The kick/snare skeleton and the cymbal ostinato come from two
        // separate real performances, exactly like Logic's two lane groups
        // with their own variation selectors. Every few phrases we step to a
        // neighbouring take so a held XY position never loops audibly.
        const int drift = (rand01 (seed, 11) < 0.35f) ? 1 : 0;
        const int rhythmIdx = corpus->pickBeat (s.complexity, s.intensity,
                                                s.variationRhythm + drift);
        const int cymbalIdx = corpus->pickBeat (s.complexity, s.intensity,
                                                s.variationCymbal + 3 + drift);
        if (rhythmIdx < 0 || cymbalIdx < 0)
            return out;

        const auto& rhythmSrc = corpus->beat (rhythmIdx);
        const auto& cymbalSrc = corpus->beat (cymbalIdx);

        const float srcBeats = 4.0f * (float) rhythmSrc.bars;
        const float stretch  = s.halfTime ? 2.0f : 1.0f;

        std::vector<Hit> raw;
        raw.reserve (rhythmSrc.hits.size() + cymbalSrc.hits.size());

        const auto gather = [&] (const Phrase& src, bool wantRhythm)
        {
            for (const auto& h : src.hits)
            {
                if (isRhythmLane (h.lane) != wantRhythm)
                    continue;
                // Toms belong to whichever group is playing them; in a groove
                // (as opposed to a fill) they read as cymbal-group colour.
                float beat = h.beat * stretch;
                while (beat < phraseBeats)
                {
                    raw.push_back ({ beat, h.lane, h.velocity });
                    beat += srcBeats * stretch;   // tile if the phrase is longer
                }
            }
        };

        gather (rhythmSrc, true);
        gather (cymbalSrc, false);

        // --- fill ---------------------------------------------------------
        const int fillBars = std::clamp (s.fillBars, 1, bars);
        if (includeFill)
        {
            const float fillStart = phraseBeats - beatsPerBar * (float) fillBars;
            raw.erase (std::remove_if (raw.begin(), raw.end(),
                                       [&] (const Hit& h) { return h.beat >= fillStart - 0.02f; }),
                       raw.end());

            const int fillIdx = corpus->pickFill (s.fillComplexity,
                                                  s.intensity,
                                                  fillBars,
                                                  (int) (mix (seed ^ 0xF111ull) % 16),
                                                  {},
                                                  s.fillLaneMask & s.laneMask);
            if (fillIdx >= 0)
            {
                const auto& f = corpus->fill (fillIdx);
                const float scale = beatsPerBar / 4.0f;   // fit 4/4 fills to the metre
                for (const auto& h : f.hits)
                {
                    const float beat = fillStart + h.beat * scale;
                    if (beat < phraseBeats - 0.01f)
                        raw.push_back ({ beat, h.lane, h.velocity });
                }
            }
        }

        // --- lane routing ---------------------------------------------------
        for (auto& h : raw)
        {
            if (s.rideInsteadOfHat)
            {
                if (h.lane == LaneHatClosed) h.lane = LaneRide;
                else if (h.lane == LaneHatOpen) h.lane = LaneRideBell;
            }
            else if (h.lane == LaneHatClosed && s.hatOpenness > 0.0f)
            {
                if (rand01 (seed, (std::uint64_t) (h.beat * 64.0f), 7) < s.hatOpenness)
                    h.lane = LaneHatOpen;
            }
        }

        raw.erase (std::remove_if (raw.begin(), raw.end(), [&] (const Hit& h)
                   { return (s.laneMask & (1u << h.lane)) == 0; }), raw.end());

        // --- feel, swing, humanization, dynamics ------------------------------
        const float gridUnit   = s.swingSixteenth ? 0.25f : 0.5f;
        const float swingShift = s.swing * gridUnit * 0.34f;
        const float feelShift  = (s.feel - 0.5f) * 0.05f;      // ±25 ms at 120 bpm
        const float timingGain = 0.35f + 1.15f * s.humanize;
        const float velGain    = 0.55f + 0.95f * s.intensity;

        out.reserve (raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            Hit h = raw[i];

            // Keep the drummer's own deviation from the 16th grid and scale it,
            // rather than adding uncorrelated noise on top of a quantised grid.
            const float dev     = snapDeviation (h.beat, 0.25f);
            const float snapped = h.beat - dev;
            float beat = snapped + dev * timingGain + feelShift;

            // Swing: push the odd grid positions late.
            const int gridIndex = (int) std::llround (snapped / gridUnit);
            if ((gridIndex & 1) != 0)
                beat += swingShift;

            // Slow phrase-level breathing so bar 3 does not sit exactly where
            // bar 1 sat.
            beat += (rand01 (seed, i, 3) - 0.5f) * 0.012f * s.humanize;

            if (beat < -0.02f || beat >= phraseBeats)
                continue;

            float vel = (float) h.velocity / 127.0f;

            const bool ghost = h.lane == LaneSnare && h.velocity < 62;
            if (ghost)
                vel *= 0.35f + 1.05f * s.ghostAmount;

            vel *= velGain;
            vel *= 0.94f + 0.12f * rand01 (seed, i, 5);

            h.beat     = std::max (0.0f, beat);
            h.velocity = (std::uint8_t) std::clamp ((int) std::lround (vel * 127.0f), 1, 127);
            out.push_back (h);
        }

        // Crash the downbeat that lands after a fill.
        if (phraseIndex > 0 && phraseEndsWithFill (s, phraseIndex - 1)
            && (s.laneMask & (1u << LaneCrashR)) != 0)
        {
            const std::uint8_t vel = (std::uint8_t) std::clamp (
                (int) std::lround ((0.75f + 0.25f * s.intensity) * 127.0f), 1, 127);
            out.push_back ({ 0.0f, LaneCrashR, vel });
        }

        std::sort (out.begin(), out.end(),
                   [] (const Hit& a, const Hit& b) { return a.beat < b.beat; });
        return out;
    }

    bool PerformanceEngine::phraseEndsWithFill (const PerformanceSettings& s,
                                                int phraseIndex) const
    {
        if (s.fillAmount <= 0.001f)
            return false;

        // Every 4th phrase (an 8-bar section at the default 2-bar phrase) is a
        // natural cadence, so it fills first; the knob then fills in the
        // in-between phrases as it is turned up.
        const bool cadence = ((phraseIndex + 1) % 4) == 0;
        const float threshold = cadence ? 0.05f : 0.55f;
        return s.fillAmount > threshold
            || rand01 (mix (s.seed ^ 0xB00Bull), (std::uint64_t) phraseIndex) < s.fillAmount * (cadence ? 1.6f : 0.6f);
    }

    std::vector<Hit> PerformanceEngine::renderPhrasePreview (const PerformanceSettings& s,
                                                             int phraseIndex) const
    {
        return renderPhrase (s, phraseIndex, phraseEndsWithFill (s, phraseIndex));
    }

    std::vector<Hit> PerformanceEngine::renderBars (const PerformanceSettings& s,
                                                    int startBar,
                                                    int numBars) const
    {
        std::vector<Hit> out;
        if (numBars <= 0)
            return out;

        const int   bars        = std::max (1, s.phraseBars);
        const float beatsPerBar = std::max (1.0f, s.beatsPerBar);
        const int   firstPhrase = (int) std::floor ((double) startBar / bars);
        const int   lastPhrase  = (int) std::floor ((double) (startBar + numBars - 1) / bars);

        const float rangeStart = (float) startBar * beatsPerBar;
        const float rangeEnd   = (float) (startBar + numBars) * beatsPerBar;

        for (int p = firstPhrase; p <= lastPhrase; ++p)
        {
            const float offset = (float) (p * bars) * beatsPerBar;
            for (auto h : renderPhrase (s, p, phraseEndsWithFill (s, p)))
            {
                h.beat += offset;
                if (h.beat >= rangeStart && h.beat < rangeEnd)
                    out.push_back (h);
            }
        }

        std::sort (out.begin(), out.end(),
                   [] (const Hit& a, const Hit& b) { return a.beat < b.beat; });
        return out;
    }
}
