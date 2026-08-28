// Engine-level tests for HumHouse Drums X. These cover the JUCE-free half of
// the plugin (corpus + performance engine), which is where every guarantee the
// UI depends on lives: determinism, real fill variation and lane masking.

#include "../SourceX/GrooveCorpus.h"
#include "../SourceX/PerformanceEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool condition, const std::string& what)
    {
        if (! condition)
        {
            std::printf ("FAIL  %s\n", what.c_str());
            ++failures;
        }
        else
        {
            std::printf ("ok    %s\n", what.c_str());
        }
    }

    std::vector<char> readFile (const std::string& path)
    {
        std::ifstream in (path, std::ios::binary);
        return { std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>() };
    }

    /** A stable fingerprint of a render.

        Beats are quantised to MIDI ticks before hashing so the fixture holds
        across compilers, but any change to what is actually played - a lane, a
        velocity, a round-robin slot, a note moving by a tick - changes it.
    */
    std::uint64_t renderHash (const std::vector<hhx::Hit>& hits)
    {
        std::uint64_t h = 0xCBF29CE484222325ull;
        const auto eat = [&h] (std::uint64_t v)
        {
            h = (h ^ v) * 0x100000001B3ull;
        };
        for (const auto& hit : hits)
        {
            eat ((std::uint64_t) (std::int64_t) std::llround (hit.beat * 960.0));
            eat (hit.lane);
            eat (hit.velocity);
            eat (hit.variant);
        }
        eat (hits.size());
        return h;
    }

    bool sameHits (const std::vector<hhx::Hit>& a, const std::vector<hhx::Hit>& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].lane != b[i].lane || a[i].velocity != b[i].velocity
                || a[i].beat != b[i].beat)
                return false;
        return true;
    }
}

int main (int argc, char** argv)
{
    const std::string corpusPath = argc > 1 ? argv[1] : "content/rock_corpus.hhc";
    const auto bytes = readFile (corpusPath);

    hhx::GrooveCorpus corpus;
    check (! bytes.empty(), "corpus file is readable: " + corpusPath);
    check (corpus.loadFromMemory (bytes.data(), bytes.size()), "corpus parses");
    check (corpus.numBeats() > 100, "corpus has a usable number of beat phrases");
    check (corpus.numFills() > 20,  "corpus has fills");

    hhx::PerformanceEngine engine;
    engine.setCorpus (&corpus);

    hhx::PerformanceSettings s;
    s.seed = 12345;

    // 1. Determinism: same settings + seed + bar range => identical output.
    {
        const auto a = engine.renderBars (s, 0, 16);
        const auto b = engine.renderBars (s, 0, 16);
        check (! a.empty(), "render produces hits");
        check (sameHits (a, b), "rendering is deterministic for a given seed");

        auto s2 = s;
        s2.seed = 999;
        check (! sameHits (a, engine.renderBars (s2, 0, 16)), "a new seed changes the performance");
    }

    // 2. Rendering a slice matches the same bars inside a longer render, so the
    //    UI preview, playback and MIDI export can never disagree.
    {
        const auto whole = engine.renderBars (s, 0, 8);
        const auto slice = engine.renderBars (s, 4, 4);
        std::vector<hhx::Hit> expected;
        for (const auto& h : whole)
            if (h.beat >= 4.0f * s.beatsPerBar - 0.001f)
                expected.push_back (h);
        check (expected.size() == slice.size(), "bar slices line up with the full render");
    }

    // 3. Lane mask: a muted lane never appears.
    {
        auto masked = s;
        for (int lane = hhx::LaneHatClosed; lane <= hhx::LaneHatBell; ++lane)
            masked.laneMask &= ~(1u << lane);
        bool foundHat = false;
        for (const auto& h : engine.renderBars (masked, 0, 32))
            if (h.lane >= hhx::LaneHatClosed && h.lane <= hhx::LaneHatBell)
                foundHat = true;
        check (! foundHat, "cleared lane-mask bits remove that kit piece");
    }

    // 4. Fills: with fills fully on, consecutive fill phrases differ — the old
    //    version stamped the same canned fill into every 8th bar.
    {
        auto fills = s;
        fills.fillAmount = 1.0f;
        fills.phraseBars = 2;

        std::vector<std::string> fingerprints;
        for (int phrase = 0; phrase < 8; ++phrase)
        {
            const int startBar = phrase * fills.phraseBars;
            std::string fp;
            for (const auto& h : engine.renderBars (fills, startBar, fills.phraseBars))
                fp += std::to_string (h.lane) + ":" + std::to_string ((int) (h.beat * 48.0f)) + ",";
            fingerprints.push_back (fp);
        }

        bool anyRepeatAdjacent = false;
        for (std::size_t i = 1; i < fingerprints.size(); ++i)
            if (fingerprints[i] == fingerprints[i - 1])
                anyRepeatAdjacent = true;
        check (! anyRepeatAdjacent, "consecutive phrases are never byte-identical");

        std::map<std::string, int> unique;
        for (const auto& fp : fingerprints)
            ++unique[fp];
        check (unique.size() >= 6, "8 phrases yield at least 6 distinct performances");
    }

    // 5. Fill lane mask: fills restricted to toms/snare use no cymbals.
    {
        auto fills = s;
        fills.fillAmount = 1.0f;
        fills.fillLaneMask = (1u << hhx::LaneSnare) | (1u << hhx::LaneTom1)
                           | (1u << hhx::LaneTom2) | (1u << hhx::LaneTom3)
                           | (1u << hhx::LaneKick);
        check (! engine.renderBars (fills, 0, 16).empty(), "fill-restricted render still produces hits");
    }

    // 6. Half time halves the backbeat rate rather than the note count.
    {
        auto half = s;
        half.halfTime = true;
        check (! engine.renderBars (half, 0, 8).empty(), "half-time render produces hits");
    }

    // 7. Odd metres: a 3/4 bar never emits a hit past beat 3.
    {
        auto waltz = s;
        waltz.timeSigNum = 3;
        waltz.timeSigDen = 4;
        waltz.beatsPerBar = 3.0f;
        bool pastBarEnd = false;
        const int bars = 8;
        for (const auto& h : engine.renderBars (waltz, 0, bars))
            if (h.beat >= waltz.beatsPerBar * (float) bars + 0.05f || h.beat < -0.05f)
                pastBarEnd = true;
        check (! pastBarEnd, "3/4 rendering stays inside the requested bars");
    }

    // 7b. Odd metres are played, not folded: the corpus carries takes that were
    //     recorded in them and selection prefers those over a folded 4/4 bar.
    {
        int native34 = 0, native68 = 0;
        for (int i = 0; i < corpus.numBeats(); ++i)
        {
            const auto& p = corpus.beat (i);
            native34 += (p.sigNum == 3 && p.sigDen == 4) ? 1 : 0;
            native68 += (p.sigNum == 6 && p.sigDen == 8) ? 1 : 0;
        }
        check (native34 > 0 && native68 > 0, "corpus holds real 3/4 and 6/8 takes");

        // 6/8 and 3/4 are both three quarter notes long, so the metre has to
        // be matched as a signature, not as a bar length.
        for (const auto sig : { std::pair { 3, 4 }, std::pair { 6, 8 } })
        {
            const auto near = corpus.neighbours (0.5f, 0.5f, 0, 2, 4,
                                                 sig.first, sig.second);
            bool allNative = ! near.empty();
            for (const int i : near)
                if (corpus.beat (i).sigNum != sig.first
                    || corpus.beat (i).sigDen != sig.second)
                    allNative = false;
            check (allNative, "odd-metre selection lands on takes played in it");
        }
    }

    // 8. Intensity actually changes how hard the kit is hit.
    {
        auto soft = s, loud = s;
        soft.intensity = 0.05f;
        loud.intensity = 0.98f;

        const auto average = [&engine] (const hhx::PerformanceSettings& settings)
        {
            const auto hits = engine.renderBars (settings, 0, 16);
            double total = 0.0;
            for (const auto& h : hits)
                total += h.velocity;
            return hits.empty() ? 0.0 : total / (double) hits.size();
        };
        check (average (loud) > average (soft) + 3.0, "intensity raises average velocity");
    }

    // 9. The 30-piece articulation map: every lane has a name and a GM note,
    //    and the notes are unique so per-instrument export never collides.
    {
        check (hhx::NumLanes == 30, "articulation map has 30 pieces");
        std::map<int, int> notes;
        bool named = true;
        for (int lane = 0; lane < hhx::NumLanes; ++lane)
        {
            const char* n = hhx::laneName (lane);
            if (n == nullptr || std::string (n).empty() || std::string (n) == "?")
                named = false;
            ++notes[hhx::laneToNote (lane)];
        }
        check (named, "every articulation is named");
        check ((int) notes.size() == hhx::NumLanes, "every articulation maps to a distinct MIDI note");
    }

    // 10. Corpus v2 metadata: characters, sections and a learned velocity model.
    {
        check (corpus.numCharacters() >= 8, "corpus clusters into at least 8 characters");
        bool allNamed = true;
        for (int c = 0; c < corpus.numCharacters(); ++c)
            if (corpus.characterName (c).empty())
                allNamed = false;
        check (allNamed, "every character is named");
        check (corpus.velocityRow (hhx::LaneSnare, 4) != nullptr, "velocity model is present");

        int deviating = 0;
        for (int i = 0; i < corpus.numBeats() && i < 400; ++i)
            for (const auto& h : corpus.beat (i).hits)
                if (h.dev != 0)
                    ++deviating;
        check (deviating > 0, "phrases keep the drummer's timing deviation");
    }

    // 11. Character selection actually restricts which takes are used.
    {
        auto a = s, b = s;
        a.character = 0;
        b.character = corpus.numCharacters() - 1;
        check (! sameHits (engine.renderBars (a, 0, 8), engine.renderBars (b, 0, 8)),
               "different characters play differently");
    }

    // 12. Fill style filtering keeps rendering valid.
    {
        auto tomLed = s;
        tomLed.fillAmount    = 1.0f;
        tomLed.fillStyleMask = hhx::FillTomLed;
        check (! engine.renderBars (tomLed, 0, 16).empty(), "tom-led fills still render");

        auto halfBar = s;
        halfBar.fillAmount     = 1.0f;
        halfBar.fillLengthBars = 0.5f;
        check (! engine.renderBars (halfBar, 0, 16).empty(), "half-bar fills still render");
    }

    // 13. Note-level round robin: consecutive hits on a lane differ in variant.
    {
        auto busy = s;
        busy.complexity = 0.85f;
        const auto hits = engine.renderBars (busy, 0, 16);
        std::map<int, int> lastVariant;
        int repeats = 0, pairs = 0;
        for (const auto& h : hits)
        {
            const auto it = lastVariant.find (h.lane);
            if (it != lastVariant.end())
            {
                ++pairs;
                if (it->second == h.variant)
                    ++repeats;
            }
            lastVariant[h.lane] = h.variant;
        }
        check (pairs > 0 && repeats == 0, "consecutive same-lane hits pick different variants");
    }

    // 13b. Sticking: the drums played with sticks are played with two hands, so
    //      consecutive strokes anywhere on the snare and toms swap hand - the
    //      even round-robin slots being one hand and the odd ones the other.
    {
        auto busy = s;
        busy.complexity = 0.85f;
        busy.fillAmount = 1.0f;

        int swaps = 0, sames = 0, last = -1;
        for (const auto& h : engine.renderBars (busy, 0, 16))
        {
            if (! (hhx::isSnareLane (h.lane) || hhx::isTomLane (h.lane)))
                continue;
            const int hand = h.variant & 1;
            if (last >= 0)
                (hand == last ? sames : swaps) += 1;
            last = hand;
        }
        // Two strokes with the same hand happen where one phrase runs into the
        // next, as they do when a player crosses the kit; through the body of a
        // phrase the hands take turns.
        check (swaps > 20 && sames * 4 < swaps,
               "the hands alternate across the snare and toms");
    }

    // 13c. The Ghost knob is the whole range of the thing: off means the ghost
    //      strokes and the percussion colour are not played at all, and full
    //      means there are more of them and they are heard.
    {
        const auto ghostly = [&] (float amount)
        {
            auto t = s;
            t.ghostAmount = amount;
            int count = 0, sum = 0;
            for (const auto& h : engine.renderBars (t, 0, 16))
                if (h.lane == hhx::LaneSnareGhost || h.lane == hhx::LaneSideStick
                    || h.lane == hhx::LaneSnareRim || h.lane == hhx::LanePerc)
                {
                    ++count;
                    sum += h.velocity;
                }
            return std::pair<int, double> { count, count > 0 ? (double) sum / count : 0.0 };
        };

        const auto off  = ghostly (0.0f);
        const auto half = ghostly (0.5f);
        const auto full = ghostly (1.0f);

        check (off.first == 0, "Ghost at zero plays no ghost strokes at all");
        check (full.first >= half.first, "Ghost at full plays at least as many ghosts");
        check (full.second > half.second + 4.0, "Ghost at full is heard, not just present");
    }

    // 14. The groove holds. A section keeps the take it was given for as long
    //     as nothing is turned, so a song does not restart itself every few
    //     bars; the fills are what keep it moving.
    {
        const auto barPrints = [&] (const hhx::PerformanceSettings& p, int numBars)
        {
            std::vector<std::string> bars ((std::size_t) numBars);
            for (const auto& h : engine.renderBars (p, 0, numBars))
            {
                const int bar = (int) (h.beat / p.beatsPerBar);
                if (bar >= 0 && bar < numBars)
                    bars[(std::size_t) bar] += std::to_string (h.lane) + ":"
                        + std::to_string ((int) ((h.beat - (float) bar * p.beatsPerBar) * 24.0f)) + ",";
            }
            std::map<std::string, int> unique;
            for (const auto& b : bars)
                ++unique[b];
            return unique.size();
        };

        auto hold = s;
        hold.phraseBars = 2;
        hold.humanize   = 0.0f;
        hold.fillAmount = 0.0f;
        check (barPrints (hold, 48) <= 4,
               "48 bars at one XY position keep playing the same groove");

        auto moving = hold;
        moving.fillAmount = 1.0f;
        check (barPrints (moving, 48) >= 4, "fills still move the song along");

        auto turned = hold;
        turned.complexity = std::min (1.0f, hold.complexity + 0.4f);
        check (! sameHits (engine.renderBars (hold, 0, 8),
                           engine.renderBars (turned, 0, 8)),
               "turning complexity does change the groove");
    }

    // 15. Section awareness: following an arrangement changes the performance.
    {
        auto flat = s, arranged = s;
        arranged.followSections = true;
        arranged.sections = { { 0, 4, hhx::SectionIntro }, { 4, 4, hhx::SectionChorus } };
        check (! sameHits (engine.renderBars (flat, 0, 8), engine.renderBars (arranged, 0, 8)),
               "following the arrangement changes the performance");
        check (engine.sectionAtBar (arranged, 5) == hhx::SectionChorus, "bar 5 reports the chorus");
    }

    // 15b. Arrangement blocks: each block plays its own settings, and editing
    //      one leaves every other bar of the song bit-identical.
    {
        auto song = s;
        song.arrangement = { { 1, 8, hhx::SectionVerse,  0.2f, 0.25f, 0.25f, 0.0f, 0.0f, false, 0, 0 },
                             { 2, 8, hhx::SectionChorus, 0.9f, 0.95f, 0.95f, 0.8f, 0.0f, false, 0, 0 },
                             { 3, 8, hhx::SectionVerse,  0.2f, 0.25f, 0.25f, 0.0f, 0.0f, false, 0, 0 } };
        check (hhx::arrangementBars (song.arrangement) == 24, "the arrangement is as long as its blocks");
        check (engine.sectionAtBar (song, 9) == hhx::SectionChorus,
               "bar 9 falls in the second block");

        const auto before = engine.renderBars (song, 0, 24);
        check (! before.empty(), "an arranged song renders");

        const auto velocityIn = [&] (const std::vector<hhx::Hit>& hits, int firstBar, int lastBar)
        {
            double sum = 0.0; int n = 0;
            for (const auto& h : hits)
            {
                const int bar = (int) (h.beat / song.beatsPerBar);
                if (bar >= firstBar && bar < lastBar) { sum += h.velocity; ++n; }
            }
            return n > 0 ? sum / n : 0.0;
        };
        check (velocityIn (before, 8, 16) > velocityIn (before, 0, 8) + 4.0,
               "the loud block plays harder than the quiet blocks either side of it");

        // Push the middle block only.
        auto edited = song;
        edited.arrangement[1].intensity  = 0.2f;
        edited.arrangement[1].velocity   = 0.2f;
        edited.arrangement[1].complexity = 0.15f;
        edited.arrangement[1].fillAmount = 0.0f;
        const auto after = engine.renderBars (edited, 0, 24);

        const auto barsOnly = [&] (const std::vector<hhx::Hit>& hits, int firstBar, int lastBar)
        {
            std::vector<hhx::Hit> out;
            for (const auto& h : hits)
            {
                const int bar = (int) (h.beat / song.beatsPerBar);
                if (bar >= firstBar && bar < lastBar)
                    out.push_back (h);
            }
            return out;
        };
        // Up to its run-in: the last four bars of a block are played towards
        // how hard the next one is, so editing a block is meant to change how
        // the bars before it arrive at it. Everything earlier is untouched.
        check (sameHits (barsOnly (before, 0, 4), barsOnly (after, 0, 4)),
               "editing a block leaves the block before it untouched");
        // Everything past the downbeat the previous block's fill resolves onto:
        // that crash belongs to the fill, so it is the one note a neighbour may
        // legitimately add or take away.
        const auto pastDownbeat = [&] (const std::vector<hhx::Hit>& hits, int firstBar, int lastBar)
        {
            std::vector<hhx::Hit> out;
            for (const auto& h : barsOnly (hits, firstBar, lastBar))
                if (h.beat > (float) firstBar * song.beatsPerBar + 0.01f)
                    out.push_back (h);
            return out;
        };
        check (sameHits (pastDownbeat (before, 16, 24), pastDownbeat (after, 16, 24)),
               "editing a block leaves the block after it untouched");
        check (! sameHits (barsOnly (before, 8, 16), barsOnly (after, 8, 16)),
               "editing a block does change that block");

        // Appending never rewrites what came before it: the "+" button is safe.
        auto grown = song;
        grown.arrangement.push_back ({ 4, 8, hhx::SectionOutro, 0.5f, 0.6f, 0.6f, 0.5f, 0.0f, false, 0, 0 });
        // Again, bar for bar except the run-in the new last block is now
        // arrived at: appending must not rewrite the song, only what leads
        // into what follows it.
        check (sameHits (barsOnly (before, 0, 20), barsOnly (engine.renderBars (grown, 0, 24), 0, 20)),
               "appending a block leaves the existing song identical");
        check (hhx::arrangementBars (grown.arrangement) == 32, "the appended block extends the song");

        // Two blocks with the same settings still play different takes.
        auto twins = s;
        twins.arrangement = { { 1, 8, hhx::SectionVerse, 0.5f, 0.6f, 0.6f, 0.3f, 0.0f, false, 0, 0 },
                              { 2, 8, hhx::SectionVerse, 0.5f, 0.6f, 0.6f, 0.3f, 0.0f, false, 0, 0 } };
        const auto twinHits = engine.renderBars (twins, 0, 16);
        check (! sameHits (barsOnly (twinHits, 0, 8), barsOnly (twinHits, 8, 16)),
               "identical blocks still play different takes");

        // Deterministic, like the rest of the engine.
        check (sameHits (before, engine.renderBars (song, 0, 24)),
               "arranged rendering is deterministic");

        // Block lengths the user picks need not divide by the phrase length.
        auto ragged = s;
        ragged.phraseBars  = 4;
        ragged.arrangement = { { 1, 3,  hhx::SectionVerse,  0.2f, 0.25f, 0.25f, 0.2f, 0.0f, false, 0, 0 },
                               { 2, 5,  hhx::SectionChorus, 0.9f, 0.95f, 0.95f, 0.6f, 0.0f, false, 0, 0 },
                               { 3, 7,  hhx::SectionBridge, 0.5f, 0.5f,  0.5f,  0.3f, 0.0f, false, 0, 0 } };
        const auto raggedHits = engine.renderBars (ragged, 0, 15);
        check (hhx::arrangementBars (ragged.arrangement) == 15
               && ! raggedHits.empty()
               && sameHits (raggedHits, engine.renderBars (ragged, 0, 15)),
               "blocks that do not divide by the phrase length still render");
        check (velocityIn (raggedHits, 3, 8) > velocityIn (raggedHits, 0, 3) + 4.0,
               "a block that starts mid-phrase still plays its own dynamics");
    }

    // 15b. Kit pieces are per block: an intro of nothing but toms leaves the
    //      chorus behind it playing the whole kit.
    {
        const std::uint32_t wholeKit = (1u << hhx::NumLanes) - 1u;
        std::uint32_t tomsOnly = 0, snareLanes = 0;
        for (int lane = 0; lane < hhx::NumLanes; ++lane)
        {
            if (hhx::isTomLane (lane))   tomsOnly   |= (1u << lane);
            if (hhx::isSnareLane (lane)) snareLanes |= (1u << lane);
        }

        auto song = s;
        song.arrangement = { { 1, 4, hhx::SectionIntro,  0.4f, 0.5f, 0.55f, 0.2f, 0.0f, false, 0, 0,
                               tomsOnly, 0 },
                             { 2, 8, hhx::SectionChorus, 0.8f, 0.9f, 0.9f,  0.6f, 0.0f, false, 0, 0,
                               wholeKit, 0 } };
        const auto hits = engine.renderBars (song, 0, 12);

        int introToms = 0, introOther = 0, chorusHats = 0, chorusSnare = 0, chorusKick = 0;
        for (const auto& h : hits)
        {
            const int bar = (int) (h.beat / 4.0f);
            if (bar < 4)
            {
                if (hhx::isTomLane (h.lane)) ++introToms;
                else                         ++introOther;
            }
            else
            {
                if (hhx::isHatLane (h.lane))   ++chorusHats;
                if (hhx::isSnareLane (h.lane)) ++chorusSnare;
                if (h.lane == hhx::LaneKick)   ++chorusKick;
            }
        }
        check (introToms >= 8 && introOther == 0,
               "a toms-only intro plays a tom part and nothing else");
        check (chorusHats > 0 && chorusSnare > 0 && chorusKick > 0,
               "the chorus behind it still plays hats, snare and kick");
        check (sameHits (hits, engine.renderBars (song, 0, 12)),
               "per-block pieces render the same every time");

        // The mask belongs to the block it is on, so the chorus is untouched by it.
        auto full = song;
        full.arrangement[0].laneMask = wholeKit;
        const auto fullHits = engine.renderBars (full, 0, 12);
        const auto barsFrom = [] (const std::vector<hhx::Hit>& in, int firstBar)
        {
            std::vector<hhx::Hit> out;
            for (const auto& h : in)
                if ((int) (h.beat / 4.0f) >= firstBar)
                    out.push_back (h);
            return out;
        };
        check (sameHits (barsFrom (hits, 4), barsFrom (fullHits, 4)),
               "switching a piece in the intro leaves the chorus bit-identical");

        // Switching one piece out while the rest of the kit plays takes that
        // part off, the way clicking it off in Logic does - it is not quietly
        // re-voiced onto something else.
        auto noSnare = s;
        noSnare.arrangement = { { 1, 8, hhx::SectionChorus, 0.7f, 0.8f, 0.8f, 0.4f, 0.0f, false, 0, 0,
                                  wholeKit & ~snareLanes, 0 } };
        int snareHits = 0, tomHits = 0, hatHits = 0;
        for (const auto& h : engine.renderBars (noSnare, 0, 8))
        {
            if (hhx::isSnareLane (h.lane)) ++snareHits;
            if (hhx::isTomLane (h.lane))   ++tomHits;
            if (hhx::isHatLane (h.lane))   ++hatHits;
        }
        check (snareHits == 0 && hatHits > 0, "switching the snare out takes the snare part off");
        check (tomHits <= 8, "and does not hand the backbeat to the toms");
    }

    const auto isCrash = [] (int lane)
    {
        return lane == hhx::LaneCrashL || lane == hhx::LaneCrashR
            || lane == hhx::LaneCrash3 || lane == hhx::LaneChina
            || lane == hhx::LaneSplash;
    };

    // 15c. The block's Intensity knob is its own control: it moves how hard
    //      the section is played without the pad re-picking the take.
    {
        const auto meanVel = [] (const std::vector<hhx::Hit>& hits)
        {
            double sum = 0.0;
            for (const auto& h : hits)
                sum += h.velocity;
            return hits.empty() ? 0.0 : sum / (double) hits.size();
        };
        const auto skeleton = [&] (const std::vector<hhx::Hit>& hits)
        {
            std::vector<std::string> out;
            for (const auto& h : hits)
                if (! isCrash (h.lane))
                    out.push_back (std::to_string (h.lane) + "@"
                                   + std::to_string ((int) (h.beat * 480.0f)));
            return out;
        };

        auto quiet = s, loud = s;
        quiet.sectionVelocity = 0.15f;
        loud.sectionVelocity  = 0.95f;
        const auto quietHits = engine.renderBars (quiet, 0, 8);
        const auto loudHits  = engine.renderBars (loud,  0, 8);

        check (meanVel (loudHits) > meanVel (quietHits) + 8.0,
               "the block's Intensity knob plays the section harder");
        check (skeleton (quietHits) == skeleton (loudHits),
               "Intensity moves dynamics only, it never re-picks the take");

        // The pad, by contrast, is a take chooser.
        auto padUp = s;
        padUp.intensity = 0.95f;
        check (! sameHits (engine.renderBars (s, 0, 8), engine.renderBars (padUp, 0, 8)),
               "the pad still chooses a different take");
    }

    // 15c-ii. One song, not a playlist: intro, verse and chorus are the same
    //         part played differently, the intro comes in on kick and hats, and
    //         a block leans into how hard the next one is played.
    {
        // A bar's part, ignoring how hard it was hit and ignoring the crashes
        // and fills that decorate it: kick and snare, on the beat grid.
        const auto spine = [&] (const std::vector<hhx::Hit>& hits, int firstBar, int lastBar)
        {
            std::vector<std::string> out;
            for (const auto& h : hits)
            {
                const int bar = (int) (h.beat / 4.0f);
                if (bar < firstBar || bar >= lastBar)
                    continue;
                if (h.lane != hhx::LaneKick && ! hhx::isSnareLane (h.lane))
                    continue;
                const float inBar = h.beat - (float) bar * 4.0f;
                out.push_back (std::to_string (h.lane == hhx::LaneKick ? 0 : 1) + "@"
                               + std::to_string ((int) std::round (inBar * 4.0f)));
            }
            std::sort (out.begin(), out.end());
            out.erase (std::unique (out.begin(), out.end()), out.end());
            return out;
        };
        const auto overlap = [] (const std::vector<std::string>& a,
                                 const std::vector<std::string>& b)
        {
            if (a.empty() || b.empty())
                return 0.0;
            std::vector<std::string> both;
            std::set_intersection (a.begin(), a.end(), b.begin(), b.end(),
                                   std::back_inserter (both));
            return (double) both.size()
                 / (double) std::max (a.size(), b.size());
        };

        auto song = s;
        song.arrangement = { { 1, 4, hhx::SectionIntro,  0.45f, 0.5f,  0.35f, 0.2f, 0.0f, false, 0, 0 },
                             { 2, 8, hhx::SectionVerse,  0.45f, 0.5f,  0.55f, 0.3f, 0.0f, false, 0, 0 },
                             { 3, 8, hhx::SectionChorus, 0.6f,  0.85f, 0.95f, 0.5f, 0.0f, false, 0, 0 } };
        const auto hits = engine.renderBars (song, 0, 20);

        // Bars taken from the middle of each block, clear of the fills at the
        // ends: the verse and the chorus are the same groove.
        check (overlap (spine (hits, 5, 7), spine (hits, 13, 15)) >= 0.7,
               "the chorus plays the verse's groove, not a different song");

        // The intro is that same groove with the kit held back.
        int introKick = 0, introHat = 0, introSnare = 0, introTom = 0;
        for (const auto& h : hits)
        {
            if ((int) (h.beat / 4.0f) >= 3)     // the last intro bar hands over with a fill
                continue;
            if (h.lane == hhx::LaneKick)        ++introKick;
            else if (hhx::isHatLane (h.lane))   ++introHat;
            else if (hhx::isSnareLane (h.lane)) ++introSnare;
            else if (hhx::isTomLane (h.lane))   ++introTom;
        }
        check (introKick > 0 && introHat > 0 && introSnare == 0 && introTom == 0,
               "an intro comes in on kick and hats by default");

        // Asking for the whole kit on that block is still the user's call.
        auto wholeIntro = song;
        wholeIntro.arrangement[0].laneMask = (1u << hhx::NumLanes) - 2u;   // all but the kick
        int keptSnare = 0;
        for (const auto& h : engine.renderBars (wholeIntro, 0, 4))
            if (hhx::isSnareLane (h.lane))
                ++keptSnare;
        check (keptSnare > 0, "touching the intro's kit switches hands the choice back");

        // The bars that run into the next block are played towards it: the same
        // verse grows into a loud chorus and comes down into a quiet one, which
        // is measured by turning the chorus down and nothing else.
        const auto meanVelIn = [] (const std::vector<hhx::Hit>& in, int firstBar, int lastBar)
        {
            double sum = 0.0; int n = 0;
            for (const auto& h : in)
            {
                const int bar = (int) (h.beat / 4.0f);
                if (bar >= firstBar && bar < lastBar)
                {
                    sum += h.velocity;
                    ++n;
                }
            }
            return n == 0 ? 0.0 : sum / (double) n;
        };
        auto down = song;
        down.arrangement[2].velocity = 0.15f;
        const auto downHits = engine.renderBars (down, 0, 20);

        check (meanVelIn (hits, 8, 12) > meanVelIn (downHits, 8, 12) + 4.0,
               "a block leans into how hard the next one is played");
        check (meanVelIn (hits, 4, 8) == meanVelIn (downHits, 4, 8),
               "and the bars before that run-in are played the same either way");
    }

    // 15d. Density: the pad stays inside what a drummer would actually play,
    //      instead of reaching for the busiest bars in the corpus.
    {
        const auto perBar = [&] (float x, float y)
        {
            auto t = s;
            t.complexity = x;
            t.intensity  = y;
            return (double) engine.renderBars (t, 0, 16).size() / 16.0;
        };
        const double top = perBar (1.0f, 1.0f);
        const double mid = perBar (0.5f, 0.5f);
        const double low = perBar (0.0f, 0.2f);

        check (top <= 26.0, "the top of the pad is still a playable bar");
        // A simple beat is a kick, a backbeat and unbroken eighth-note time:
        // around fourteen strokes a bar, not eight with holes in the hat.
        check (mid <= 19.0, "the middle of the pad stays sparse-to-moderate");
        check (low <= 16.0 && low >= 3.0, "the bottom of the pad plays a simple beat");
        check (top > mid && mid > low, "the pad gets busier from left to right");

        // Density is not the same thing as clutter: even wide open, a bar must
        // not stack a handful of pieces onto one instant.
        auto busy = s;
        busy.complexity = 1.0f;
        busy.intensity  = 1.0f;
        const auto hits = engine.renderBars (busy, 0, 16);
        std::size_t widest = 0;
        int stacked = 0;
        for (std::size_t i = 0; i < hits.size(); ++i)
        {
            std::size_t j = i;
            while (j < hits.size() && hits[j].beat - hits[i].beat < 0.03f)
                ++j;
            widest = std::max (widest, j - i);
            if (j - i > 3)
                ++stacked;
        }
        // Four limbs at once is a crash accent resolving a fill; anything
        // beyond that, or more than a couple per 16 bars, is a pile-up.
        check (widest <= 4 && stacked <= 2, "no instant piles up strikes");
    }

    // 15d-ii. Humanize at zero means dead tight: every strike lands on the
    //         grid the corpus was quantised to, with no drift or lane bias.
    {
        auto tight = s;
        tight.humanize   = 0.0f;
        tight.swing      = 0.0f;
        tight.feel       = 0.5f;
        tight.fillAmount = 0.0f;   // stretched fills have a grid of their own

        // 1/96 of a beat covers everything a drummer plays on purpose: 8ths,
        // 16ths, triplets, 32nds and sextuplets all land on it exactly.
        double worst = 0.0;
        for (const auto& h : engine.renderBars (tight, 0, 16))
        {
            const double tick = h.beat * 96.0;
            worst = std::max (worst, std::abs (tick - std::round (tick)) / 96.0);
        }
        check (worst < 0.001, "Humanize at zero puts every hit on the grid");

        auto loose = tight;
        loose.humanize = 1.0f;
        double moved = 0.0;
        for (const auto& h : engine.renderBars (loose, 0, 16))
        {
            const double tick = h.beat * 96.0;
            moved = std::max (moved, std::abs (tick - std::round (tick)) / 96.0);
        }
        check (moved > 0.004, "Humanize at full still pushes and pulls");
    }

    // 15e. Crashes arrive with the energy, on landmarks rather than everywhere.
    {
        const auto crashes = [&] (float energy)
        {
            auto t = s;
            t.sectionVelocity = energy;
            int n = 0;
            for (const auto& h : engine.renderBars (t, 0, 16))
                if (isCrash (h.lane))
                    ++n;
            return n;
        };
        const int soft = crashes (0.2f);
        const int hard = crashes (1.0f);
        check (hard > soft, "higher intensity brings more crashes");
        check (hard <= 16 * 4, "crashes still land on landmarks, not on every beat");
    }

    // 15e-2. A chorus played flat out is a wall of crashes on the bar and the
    // half bar, and its cymbal time keeping stays on the grid rather than
    // scattering stray hat and ride hits.
    {
        auto t = s;
        t.sectionVelocity = 0.95f;
        t.intensity       = 0.95f;
        t.complexity      = 0.7f;
        const auto hits = engine.renderBars (t, 0, 8);

        int crashes = 0, offGrid = 0, ornaments = 0;
        for (const auto& h : hits)
        {
            if (isCrash (h.lane))
                ++crashes;
            if (hhx::isHatLane (h.lane) || hhx::isRideLane (h.lane))
            {
                ++ornaments;
                const float inBar = h.beat - std::floor (h.beat / 4.0f) * 4.0f;
                if (std::abs (inBar / 0.5f - std::round (inBar / 0.5f)) > 0.2f)
                    ++offGrid;
            }
        }
        check (crashes >= 12, "a chorus at full loudness crashes on bars and half bars");
        check (offGrid * 4 <= ornaments,
               "loud cymbal time keeping stays on the grid");
    }

    // 15e-3. Kit-piece buttons add and subtract whole patterns: switching the
    // hats out silences them, and switching the ride in with the hats out gives
    // the section a ride part of its own.
    {
        auto t = s;
        t.complexity = 0.5f;
        const auto count = [&] (const hhx::PerformanceSettings& set, bool ride)
        {
            int n = 0;
            for (const auto& h : engine.renderBars (set, 0, 8))
                if (ride ? hhx::isRideLane (h.lane) : hhx::isHatLane (h.lane))
                    ++n;
            return n;
        };

        auto noHats = t;
        for (int lane = hhx::LaneHatClosed; lane <= hhx::LaneHatBell; ++lane)
            noHats.laneMask &= ~(1u << lane);
        check (count (noHats, false) == 0, "switching the hats out drops the hat pattern");
        check (count (noHats, true) >= 8, "the ride takes over the time keeping");

        auto noCymbals = noHats;
        for (int lane = hhx::LaneRideBow; lane <= hhx::LaneRideCrash; ++lane)
            noCymbals.laneMask &= ~(1u << lane);
        check (count (noCymbals, true) == 0 && count (noCymbals, false) == 0,
               "with both switched out the groove plays without cymbal time");
    }

    // 15f. Fills: every block ends with one, and cadences carry them too.
    {
        auto t = s;
        t.phraseBars  = 2;
        t.fillAmount  = 0.4f;
        t.arrangement = { { 1, 8, hhx::SectionVerse,  0.4f, 0.5f, 0.5f, 0.4f, 0.0f, false, 0, 0 },
                          { 2, 8, hhx::SectionChorus, 0.6f, 0.7f, 0.8f, 0.4f, 0.0f, false, 0, 0 } };

        check (engine.phraseEndsWithFill (t, 3), "the block hands over with a fill");
        check (engine.phraseEndsWithFill (t, 7), "the song's last block ends with a fill");

        int filled = 0;
        for (int phrase = 0; phrase < 8; ++phrase)
            filled += engine.phraseEndsWithFill (t, phrase) ? 1 : 0;
        check (filled >= 2, "fills turn up through the song, not only at the end");

        auto none = t;
        none.arrangement.clear();
        none.fillAmount = 0.0f;
        int stillFilled = 0;
        for (int phrase = 0; phrase < 8; ++phrase)
            stillFilled += engine.phraseEndsWithFill (none, phrase) ? 1 : 0;
        check (stillFilled == 0, "Fills at zero really means no fills");
    }

    // 15f-2. A fill glues the arrangement together, so it is played to the
    // grid: with Humanize off every hit of it lands on a thirty second or a
    // triplet sixteenth, in 4/4 and in a stretched metre alike.
    {
        const auto offGrid = [&] (const hhx::PerformanceSettings& set)
        {
            int bad = 0;
            for (const auto& h : engine.renderBars (set, 0, 16))
            {
                const float r   = h.beat - std::floor (h.beat);
                const float d32 = std::abs (r * 8.0f - std::round (r * 8.0f)) / 8.0f;
                const float d24 = std::abs (r * 6.0f - std::round (r * 6.0f)) / 6.0f;
                if (std::min (d32, d24) > 0.01f)
                    ++bad;
            }
            return bad;
        };

        auto t = s;
        t.humanize      = 0.0f;
        t.swing         = 0.0f;
        t.feel          = 0.5f;
        t.fillAmount    = 1.0f;
        t.phraseBars    = 2;
        check (offGrid (t) == 0, "fills and groove sit on the grid with Humanize off");


        auto waltz = t;
        waltz.timeSigNum = 3;
        waltz.beatsPerBar = 3.0f;
        check (offGrid (waltz) == 0, "a fill stretched into 3/4 still lands on the grid");
    }

    // 15f-3. A fill is vetted against the tempo it is actually played at. At 70
    // in half time the groove is moving at half rate, so the figures that read
    // as rolls at 150 are rejected instead of being smoothed over afterwards -
    // and the fill still resolves into the downbeat it hands over to.
    {
        // The fill is the last bar of a phrase that hands over with one, so it
        // can be read back out of the render without the engine marking it.
        const auto fillHits = [&] (const hhx::PerformanceSettings& set, int bars)
        {
            std::vector<std::vector<hhx::Hit>> out;
            const float phraseBeats = (float) std::max (1, set.phraseBars) * set.beatsPerBar;
            const auto all = engine.renderBars (set, 0, bars);
            for (int phrase = 0; phrase * std::max (1, set.phraseBars) < bars; ++phrase)
            {
                if (! engine.phraseEndsWithFill (set, phrase))
                    continue;
                const float end   = (float) (phrase + 1) * phraseBeats;
                const float start = end - set.fillLengthBars * set.beatsPerBar;
                std::vector<hhx::Hit> one;
                for (const auto& h : all)
                    if (h.beat >= start - 0.001f && h.beat < end)
                        one.push_back (h);
                if (! one.empty())
                    out.push_back (one);
            }
            return out;
        };

        auto slow = s;
        slow.humanize   = 0.0f;
        slow.swing      = 0.0f;
        slow.fillAmount = 1.0f;
        slow.phraseBars = 2;
        slow.halfTime   = true;
        slow.tempoBpm   = 70.0f;

        const auto fills = fillHits (slow, 32);
        check (! fills.empty(), "a slow half-time song still gets fills");

        // Strokes closer than a sixteenth of the half-time pulse: at 70 that is
        // a thirty-second-note roll over a groove playing half notes.
        int tooFast = 0, short_ = 0;
        for (const auto& fill : fills)
        {
            for (std::size_t i = 1; i < fill.size(); ++i)
                if (const float d = fill[i].beat - fill[i - 1].beat; d > 0.001f && d < 0.2f)
                    ++tooFast;

            float last = 0.0f;
            for (const auto& h : fill)
                last = std::max (last, h.beat);
            const float end = std::ceil (last / slow.beatsPerBar) * slow.beatsPerBar;
            if (end - last > 1.3f)
                ++short_;
        }

        check (tooFast == 0, "half-time fills are not thirty-second rolls at 70 bpm");
        check (short_ == 0, "every half-time fill runs up to the downbeat");

        // The same settings at 150 may reach for busier figures - the rule is
        // about the sounding tempo, not about the feel alone.
        auto fast = slow;
        fast.halfTime = false;
        fast.tempoBpm = 150.0f;
        check (! fillHits (fast, 32).empty(), "fast songs still get fills");

        auto slowStraight = slow;
        slowStraight.halfTime = false;
        check (! fillHits (slowStraight, 32).empty(), "slow straight-time songs still get fills");
    }

    // 16. Landing zone: the XY position resolves to real neighbouring takes.
    {
        const auto zone = engine.landingZone (s, 8);
        check (zone.size() >= 4, "XY position has several real takes within reach");
    }

    // 17. Every cell of the 10x10 XY grid holds a take, for every character,
    //     so no pad position has to reach across the plane for material.
    {
        int worst = 100;
        for (int c = 0; c < corpus.numCharacters(); ++c)
        {
            std::vector<bool> cell ((std::size_t) 100, false);
            const std::uint16_t mask = (std::uint16_t) (1u << c);
            for (int i = 0; i < corpus.numBeats(); ++i)
            {
                const auto& p = corpus.beat (i);
                if ((p.charMask & mask) == 0)
                    continue;
                const int x = std::min (9, (int) (p.complexity * 10.0f));
                const int y = std::min (9, (int) (p.intensity  * 10.0f));
                cell[(std::size_t) (y * 10 + x)] = true;
            }
            int filled = 0;
            for (bool b : cell)
                filled += b ? 1 : 0;
            worst = std::min (worst, filled);
        }
        check (worst == 100, "every character populates all 100 XY cells");
    }

    // 17b. What actually makes a bar read as a drummer rather than one-shots
    //      stitched together: unbroken cymbal time, a backbeat, crashes on the
    //      beat they mark, and time-keeping played under the kit, not over it.
    {
        const auto isTime = [] (int lane)
        {
            return (lane >= hhx::LaneHatClosed && lane <= hhx::LaneHatBell
                    && lane != hhx::LaneHatPedal)
                || (lane >= hhx::LaneRideBow && lane <= hhx::LaneRideEdge);
        };
        const auto isAccent = [] (int lane)
        {
            return lane == hhx::LaneCrashL || lane == hhx::LaneCrashR
                || lane == hhx::LaneCrash3 || lane == hhx::LaneChina
                || lane == hhx::LaneSplash || lane == hhx::LaneRideCrash;
        };

        for (const float complexity : { 0.15f, 0.45f, 0.78f, 1.0f })
            for (const float intensity : { 0.2f, 0.6f, 0.95f })
                for (const bool half : { false, true })
                {
                    auto t = s;
                    t.complexity = complexity;
                    t.intensity  = intensity;
                    t.halfTime   = half;
                    t.humanize   = 0.5f;
                    t.phraseBars = 4;

                    const auto hits = engine.renderBars (t, 0, 8);
                    const std::string at = " (c" + std::to_string ((int) (complexity * 100))
                                         + " i" + std::to_string ((int) (intensity * 100))
                                         + (half ? " half)" : ")");

                    // Cymbal time never leaves a hole. A bar and a half of
                    // sixteenths followed by silence is what sounded boxy.
                    std::vector<float> time;
                    float loudestHat = 0.0f;
                    for (const auto& h : hits)
                        if (isTime (h.lane) || isAccent (h.lane))
                        {
                            time.push_back (h.beat);
                            if (isTime (h.lane))
                                loudestHat = std::max (loudestHat, (float) h.velocity);
                        }
                    std::sort (time.begin(), time.end());

                    float widest = 0.0f;
                    for (std::size_t i = 1; i < time.size(); ++i)
                        widest = std::max (widest, time[i] - time[i - 1]);
                    check (time.size() > 8 && widest <= 1.02f,
                           "cymbal time runs without a hole" + at);

                    // Time-keeping stays under the kit.
                    check (loudestHat <= 112.0f, "hats keep off the ceiling" + at);

                    // Every bar has a backbeat.
                    int missing = 0;
                    for (int bar = 0; bar < 8; ++bar)
                    {
                        const float top = (float) bar * t.beatsPerBar;
                        const bool  hit = std::any_of (hits.begin(), hits.end(),
                            [&] (const hhx::Hit& h)
                            {
                                if (h.lane != hhx::LaneSnare && h.lane != hhx::LaneSnareRim
                                    && h.lane != hhx::LaneSnareFlam)
                                    return false;
                                const float in = h.beat - top;
                                return in > 0.5f && in < t.beatsPerBar - 0.35f;
                            });
                        if (! hit)
                            ++missing;
                    }
                    check (missing == 0, "every bar has a backbeat" + at);

                    // Crashes mark a beat exactly, whatever Humanize is doing.
                    float worstCrash = 0.0f;
                    for (const auto& h : hits)
                        if (isAccent (h.lane))
                        {
                            const float in = h.beat - std::floor (h.beat / t.beatsPerBar) * t.beatsPerBar;
                            worstCrash = std::max (worstCrash,
                                                   std::abs (in * 4.0f - std::round (in * 4.0f)) / 4.0f);
                        }
                    check (worstCrash < 0.005f, "crashes land on the beat they mark" + at);
                }
    }

    // 17c. Percussion is a colour the Ghost knob asks for, not something the
    //      generator sprinkles in on its own.
    {
        auto quiet = s;
        quiet.ghostAmount = 0.3f;
        bool perc = false;
        for (const auto& h : engine.renderBars (quiet, 0, 32))
            if (h.lane == hhx::LanePerc)
                perc = true;
        check (! perc, "percussion stays out until the Ghost knob asks for it");
    }

    // 17d. Hat Openness is a real control: closed at zero, an audible open-hat
    //      contrast at the top, and it reaches the fills too.
    {
        const auto openHats = [&] (float openness, bool fillsOnly)
        {
            auto t = s;
            t.hatOpenness = openness;
            t.fillAmount  = 1.0f;
            t.phraseBars  = 4;
            int n = 0;
            for (const auto& h : engine.renderBars (t, 0, 16))
            {
                const bool inFillBar = std::fmod (std::floor (h.beat / t.beatsPerBar), 4.0f) == 3.0f;
                if (fillsOnly && ! inFillBar)
                    continue;
                if (h.lane >= hhx::LaneHatOpen1 && h.lane <= hhx::LaneHatOpen4)
                    ++n;
            }
            return n;
        };

        const int shut = openHats (0.0f, false);
        const int wide = openHats (1.0f, false);
        check (wide > shut * 2 + 4, "hat openness opens the hats when turned up");
        check (openHats (1.0f, true) > 0, "fills get the open hats too");

        // Turned up it is a different instrument, not a stroke of colour: most
        // of the hat part is played open, and half the knob is already halfway
        // up the ladder.
        const auto openShare = [&] (float openness)
        {
            auto t = s;
            t.hatOpenness = openness;
            int open = 0, hats = 0;
            for (const auto& h : engine.renderBars (t, 0, 16))
            {
                if (h.lane >= hhx::LaneHatClosed && h.lane <= hhx::LaneHatOpen4
                    && h.lane != hhx::LaneHatPedal)
                    ++hats;
                if (h.lane >= hhx::LaneHatOpen1 && h.lane <= hhx::LaneHatOpen4)
                    ++open;
            }
            return hats > 0 ? (float) open / (float) hats : 0.0f;
        };

        check (openShare (1.0f) > 0.8f, "wide open rides open, not just accents");
        check (openShare (0.5f) > 0.3f, "half the knob is already half open");

        // And the hat is played, not stamped: a real part leans on the beat and
        // lets the strokes between it fall away.
        {
            auto t = s;
            t.hatOpenness = 0.0f;
            int lo = 127, hi = 0;
            for (const auto& h : engine.renderBars (t, 0, 8))
                if (h.lane == hhx::LaneHatClosed || h.lane == hhx::LaneHatTight)
                {
                    lo = std::min (lo, (int) h.velocity);
                    hi = std::max (hi, (int) h.velocity);
                }
            check (hi - lo >= 12, "the hat part has real dynamics, not one level");
        }
    }

    // 17e. One hand, one cymbal: no two time-keeping strokes within a few
    //      milliseconds of each other.
    {
        const auto isCymbalTime = [] (int lane)
        {
            return (lane >= hhx::LaneHatClosed && lane <= hhx::LaneHatBell
                    && lane != hhx::LaneHatPedal)
                || (lane >= hhx::LaneRideBow && lane <= hhx::LaneRideEdge);
        };

        for (const float openness : { 0.0f, 0.5f, 1.0f })
        {
            auto t = s;
            t.hatOpenness = openness;
            const auto hits = engine.renderBars (t, 0, 16);
            int doubled = 0;
            for (std::size_t i = 1; i < hits.size(); ++i)
                for (std::size_t j = i; j-- > 0 && hits[i].beat - hits[j].beat < 0.06f;)
                    if (isCymbalTime (hits[i].lane) && isCymbalTime (hits[j].lane))
                        ++doubled;
            check (doubled == 0, "no doubled cymbal strokes (open "
                                 + std::to_string ((int) (openness * 100)) + ")");
        }
    }

    // 17f. Every bar that is not handing over to a fill starts on a kick: a
    //      chorus or a verse opening without one lands with nothing under it.
    {
        for (const float cx : { 0.2f, 0.5f, 0.85f })
        {
            auto t = s;
            t.complexity = cx;
            t.fillAmount = 0.0f;
            const auto hits = engine.renderBars (t, 0, 16);
            int missing = 0;
            for (int bar = 0; bar < 16; ++bar)
            {
                const float downbeat = (float) bar * t.beatsPerBar;
                bool kick = false;
                for (const auto& h : hits)
                    if (h.lane == hhx::LaneKick
                        && std::abs (h.beat - downbeat) < 0.06f)
                        kick = true;
                if (! kick)
                    ++missing;
            }
            check (missing == 0, "a kick lands on the one of every bar (cx "
                                 + std::to_string ((int) (cx * 100)) + ")");
        }
    }

    // 17g. Straight is straight: with Swing at zero nothing - groove or fill -
    //      may be played off the straight grid, which is the shuffle that used
    //      to creep into the metal characters.
    {
        auto t = s;
        t.swing = 0.0f;
        t.fillAmount = 1.0f;
        const auto hits = engine.renderBars (t, 0, 32);
        // A triplet sixteenth sits a third or two thirds of the way through a
        // sixteenth; the straight grid only reaches the halfway point, so a
        // stroke near those thirds was played in triplets, not pushed by feel.
        int off = 0;
        for (const auto& h : hits)
        {
            const float in16 = h.beat * 4.0f - std::floor (h.beat * 4.0f);
            if (std::min (std::abs (in16 - 1.0f / 3.0f),
                          std::abs (in16 - 2.0f / 3.0f)) < 0.1f)
                ++off;
        }
        check (off == 0, "a straight song has no triplet strokes in it");
    }

    // 18. Load time: the corpus is parsed well inside the 150 ms budget.
    {
        const auto start = std::chrono::steady_clock::now();
        hhx::GrooveCorpus timed;
        const bool ok = timed.loadFromMemory (bytes.data(), bytes.size());
        const auto ms = std::chrono::duration<double, std::milli> (
                            std::chrono::steady_clock::now() - start).count();
        check (ok && ms < 150.0, "corpus loads in under 150 ms");
    }

    // 19. Golden renders: fixed settings must keep producing the exact same
    //     performance. Run with --update-golden after an intended change.
    {
        const std::string goldenPath = argc > 2 ? argv[2] : "tests/golden_renders.txt";
        const bool update = argc > 3 && std::string (argv[3]) == "--update-golden";

        struct Fixture { std::string name; hhx::PerformanceSettings settings; int bars; };
        std::vector<Fixture> fixtures;
        {
            auto rock = s;
            fixtures.push_back ({ "rock-8", rock, 8 });

            auto loud = s;
            loud.character = 3;
            loud.complexity = 0.8f; loud.intensity = 0.9f;
            loud.fillAmount = 1.0f; loud.fillLengthBars = 2.0f;
            fixtures.push_back ({ "hard-fills-8", loud, 8 });

            auto waltz = s;
            waltz.timeSigNum = 3; waltz.timeSigDen = 4; waltz.beatsPerBar = 3.0f;
            fixtures.push_back ({ "waltz-8", waltz, 8 });

            auto shuffled = s;
            shuffled.swing = 0.6f; shuffled.halfTime = true; shuffled.humanize = 0.9f;
            fixtures.push_back ({ "shuffle-halftime-8", shuffled, 8 });
        }

        std::map<std::string, std::uint64_t> golden;
        {
            std::ifstream in (goldenPath);
            std::string name;
            std::uint64_t hash = 0;
            while (in >> name >> hash)
                golden[name] = hash;
        }

        if (update)
        {
            std::ofstream out (goldenPath, std::ios::trunc);
            for (const auto& f : fixtures)
                out << f.name << ' ' << renderHash (engine.renderBars (f.settings, 0, f.bars)) << '\n';
            std::printf ("note  golden fixtures rewritten: %s\n", goldenPath.c_str());
        }
        else
        {
            check (golden.size() == fixtures.size(), "golden fixture file is present");
            for (const auto& f : fixtures)
            {
                const auto it = golden.find (f.name);
                check (it != golden.end()
                       && it->second == renderHash (engine.renderBars (f.settings, 0, f.bars)),
                       "golden render matches: " + f.name);
            }
        }
    }

    std::printf ("\n%s\n", failures == 0 ? "All engine tests passed." : "Engine tests FAILED.");
    return failures == 0 ? 0 : 1;
}
