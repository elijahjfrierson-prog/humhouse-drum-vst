#pragma once

#include "GrooveCorpus.h"

#include <cstdint>
#include <vector>

namespace hhx
{
    /** Everything the UI can change about the performance. Values are the
        plain 0..1 / enum forms the APVTS stores.
    */
    struct PerformanceSettings
    {
        float complexity     = 0.45f;
        float intensity      = 0.55f;

        float fillAmount     = 0.35f;   // how often a phrase ends on a fill
        float fillComplexity = 0.5f;
        int   fillBars       = 1;       // 1 or 2
        std::uint32_t fillLaneMask = 0xFFFFFFFFu;

        float swing          = 0.0f;    // 0..1
        bool  swingSixteenth = false;   // false = 8th grid

        float humanize       = 0.5f;    // scales the corpus micro-timing + drift
        float feel           = 0.5f;    // 0 = push, 0.5 = neutral, 1 = laid back
        float ghostAmount    = 0.5f;
        float hatOpenness    = 0.0f;    // 0 = closed, 1 = fully open ostinato
        bool  rideInsteadOfHat = false;
        bool  halfTime       = false;

        /** Bit per lane; a cleared bit removes that kit piece from the
            performance (Logic's kit-piece selector).
        */
        std::uint32_t laneMask = 0xFFFFFFFFu;

        // Bar length in quarter notes: numerator * 4 / denominator.
        // 4/4 → 4, 3/4 → 3, 6/8 → 3, 7/8 → 3.5.
        float beatsPerBar    = 4.0f;
        int   timeSigNum     = 4;
        int   timeSigDen     = 4;
        int   phraseBars     = 2;

        /** Per-group variation index, driven by the numbered variation
            buttons on the MAIN page (0 = "the closest take").
        */
        int   variationRhythm = 0;      // kick / snare group
        int   variationCymbal = 0;      // hats / ride / crash group

        std::uint64_t seed = 1;
    };

    /** Turns corpus phrases into a concrete, bar-indexed performance.

        Deterministic: (settings, seed, barIndex) always produces identical
        output, so nothing drifts between the UI preview, playback and export.
    */
    class PerformanceEngine
    {
    public:
        void setCorpus (const GrooveCorpus* c) { corpus = c; }

        /** Renders `numBars` starting at `startBar`. Returned hit beats are
            absolute, measured from bar 0 of the performance.
        */
        std::vector<Hit> renderBars (const PerformanceSettings& s,
                                     int startBar,
                                     int numBars) const;

        /** One phrase's worth of hits, positions relative to the phrase.
            Used by the MAIN page dot strips so the UI shows the real pattern.
        */
        std::vector<Hit> renderPhrasePreview (const PerformanceSettings& s,
                                              int phraseIndex) const;

        /** True when the phrase starting at `startBar` ends on a fill. */
        bool phraseEndsWithFill (const PerformanceSettings& s, int phraseIndex) const;

    private:
        std::vector<Hit> renderPhrase (const PerformanceSettings& s,
                                       int phraseIndex,
                                       bool includeFill) const;

        const GrooveCorpus* corpus = nullptr;
    };
}
