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

        /** Sine of `turns` full cycles, built only from multiply/add/abs so the
            result is bit-identical on every platform. libm's sin is not, and the
            golden-render fixtures depend on it being. */
        float cycleSin (float turns)
        {
            const float frac = turns - std::floor (turns);
            const float x = 2.0f * frac - 1.0f;              // sin(2*pi*t) = -sin(pi*x)
            float s = 4.0f * x * (1.0f - std::abs (x));      // parabola approximation
            s = 0.775f * s + 0.225f * s * std::abs (s);      // shape correction
            return -s;
        }

        /** Skeleton lanes come from the primary take, colour from a neighbour. */
        bool isSkeletonLane (int lane)
        {
            return lane == LaneKick || isSnareLane (lane) || isTomLane (lane);
        }

        int velocityBucket (int velocity)
        {
            return std::clamp (velocity * GrooveCorpus::kVelocityBuckets / 128, 0,
                               GrooveCorpus::kVelocityBuckets - 1);
        }

        constexpr int kRoundRobins = 4;

        int wrap (int value, int size)
        {
            return size <= 0 ? 0 : ((value % size) + size) % size;
        }
    }

    std::uint16_t PerformanceEngine::characterMask (const PerformanceSettings& s) const
    {
        if (corpus == nullptr || s.character < 0 || s.character >= corpus->numCharacters())
            return 0;
        return (std::uint16_t) (1u << s.character);
    }

    int PerformanceEngine::sectionAtBar (const PerformanceSettings& s, int bar) const
    {
        if (! s.followSections)
            return -1;
        for (const auto& span : s.sections)
            if (bar >= span.startBar && bar < span.startBar + span.numBars)
                return span.section;
        return SectionVerse;
    }

    std::vector<int> PerformanceEngine::landingZone (const PerformanceSettings& s,
                                                     int maxResults) const
    {
        if (corpus == nullptr || ! corpus->isLoaded())
            return {};
        return corpus->neighbours (s.complexity, s.intensity, characterMask (s),
                                   std::max (1, s.phraseBars), maxResults,
                                   s.timeSigNum, s.timeSigDen);
    }

    PerformanceEngine::Sources PerformanceEngine::pickSources (const PerformanceSettings& s,
                                                               int phraseIndex,
                                                               std::uint64_t seed) const
    {
        Sources out;

        const int section = sectionAtBar (s, phraseIndex * std::max (1, s.phraseBars));
        const auto ranked = corpus->neighbours (s.complexity, s.intensity,
                                                characterMask (s),
                                                std::max (1, s.phraseBars), 12,
                                                s.timeSigNum, s.timeSigDen);
        if (ranked.empty())
            return out;

        // Seeded weighted choice over the k nearest takes: the closest are the
        // most likely, but a held XY position still breathes between real
        // performances instead of repeating one.
        const int n = (int) ranked.size();
        float weights[12] {};
        float total = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float w = 1.0f / (1.0f + (float) i * 0.55f);
            if (section >= 0 && corpus->beat (ranked[(std::size_t) i]).section
                                    == (std::uint8_t) section)
                w *= 2.0f;
            weights[i] = w;
            total += w;
        }

        const auto draw = [&] (std::uint64_t salt)
        {
            float r = rand01 (seed, salt) * total;
            for (int i = 0; i < n; ++i)
            {
                r -= weights[i];
                if (r <= 0.0f)
                    return i;
            }
            return n - 1;
        };

        const int primary = wrap (draw (0x5Bu) + s.variationRhythm, n);
        int colour  = wrap (draw (0xC010u) + s.variationCymbal + 1, n);
        if (n > 1 && colour == primary)
            colour = wrap (colour + 1, n);

        out.skeleton = &corpus->beat (ranked[(std::size_t) primary]);
        out.colour   = &corpus->beat (ranked[(std::size_t) colour]);
        return out;
    }

    int PerformanceEngine::fillIndexForPhrase (const PerformanceSettings& s,
                                               int phraseIndex) const
    {
        const int  bars    = (s.fillLengthBars >= 1.5f) ? 2 : 1;
        const auto pick    = [&] (int idx, const std::vector<int>& avoid)
        {
            const std::uint64_t seed = mix (s.seed ^ mix ((std::uint64_t) idx + 1));
            return corpus->pickFill (s.fillComplexity, s.intensity, bars,
                                     (int) (mix (seed ^ 0xF111ull) % 16u), avoid,
                                     s.fillLaneMask & s.laneMask, s.fillStyleMask,
                                     s.timeSigNum, s.timeSigDen);
        };

        // Recently-used ring buffer: the last few fills are excluded so the
        // same one never lands twice in a row.
        std::vector<int> avoid;
        for (int q = std::max (0, phraseIndex - 4); q < phraseIndex; ++q)
            if (phraseEndsWithFill (s, q))
                if (const int i = pick (q, {}); i >= 0)
                    avoid.push_back (i);

        return pick (phraseIndex, avoid);
    }

    void PerformanceEngine::appendFill (const PerformanceSettings& s,
                                        int   phraseIndex,
                                        float fillStartBeat,
                                        float phraseBeats,
                                        std::uint64_t seed,
                                        std::vector<Raw>& raw) const
    {
        const int fillIdx = fillIndexForPhrase (s, phraseIndex);
        if (fillIdx < 0)
            return;

        const auto& f         = corpus->fill (fillIdx);
        const float dstBar    = std::max (1.0f, s.beatsPerBar);
        const float kSourceBar = f.sourceBeatsPerBar();
        const bool  halfFill = s.fillLengthBars < 0.75f;
        // A half-bar fill is the tail of a one-bar fill, which is how it is
        // actually played, rather than a squashed whole fill.
        const float srcSkip  = halfFill ? kSourceBar * 0.5f : 0.0f;
        const float scale    = dstBar / kSourceBar;

        for (const auto& h : f.hits)
        {
            const float src = h.gridBeat() + h.devBeats();
            if (src < srcSkip)
                continue;
            const float beat = fillStartBeat + (src - srcSkip) * scale;
            if (beat < fillStartBeat - 0.02f || beat >= phraseBeats - 0.005f)
                continue;

            float vel = (float) h.velocity;
            vel *= 1.0f + (rand01 (seed, (std::uint64_t) (src * 64.0f), 0x1Eu) - 0.5f)
                          * 2.0f * s.fillVelVar * 0.4f;
            raw.push_back ({ beat, 0.0f, h.lane,
                             (std::uint8_t) std::clamp ((int) std::lround (vel), 1, 127) });
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
        const float dstBar      = std::max (1.0f, s.beatsPerBar);
        const float phraseBeats = dstBar * (float) bars;
        const std::uint64_t seed = mix (s.seed ^ mix ((std::uint64_t) phraseIndex + 1));

        // --- section colouring -------------------------------------------
        PerformanceSettings sec = s;
        switch (sectionAtBar (s, phraseIndex * bars))
        {
            case SectionIntro:
                sec.complexity *= 0.75f; sec.intensity *= 0.82f;
                sec.rideInsteadOfHat = true;
                break;
            case SectionChorus:
                sec.intensity = std::min (1.0f, sec.intensity + 0.12f);
                sec.hatOpenness = std::min (1.0f, sec.hatOpenness + 0.3f);
                break;
            case SectionBridge:
                sec.intensity *= 0.85f; sec.halfTime = true;
                break;
            case SectionOutro:
                sec.intensity *= 0.9f;
                break;
            default:
                break;
        }

        const auto src = pickSources (sec, phraseIndex, seed);
        if (src.skeleton == nullptr || src.colour == nullptr)
            return out;

        // --- gather, folded onto the target metre -------------------------
        // Takes played in the requested metre are preferred, so most bars need
        // no folding at all. When only 4/4 material is available, bars shorter
        // than the source drop the tail and longer bars wrap round to the top
        // of the source bar, keeping the backbeat where a drummer would put it.
        const float stretch  = s.halfTime || sec.halfTime ? 2.0f : 1.0f;

        std::vector<Raw> raw;
        raw.reserve (src.skeleton->hits.size() + src.colour->hits.size() + 16);

        const auto gather = [&] (const Phrase& phrase, bool wantSkeleton)
        {
            const int   srcBars   = std::max (1, phrase.bars);
            const float kSourceBar = phrase.sourceBeatsPerBar();
            // Only a 4/4 take folded into a compound metre needs its grid
            // re-read as triplets; a take actually played in 6/8 already is.
            const bool  compound  = s.timeSigDen == 8 && s.timeSigNum % 3 == 0
                                    && std::abs (kSourceBar - dstBar) > 0.01f;
            for (int bar = 0; bar < bars; ++bar)
            {
                const int srcBarIndex = bar % srcBars;
                for (const auto& h : phrase.hits)
                {
                    if (isSkeletonLane (h.lane) != wantSkeleton)
                        continue;

                    float grid = h.gridBeat();
                    if (compound)
                        grid = std::round (grid * 3.0f) / 3.0f;   // triplet subdivision

                    const int   hitBar = (int) std::floor (grid / kSourceBar);
                    if (hitBar != srcBarIndex)
                        continue;
                    float pos = (grid - (float) hitBar * kSourceBar) * stretch;
                    if (stretch > 1.0f && pos >= kSourceBar)
                        continue;

                    for (float p = pos; p < dstBar - 0.001f; p += kSourceBar * stretch)
                        raw.push_back ({ (float) bar * dstBar + p, h.devBeats(),
                                         h.lane, h.velocity });
                }
            }
        };

        gather (*src.skeleton, true);
        gather (*src.colour,   false);

        // --- fill ---------------------------------------------------------
        if (includeFill)
        {
            const float fillBeats = std::min (phraseBeats,
                                              dstBar * std::max (0.5f, s.fillLengthBars));
            const float fillStart = phraseBeats - fillBeats;
            raw.erase (std::remove_if (raw.begin(), raw.end(),
                                       [&] (const Raw& h) { return h.beat >= fillStart - 0.02f; }),
                       raw.end());
            appendFill (sec, phraseIndex, fillStart, phraseBeats, seed, raw);
        }

        // --- articulation routing ------------------------------------------
        for (auto& h : raw)
        {
            if (sec.rideInsteadOfHat && isHatLane (h.lane) && h.lane != LaneHatPedal)
            {
                h.lane = (h.lane >= LaneHatOpen2) ? (std::uint8_t) LaneRideEdge
                                                  : (std::uint8_t) LaneRideBow;
            }
            else if (isHatLane (h.lane) && h.lane != LaneHatPedal && sec.hatOpenness > 0.0f)
            {
                // Walk the openness ladder rather than jumping to "open hat".
                int step = 0;
                for (; step < kNumHatSteps; ++step)
                    if (kHatLadder[step] == h.lane)
                        break;
                if (step < kNumHatSteps)
                {
                    const float amount = sec.hatOpenness * (float) (kNumHatSteps - 1);
                    const int   extra  = (int) amount
                        + (rand01 (seed, (std::uint64_t) (h.beat * 64.0f), 7) < amount - std::floor (amount) ? 1 : 0);
                    h.lane = (std::uint8_t) kHatLadder[std::min (kNumHatSteps - 1, step + extra)];
                }
            }
        }

        raw.erase (std::remove_if (raw.begin(), raw.end(), [&] (const Raw& h)
                   { return (s.laneMask & (1u << h.lane)) == 0; }), raw.end());

        std::sort (raw.begin(), raw.end(),
                   [] (const Raw& a, const Raw& b)
                   {
                       if (a.beat < b.beat) return true;
                       if (b.beat < a.beat) return false;
                       if (a.lane != b.lane) return a.lane < b.lane;
                       return a.velocity < b.velocity;
                   });

        // --- feel, swing, drift, dynamics -----------------------------------
        const float gridUnit   = s.swingSixteenth ? 0.25f : 0.5f;
        const float swingShift = s.swing * gridUnit * 0.34f;
        const float feelShift  = (s.feel - 0.5f) * 0.05f;      // +/-25 ms at 120 bpm
        const float velGain    = 0.55f + 0.95f * sec.intensity;

        // Slow phrase breathing over eight bars, so bar 3 does not sit exactly
        // where bar 1 sat.
        const float driftPhase = (float) (phraseIndex * bars) / 8.0f;

        int lastBucket[NumLanes];
        int laneCount[NumLanes];
        int lastVariant[NumLanes];
        std::fill (std::begin (lastBucket), std::end (lastBucket), -1);
        std::fill (std::begin (laneCount), std::end (laneCount), 0);
        std::fill (std::begin (lastVariant), std::end (lastVariant), -1);

        out.reserve (raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            const Raw& r = raw[i];
            const int  lane = r.lane;

            // Keep the drummer's own deviation and scale it, rather than adding
            // uncorrelated noise on top of a quantised grid.
            float beat = r.beat + r.dev * (0.25f + 1.5f * s.humanize) + feelShift;

            const int gridIndex = (int) std::llround (beat / gridUnit);
            if ((gridIndex & 1) != 0)
                beat += swingShift;

            beat += 0.014f * s.humanize
                    * cycleSin (driftPhase + beat / (8.0f * dstBar));

            if (beat < -0.02f || beat >= phraseBeats)
                continue;

            // --- velocity: correlated with the previous stroke on this lane
            // via the transition matrix learned from the corpus.
            float vel = (float) r.velocity;
            if (const auto* row = corpus->velocityRow (lane, std::max (0, lastBucket[lane])))
            {
                if (lastBucket[lane] >= 0)
                {
                    int total = 0;
                    for (int b = 0; b < GrooveCorpus::kVelocityBuckets; ++b)
                        total += row[b];
                    if (total > 0)
                    {
                        int r = (int) (rand01 (seed, i, 0x5Eu) * (float) total);
                        int bucket = GrooveCorpus::kVelocityBuckets - 1;
                        for (int b = 0; b < GrooveCorpus::kVelocityBuckets; ++b)
                            if ((r -= row[b]) < 0) { bucket = b; break; }
                        const float target = ((float) bucket + 0.5f) * 128.0f
                                             / (float) GrooveCorpus::kVelocityBuckets;
                        vel = 0.72f * vel + 0.28f * target;
                    }
                }
            }
            lastBucket[lane] = velocityBucket ((int) vel);

            if (lane == LaneSnareGhost || (s.ghostMask & (1u << lane)) != 0)
                vel *= 0.35f + 1.0f * s.ghostAmount;

            vel *= velGain;
            vel *= 0.96f + 0.08f * rand01 (seed, i, 5);

            // --- round robin: never the same sample twice in a row on a lane.
            const int slots = kRoundRobins;
            int variant = (int) (rand01 (seed, i, 0x22u) * (float) slots);
            variant = std::min (slots - 1, variant);
            if (variant == lastVariant[lane])
                variant = (variant + 1 + (laneCount[lane] % (slots - 1))) % slots;
            lastVariant[lane] = variant;
            ++laneCount[lane];

            out.push_back ({ std::max (0.0f, beat), r.lane,
                             (std::uint8_t) std::clamp ((int) std::lround (vel), 1, 127),
                             (std::uint8_t) variant });
        }

        // Crash the downbeat that lands after a fill.
        if (phraseIndex > 0 && phraseEndsWithFill (s, phraseIndex - 1))
        {
            const int crash = (s.laneMask & (1u << LaneCrashR)) != 0 ? LaneCrashR
                            : (s.laneMask & (1u << LaneCrashL)) != 0 ? LaneCrashL : -1;
            if (crash >= 0)
            {
                const std::uint8_t vel = (std::uint8_t) std::clamp (
                    (int) std::lround ((0.75f + 0.25f * sec.intensity) * 127.0f), 1, 127);
                out.insert (out.begin(), { 0.0f, (std::uint8_t) crash, vel, 0 });
            }
        }

        std::stable_sort (out.begin(), out.end(),
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
        const bool  cadence   = ((phraseIndex + 1) % 4) == 0;
        const float threshold = cadence ? 0.05f : 0.55f;
        return s.fillAmount > threshold
            || rand01 (mix (s.seed ^ 0xB00Bull), (std::uint64_t) phraseIndex)
                   < s.fillAmount * (cadence ? 1.6f : 0.6f);
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

        std::stable_sort (out.begin(), out.end(),
                          [] (const Hit& a, const Hit& b) { return a.beat < b.beat; });

        // Round robin is guaranteed across the whole rendered range, not just
        // inside a phrase, so a phrase boundary cannot fire the same sample
        // twice in a row either.
        int lastVariant[NumLanes];
        std::fill (std::begin (lastVariant), std::end (lastVariant), -1);
        for (auto& h : out)
        {
            if ((int) h.variant == lastVariant[h.lane])
                h.variant = (std::uint8_t) (((int) h.variant + 1) % kRoundRobins);
            lastVariant[h.lane] = (int) h.variant;
        }
        return out;
    }
}
