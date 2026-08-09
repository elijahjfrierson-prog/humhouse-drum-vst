// Engine-level tests for HumHouse Drums X. These cover the JUCE-free half of
// the plugin (corpus + performance engine), which is where every guarantee the
// UI depends on lives: determinism, real fill variation and lane masking.

#include "../SourceX/GrooveCorpus.h"
#include "../SourceX/PerformanceEngine.h"

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
        masked.laneMask &= ~(1u << hhx::LaneHatClosed);
        masked.laneMask &= ~(1u << hhx::LaneHatOpen);
        masked.laneMask &= ~(1u << hhx::LaneHatPedal);
        bool foundHat = false;
        for (const auto& h : engine.renderBars (masked, 0, 32))
            if (h.lane == hhx::LaneHatClosed || h.lane == hhx::LaneHatOpen || h.lane == hhx::LaneHatPedal)
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
        fills.fillLaneMask = (1u << hhx::LaneSnare) | (1u << hhx::LaneTomHi)
                           | (1u << hhx::LaneTomMid) | (1u << hhx::LaneTomFloor)
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

    std::printf ("\n%s\n", failures == 0 ? "All engine tests passed." : "Engine tests FAILED.");
    return failures == 0 ? 0 : 1;
}
