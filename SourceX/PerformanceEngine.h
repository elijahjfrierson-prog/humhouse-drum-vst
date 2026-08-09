#pragma once

#include "GrooveCorpus.h"

#include <cstdint>
#include <vector>

namespace hhx
{
    /** One arrangement span read from the host's markers. */
    struct SectionSpan
    {
        int startBar = 0;
        int numBars  = 0;
        int section  = SectionVerse;
    };

    /** Everything the performance depends on. Two identical settings structs
        always produce identical output - that is what makes golden-render
        tests possible.
    */
    struct PerformanceSettings
    {
        // --- performance ------------------------------------------------
        float complexity   = 0.45f;
        float intensity    = 0.55f;
        int   character    = 1;        // index into the corpus character table

        // --- fills ------------------------------------------------------
        float fillAmount     = 0.35f;
        float fillComplexity = 0.5f;
        float fillVelVar     = 0.3f;
        float fillLengthBars = 1.0f;   // 0.5, 1 or 2
        std::uint8_t  fillStyleMask = 0;               // 0 = any style
        std::uint32_t fillLaneMask  = 0xFFFFFFFFu;

        // --- feel -------------------------------------------------------
        float swing          = 0.0f;
        bool  swingSixteenth = false;
        float humanize       = 0.5f;   // how much of the drummer's deviation to keep
        float feel           = 0.5f;   // 0 = laid back, 1 = pushed
        float ghostAmount    = 0.5f;
        float hatOpenness    = 0.0f;
        float kickVariation  = 0.3f;
        bool  rideInsteadOfHat = false;
        bool  halfTime         = false;

        // --- kit / metre ------------------------------------------------
        std::uint32_t laneMask  = 0xFFFFFFFFu;
        std::uint32_t ghostMask = 0;   // lanes forced to ghost level
        float beatsPerBar = 4.0f;      // in quarter notes
        int   timeSigNum  = 4;
        int   timeSigDen  = 4;
        int   phraseBars  = 2;

        int   variationRhythm = 0;
        int   variationCymbal = 0;
        bool  followSections  = false;
        std::vector<SectionSpan> sections;

        std::uint64_t seed = 1;
    };

    /** Selection, variation and humanization. Knows nothing about samples.

        Rendering is pure and deterministic: (settings, bar) always yields the
        same hits, so the host can ask for any window at any time and the
        result is stitch-free.
    */
    class PerformanceEngine
    {
    public:
        void setCorpus (const GrooveCorpus* c) { corpus = c; }

        std::vector<Hit> renderBars (const PerformanceSettings& s,
                                     int startBar,
                                     int numBars) const;

        std::vector<Hit> renderPhrasePreview (const PerformanceSettings& s,
                                              int phraseIndex) const;

        bool phraseEndsWithFill (const PerformanceSettings& s, int phraseIndex) const;

        /** Where the current XY position lands: the nearest real takes, for the
            landing-zone display on the performance page.
        */
        std::vector<int> landingZone (const PerformanceSettings& s, int maxResults) const;

        int sectionAtBar (const PerformanceSettings& s, int bar) const;

    private:
        /** A hit mid-render: the grid position it has been folded onto plus the
            drummer's own deviation, still separate so Humanize can scale it.
        */
        struct Raw
        {
            float        beat     = 0.0f;
            float        dev      = 0.0f;
            std::uint8_t lane     = LaneKick;
            std::uint8_t velocity = 100;
        };

        struct Sources
        {
            const Phrase* skeleton = nullptr;   // kick / snare / toms
            const Phrase* colour   = nullptr;   // hats / ride / cymbals
        };

        Sources pickSources (const PerformanceSettings& s,
                             int phraseIndex,
                             std::uint64_t seed) const;

        std::vector<Hit> renderPhrase (const PerformanceSettings& s,
                                       int phraseIndex,
                                       bool includeFill) const;

        void appendFill (const PerformanceSettings& s,
                         int   phraseIndex,
                         float fillStartBeat,
                         float phraseBeats,
                         std::uint64_t seed,
                         std::vector<Raw>& raw) const;

        int fillIndexForPhrase (const PerformanceSettings& s, int phraseIndex) const;

        std::uint16_t characterMask (const PerformanceSettings& s) const;

        const GrooveCorpus* corpus = nullptr;
    };
}
