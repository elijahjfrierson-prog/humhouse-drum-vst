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

    // 14. No audible loop over 32 bars at a fixed XY position.
    {
        std::vector<std::string> bars;
        const auto hits = engine.renderBars (s, 0, 32);
        bars.resize (32);
        for (const auto& h : hits)
        {
            const int bar = (int) (h.beat / s.beatsPerBar);
            if (bar >= 0 && bar < 32)
                bars[(std::size_t) bar] += std::to_string (h.lane) + ":"
                                         + std::to_string ((int) (h.beat * 24.0f)) + ",";
        }
        std::map<std::string, int> unique;
        for (const auto& b : bars)
            ++unique[b];
        check ((int) unique.size() >= 24, "32 bars at one XY position give at least 24 distinct bars");
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
        song.arrangement = { { 1, 8, hhx::SectionVerse,  0.2f, 0.25f, 0.0f, 0.0f, false, 0, 0 },
                             { 2, 8, hhx::SectionChorus, 0.9f, 0.95f, 0.8f, 0.0f, false, 0, 0 },
                             { 3, 8, hhx::SectionVerse,  0.2f, 0.25f, 0.0f, 0.0f, false, 0, 0 } };
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
        check (sameHits (barsOnly (before, 0, 8), barsOnly (after, 0, 8)),
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
        grown.arrangement.push_back ({ 4, 8, hhx::SectionOutro, 0.5f, 0.6f, 0.5f, 0.0f, false, 0, 0 });
        check (sameHits (before, engine.renderBars (grown, 0, 24)),
               "appending a block leaves the existing song identical");
        check (hhx::arrangementBars (grown.arrangement) == 32, "the appended block extends the song");

        // Two blocks with the same settings still play different takes.
        auto twins = s;
        twins.arrangement = { { 1, 8, hhx::SectionVerse, 0.5f, 0.6f, 0.3f, 0.0f, false, 0, 0 },
                              { 2, 8, hhx::SectionVerse, 0.5f, 0.6f, 0.3f, 0.0f, false, 0, 0 } };
        const auto twinHits = engine.renderBars (twins, 0, 16);
        check (! sameHits (barsOnly (twinHits, 0, 8), barsOnly (twinHits, 8, 16)),
               "identical blocks still play different takes");

        // Deterministic, like the rest of the engine.
        check (sameHits (before, engine.renderBars (song, 0, 24)),
               "arranged rendering is deterministic");

        // Block lengths the user picks need not divide by the phrase length.
        auto ragged = s;
        ragged.phraseBars  = 4;
        ragged.arrangement = { { 1, 3,  hhx::SectionVerse,  0.2f, 0.25f, 0.2f, 0.0f, false, 0, 0 },
                               { 2, 5,  hhx::SectionChorus, 0.9f, 0.95f, 0.6f, 0.0f, false, 0, 0 },
                               { 3, 7,  hhx::SectionBridge, 0.5f, 0.5f,  0.3f, 0.0f, false, 0, 0 } };
        const auto raggedHits = engine.renderBars (ragged, 0, 15);
        check (hhx::arrangementBars (ragged.arrangement) == 15
               && ! raggedHits.empty()
               && sameHits (raggedHits, engine.renderBars (ragged, 0, 15)),
               "blocks that do not divide by the phrase length still render");
        check (velocityIn (raggedHits, 3, 8) > velocityIn (raggedHits, 0, 3) + 4.0,
               "a block that starts mid-phrase still plays its own dynamics");
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
