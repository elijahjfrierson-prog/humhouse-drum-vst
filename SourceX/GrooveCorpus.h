#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hhx
{
    /** Canonical kit lanes. The corpus, the performance engine, the sampler and
        the MIDI exporter all speak these ids; only the exporter knows about GM
        note numbers.
    */
    enum Lane : std::uint8_t
    {
        LaneKick = 0,
        LaneSnare,
        LaneSnareRim,
        LaneSideStick,
        LaneHatClosed,
        LaneHatPedal,
        LaneHatOpen,
        LaneTomHi,
        LaneTomMid,
        LaneTomFloor,
        LaneCrashL,
        LaneCrashR,
        LaneRide,
        LaneRideBell,
        NumLanes
    };

    const char* laneName (int lane);

    /** General MIDI note for a lane — used only when exporting/emitting MIDI. */
    int laneToGmNote (int lane);
    int gmNoteToLane (int note);

    /** A single drum hit. `beat` is relative to the start of its phrase and
        carries the drummer's real micro-timing (it is deliberately not
        quantised).
    */
    struct Hit
    {
        float         beat     = 0.0f;
        std::uint8_t  lane     = LaneKick;
        std::uint8_t  velocity = 100;
    };

    /** One bar-aligned slice of a real performance. */
    struct Phrase
    {
        std::vector<Hit> hits;
        float            complexity = 0.0f;  // 0..1
        float            intensity  = 0.0f;  // 0..1
        int              bars       = 2;
        int              bpm        = 120;
        bool             isFill     = false;
    };

    /** Human-performance groove library.

        Compiled offline by tools/corpusx/build_corpus.py from the Magenta
        Groove MIDI Dataset (CC-BY 4.0). Selection is nearest-neighbour on the
        (complexity, intensity) plane — the plugin never invents a pattern, it
        picks a take a person actually played and varies it.
    */
    class GrooveCorpus
    {
    public:
        bool loadFromMemory (const void* data, std::size_t numBytes);

        bool  isLoaded()   const { return loaded; }
        int   numBeats()   const { return (int) beatPhrases.size(); }
        int   numFills()   const { return (int) fillPhrases.size(); }

        const Phrase& beat (int index) const { return beatPhrases[(std::size_t) index]; }
        const Phrase& fill (int index) const { return fillPhrases[(std::size_t) index]; }

        /** Returns the index of a beat phrase near (complexity, intensity).

            `variation` walks the ranked neighbour list, so the UI's numbered
            variation buttons map straight onto "the Nth closest real take".
        */
        int pickBeat (float complexity, float intensity, int variation) const;

        /** Fill selection, filtered by bar length and by the lanes the user
            allows fills to use. `avoid` holds recently played fills so the
            same one never lands twice in a row.
        */
        int pickFill (float complexity,
                      float intensity,
                      int   bars,
                      int   variation,
                      const std::vector<int>& avoid,
                      std::uint32_t laneMask) const;

    private:
        std::vector<int> rank (const std::vector<Phrase>& pool,
                               float complexity,
                               float intensity,
                               int   maxResults) const;

        std::vector<Phrase> beatPhrases;
        std::vector<Phrase> fillPhrases;
        bool                loaded = false;
    };
}
