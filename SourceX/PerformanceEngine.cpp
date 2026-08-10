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

        /** How strong a beat position is: a quarter note is a landmark, a
            sixteenth off-beat is decoration. Used to decide what to drop when
            a take is busier than the pad asked for, and to accent. */
        float metricWeight (float beatInBar)
        {
            const float sixteenths = beatInBar * 4.0f;
            const float nearest    = std::round (sixteenths);
            if (std::abs (sixteenths - nearest) > 0.35f)
                return 0.25f;                       // between the sixteenths
            const int step = ((int) nearest) & 3;
            if (step == 0) return 1.0f;             // quarter
            if (step == 2) return 0.6f;             // eighth off-beat
            return 0.3f;                            // sixteenth
        }

        /** Lanes a drummer decorates with. Everything else - kick, snare,
            toms, crashes - is structure and is never thinned out. */
        bool isOrnamentLane (int lane)
        {
            return (isHatLane (lane) && lane != LaneHatPedal)
                || (isRideLane (lane) && lane != LaneRideCrash);
        }

        /** Where each limb sits against the click. Real players are not
            uniformly early or late: the kick leans back under the backbeat and
            the hat leads it slightly. Correlated, not noise. */
        float laneTimingBias (int lane)
        {
            if (lane == LaneKick)               return  0.006f;
            if (isSnareLane (lane))             return  0.004f;
            if (isHatLane (lane))               return -0.005f;
            if (isRideLane (lane))              return -0.003f;
            return 0.0f;
        }

        int wrap (int value, int size)
        {
            return size <= 0 ? 0 : ((value % size) + size) % size;
        }

        /** The seed a block renders from: the song seed folded with the block's
            identity, so two blocks at the same XY position still pick their own
            takes and neither changes when the other is edited. */
        std::uint64_t blockSeed (const PerformanceSettings& s)
        {
            return s.seed ^ (s.sectionSalt * 0x9E3779B97F4A7C15ull);
        }
    }

    int arrangementBars (const std::vector<ArrangementSection>& sections)
    {
        int total = 0;
        for (const auto& sec : sections)
            total += std::max (1, sec.numBars);
        return total;
    }

    PerformanceSettings PerformanceEngine::settingsForBar (const PerformanceSettings& base,
                                                           int bar) const
    {
        if (base.arrangement.empty())
            return base;

        const int total = arrangementBars (base.arrangement);
        int pos = wrap (bar, total);

        const ArrangementSection* found = &base.arrangement.back();
        for (const auto& sec : base.arrangement)
        {
            const int n = std::max (1, sec.numBars);
            if (pos < n)
            {
                found = &sec;
                break;
            }
            pos -= n;
        }

        PerformanceSettings out = base;
        out.complexity      = found->complexity;
        out.intensity       = found->intensity;
        out.sectionVelocity = found->velocity;
        out.fillAmount      = found->fillAmount;
        out.swing           = found->swing;
        out.halfTime        = found->halfTime;
        out.variationRhythm = found->variationRhythm;
        out.variationCymbal = found->variationCymbal;
        out.sectionHint     = found->section;
        out.sectionSalt     = (std::uint64_t) found->id;
        return out;
    }

    void PerformanceEngine::corpusTarget (const PerformanceSettings& s,
                                          float& complexity,
                                          float& intensity)
    {
        // The library is a practice-room recording session: its busiest bars
        // run past thirty hits, which is nobody's idea of a rock groove. The
        // pad is curved so its whole travel stays inside the range a song is
        // actually played in, and the top of the travel is a hard, busy take
        // rather than a drum solo.
        const float x = std::clamp (s.complexity, 0.0f, 1.0f);
        const float y = std::clamp (s.intensity,  0.0f, 1.0f);
        complexity = std::clamp (0.05f + 0.80f * x * x, 0.0f, 0.92f);
        intensity  = std::clamp (0.06f + 0.78f * y, 0.0f, 1.0f);
    }

    float PerformanceEngine::densityCap (const PerformanceSettings& s)
    {
        const float x = std::clamp (s.complexity, 0.0f, 1.0f);
        return (9.0f + 13.0f * x) * (s.halfTime ? 0.7f : 1.0f);
    }

    std::uint16_t PerformanceEngine::characterMask (const PerformanceSettings& s) const
    {
        if (corpus == nullptr || s.character < 0 || s.character >= corpus->numCharacters())
            return 0;
        return (std::uint16_t) (1u << s.character);
    }

    int PerformanceEngine::blockIndexForBar (const PerformanceSettings& s, int bar) const
    {
        if (s.arrangement.empty())
            return 0;

        int pos = wrap (bar, arrangementBars (s.arrangement));
        for (int i = 0; i < (int) s.arrangement.size(); ++i)
        {
            const int n = std::max (1, s.arrangement[(std::size_t) i].numBars);
            if (pos < n)
                return i;
            pos -= n;
        }
        return (int) s.arrangement.size() - 1;
    }

    int PerformanceEngine::sectionAtBar (const PerformanceSettings& s, int bar) const
    {
        // The host's song form wins while Follow Arrangement is on; the strip's
        // own blocks only decide the form when it is off.
        if (s.followSections && ! s.sections.empty())
        {
            for (const auto& span : s.sections)
                if (bar >= span.startBar && bar < span.startBar + span.numBars)
                    return span.section;
            return SectionVerse;
        }

        if (s.sectionHint >= 0)
            return s.sectionHint;
        if (! s.arrangement.empty())
            return settingsForBar (s, bar).sectionHint;
        return s.followSections ? SectionVerse : -1;
    }

    std::vector<int> PerformanceEngine::landingZone (const PerformanceSettings& s,
                                                     int maxResults) const
    {
        if (corpus == nullptr || ! corpus->isLoaded())
            return {};

        float cx = 0.0f, in = 0.0f;
        corpusTarget (s, cx, in);
        return corpus->neighbours (cx, in, characterMask (s),
                                   std::max (1, s.phraseBars), maxResults,
                                   s.timeSigNum, s.timeSigDen);
    }

    PerformanceEngine::Sources PerformanceEngine::pickSources (const PerformanceSettings& s,
                                                               int phraseIndex,
                                                               std::uint64_t seed) const
    {
        Sources out;

        const int section = sectionAtBar (s, phraseIndex * std::max (1, s.phraseBars));

        float cx = 0.0f, in = 0.0f;
        corpusTarget (s, cx, in);
        const auto ranked = corpus->neighbours (cx, in, characterMask (s),
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
        const auto& take = corpus->beat (ranked[(std::size_t) primary]);

        // One human take plays the whole bar. Cymbals only come from a
        // different take when the user asks for a cymbal variation, and even
        // then only from a take close enough that the two were playing the
        // same kind of music - otherwise the bar stops sounding like one
        // person and starts sounding like two records at once.
        out.skeleton = &take;
        out.colour   = &take;

        if (s.variationCymbal != 0 && n > 1)
        {
            const int start = wrap (draw (0xC010u) + s.variationCymbal, n);
            for (int step = 0; step < n; ++step)
            {
                const int i = wrap (start + step, n);
                if (i == primary)
                    continue;

                const auto& other = corpus->beat (ranked[(std::size_t) i]);
                if (other.bars != take.bars || other.sigNum != take.sigNum
                    || other.sigDen != take.sigDen)
                    continue;
                if (std::abs (other.complexity - take.complexity) > 0.15f
                    || std::abs (other.intensity - take.intensity) > 0.20f
                    || std::abs (other.swing - take.swing) > 0.15f)
                    continue;

                out.colour = &other;
                break;
            }
        }

        return out;
    }

    int PerformanceEngine::fillIndexForPhrase (const PerformanceSettings& s,
                                               int phraseIndex) const
    {
        const int  bars    = (s.fillLengthBars >= 1.5f) ? 2 : 1;
        const auto pick    = [&] (int idx, const std::vector<int>& avoid)
        {
            const std::uint64_t seed = mix (blockSeed (s) ^ mix ((std::uint64_t) idx + 1));
            return corpus->pickFill (s.fillComplexity, s.intensity, bars,
                                     (int) (mix (seed ^ 0xF111ull) % 16u), avoid,
                                     s.fillLaneMask & s.laneMask, s.fillStyleMask,
                                     s.timeSigNum, s.timeSigDen);
        };

        // Recently-used ring buffer: the last few fills are excluded so the
        // same one never lands twice in a row.
        // Only fills inside the same arrangement block count, so what a block
        // plays never depends on the block before it.
        const int bars_ = std::max (1, s.phraseBars);
        const int block = blockIndexForBar (s, phraseIndex * bars_);

        std::vector<int> avoid;
        for (int q = std::max (0, phraseIndex - 4); q < phraseIndex; ++q)
            if (blockIndexForBar (s, q * bars_) == block && phraseEndsWithFill (s, q))
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

    void PerformanceEngine::thinOrnaments (const PerformanceSettings& s,
                                           float dstBar,
                                           int   bars,
                                           std::vector<Raw>& raw) const
    {
        const int limit = (int) std::lround (densityCap (s) * (float) bars);
        if ((int) raw.size() <= limit)
            return;

        // Order the decoration from most to least expendable and take the top
        // off. The kick, the backbeat and the toms are the take; only the
        // hat and ride chatter that pushed it past the pad's density goes.
        std::vector<std::size_t> candidates;
        candidates.reserve (raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i)
            if (isOrnamentLane (raw[i].lane))
                candidates.push_back (i);

        const auto rankOf = [&] (std::size_t i)
        {
            const auto& h = raw[i];
            const float inBar = h.beat - std::floor (h.beat / dstBar) * dstBar;
            return metricWeight (inBar) * 1000.0f + (float) h.velocity;
        };

        std::sort (candidates.begin(), candidates.end(),
                   [&] (std::size_t a, std::size_t b)
                   {
                       const float ra = rankOf (a), rb = rankOf (b);
                       return ra != rb ? ra < rb : a < b;
                   });

        const std::size_t drop = std::min (candidates.size(),
                                           (std::size_t) ((int) raw.size() - limit));
        std::vector<bool> remove (raw.size(), false);
        for (std::size_t i = 0; i < drop; ++i)
            remove[candidates[i]] = true;

        std::size_t write = 0;
        for (std::size_t i = 0; i < raw.size(); ++i)
            if (! remove[i])
                raw[write++] = raw[i];
        raw.resize (write);
    }

    void PerformanceEngine::addCrashes (const PerformanceSettings& base,
                                        const PerformanceSettings& sec,
                                        int   phraseIndex,
                                        int   bars,
                                        float dstBar,
                                        std::uint64_t seed,
                                        std::vector<Hit>& out) const
    {
        const bool haveRight = (sec.laneMask & (1u << LaneCrashR)) != 0;
        const bool haveLeft  = (sec.laneMask & (1u << LaneCrashL)) != 0;
        if (! haveRight && ! haveLeft)
            return;

        const float energy  = std::clamp (sec.sectionVelocity, 0.0f, 1.0f);
        const int   section = sectionAtBar (base, phraseIndex * bars);
        const bool  blockTop = phraseIndex == 0
                             || blockIndexForBar (base, phraseIndex * bars)
                                    != blockIndexForBar (base, (phraseIndex - 1) * bars);
        const bool  afterFill = phraseIndex > 0 && phraseEndsWithFill (base, phraseIndex - 1);

        std::vector<float> beats;

        // The top of the phrase: always after a fill, always at the top of a
        // section, and once the section is being hit hard, every phrase.
        if (afterFill || blockTop || energy > 0.6f || section == SectionChorus)
            beats.push_back (0.0f);

        // Hard sections mark the half-way bar as well, which is what makes a
        // loud chorus sound like a chorus instead of a louder verse.
        if (energy > 0.72f && bars >= 2)
            beats.push_back ((float) (bars / 2) * dstBar);

        // Flat out, every bar gets marked.
        if (energy > 0.88f)
            for (int bar = 1; bar < bars; ++bar)
                beats.push_back ((float) bar * dstBar);

        std::sort (beats.begin(), beats.end());
        beats.erase (std::unique (beats.begin(), beats.end()), beats.end());

        int index = 0;
        for (const float beat : beats)
        {
            const bool taken = std::any_of (out.begin(), out.end(),
                                            [beat] (const Hit& h)
                                            {
                                                return isCymbalLane (h.lane)
                                                    && ! isHatLane (h.lane)
                                                    && std::abs (h.beat - beat) < 0.12f;
                                            });
            if (taken)
            {
                ++index;
                continue;
            }

            const bool useRight = haveRight && (! haveLeft || (index % 2) == 0);
            const int  lane     = useRight ? LaneCrashR : LaneCrashL;
            const float level   = 0.62f + 0.30f * energy
                                + 0.05f * (rand01 (seed, (std::uint64_t) index, 0xC7u) - 0.5f);

            out.push_back ({ std::max (0.0f, beat), (std::uint8_t) lane,
                             (std::uint8_t) std::clamp ((int) std::lround (level * 127.0f), 1, 127),
                             (std::uint8_t) (index % kRoundRobins) });
            ++index;
        }
    }

    std::vector<Hit> PerformanceEngine::renderPhrase (const PerformanceSettings& base,
                                                      int  phraseIndex,
                                                      bool includeFill) const
    {
        std::vector<Hit> out;
        if (corpus == nullptr || ! corpus->isLoaded())
            return out;

        const int   bars        = std::max (1, base.phraseBars);
        // Whichever arrangement block owns this phrase supplies its settings.
        const PerformanceSettings s = settingsForBar (base, phraseIndex * bars);
        const float dstBar      = std::max (1.0f, s.beatsPerBar);
        const float phraseBeats = dstBar * (float) bars;
        const std::uint64_t seed = mix (blockSeed (s) ^ mix ((std::uint64_t) phraseIndex + 1));

        // --- section colouring -------------------------------------------
        PerformanceSettings sec = s;
        switch (sectionAtBar (s, phraseIndex * bars))
        {
            case SectionIntro:
                sec.complexity *= 0.75f;
                sec.sectionVelocity *= 0.82f;
                sec.rideInsteadOfHat = true;
                break;
            case SectionChorus:
                sec.sectionVelocity = std::min (1.0f, sec.sectionVelocity + 0.10f);
                sec.hatOpenness = std::min (1.0f, sec.hatOpenness + 0.3f);
                break;
            case SectionBridge:
                sec.sectionVelocity *= 0.85f; sec.halfTime = true;
                break;
            case SectionOutro:
                sec.sectionVelocity *= 0.9f;
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

        thinOrnaments (sec, dstBar, bars, raw);

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

        // Dynamics come from the block's own Intensity knob. The pad decides
        // which take is played; this decides how hard it is played, and the
        // two are deliberately independent.
        const float energy  = std::clamp (sec.sectionVelocity, 0.0f, 1.0f);
        const float velGain = 0.62f + 0.78f * energy;

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
            float beat = r.beat + r.dev * (0.25f + 1.5f * s.humanize) + feelShift
                       + laneTimingBias (lane) * s.humanize;

            const int gridIndex = (int) std::llround (beat / gridUnit);
            if ((gridIndex & 1) != 0)
                beat += swingShift;

            beat += 0.014f * s.humanize
                    * cycleSin (driftPhase + beat / (8.0f * dstBar));

            if (beat < -0.02f || beat >= phraseBeats)
                continue;

            // --- velocity. The drummer's own dynamics carry the bar; the
            // corpus transition model only pulls a stroke gently towards what
            // usually follows the last one on this lane. Drawing a bucket at
            // random instead - which is what this used to do - throws the
            // performance away and replaces it with noise.
            float vel = (float) r.velocity;
            if (lastBucket[lane] >= 0)
            {
                if (const auto* row = corpus->velocityRow (lane, lastBucket[lane]))
                {
                    int   total    = 0;
                    float weighted = 0.0f;
                    for (int b = 0; b < GrooveCorpus::kVelocityBuckets; ++b)
                    {
                        total    += row[b];
                        weighted += (float) row[b] * ((float) b + 0.5f);
                    }
                    if (total > 0)
                    {
                        const float expected = weighted / (float) total * 128.0f
                                               / (float) GrooveCorpus::kVelocityBuckets;
                        vel = 0.88f * vel + 0.12f * expected;
                    }
                }
            }
            lastBucket[lane] = velocityBucket ((int) vel);

            // Metric accenting: the notes a player leans on stay up, the ones
            // between them sit back. This is the difference between a groove
            // and a row of identical hits.
            const float inBar = beat - std::floor (beat / dstBar) * dstBar;
            if (isOrnamentLane (lane))
                vel *= 0.80f + 0.26f * metricWeight (inBar);

            // Phrase breathing: the take lifts slightly into its own cadence
            // rather than sitting at one level for eight bars.
            vel *= 0.965f + 0.07f * (beat / std::max (1.0f, phraseBeats));

            if (lane == LaneSnareGhost || (s.ghostMask & (1u << lane)) != 0)
                vel *= 0.35f + 1.0f * s.ghostAmount;

            vel *= velGain;
            vel *= 0.975f + 0.05f * rand01 (seed, i, 5);

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

        addCrashes (s, sec, phraseIndex, bars, dstBar, seed, out);

        std::stable_sort (out.begin(), out.end(),
                          [] (const Hit& a, const Hit& b) { return a.beat < b.beat; });
        return out;
    }

    bool PerformanceEngine::phraseEndsWithFill (const PerformanceSettings& base,
                                                int phraseIndex) const
    {
        const int  bars = std::max (1, base.phraseBars);
        const auto s    = settingsForBar (base, phraseIndex * bars);
        if (s.fillAmount <= 0.001f)
            return false;

        // The last phrase of a block always turns the corner with a fill, so
        // no section of the song ever runs into the next one flat. Every 4th
        // phrase is the other natural cadence; the knob then fills in the
        // phrases between them as it is turned up.
        const bool blockEnd = blockIndexForBar (base, phraseIndex * bars)
                              != blockIndexForBar (base, (phraseIndex + 1) * bars);
        if (blockEnd)
            return true;

        const bool  cadence   = ((phraseIndex + 1) % 4) == 0;
        if (cadence)
            return true;

        return s.fillAmount > 0.55f
            || rand01 (mix (blockSeed (s) ^ 0xB00Bull), (std::uint64_t) phraseIndex)
                   < s.fillAmount * 0.6f;
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
        // Each arrangement block is its own performance, so the chain restarts
        // at a block boundary: editing one block cannot rotate the samples in
        // the block after it.
        int lastVariant[NumLanes];
        std::fill (std::begin (lastVariant), std::end (lastVariant), -1);
        int lastBlock = -1;
        for (auto& h : out)
        {
            const int block = blockIndexForBar (s, (int) std::floor (h.beat / beatsPerBar));
            if (block != lastBlock)
            {
                std::fill (std::begin (lastVariant), std::end (lastVariant), -1);
                lastBlock = block;
            }

            if ((int) h.variant == lastVariant[h.lane])
                h.variant = (std::uint8_t) (((int) h.variant + 1) % kRoundRobins);
            lastVariant[h.lane] = (int) h.variant;
        }
        return out;
    }
}
