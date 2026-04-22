// v1.6.1-rc.3 — map each of the 119 STARTER grooves to one of the 5
// bundled kits so each kit has its own subset of grooves that feel
// right for that kit's personality (Thrash gets punk/double-kick,
// IndieLofi gets brush/shuffle/soft, etc.). The user explicitly asked
// that "a NuRock groove should not sound like an AltRock groove" —
// with no other randomness, the kit + its groove pool is what gives
// each kit its identity.
#pragma once

#include "StarterGrooves.generated.h"

#include <string>
#include <string_view>
#include <vector>

namespace aidrum
{
    // 0 = PopRock, 1 = NuRock, 2 = AltRock, 3 = IndieLofi, 4 = Thrash.
    // Matches PluginEditor.cpp's drumKitBox order.
    inline int kitIndexForStarterGroove (std::string_view name)
    {
        auto contains = [&] (const char* needle)
        {
            return name.find (needle) != std::string_view::npos;
        };

        // Thrash — fastest, double-kick, aggressive.
        if (contains ("DOUBLE+PUNK")
         || contains ("THRASH")
         || contains ("METAL")
         || contains ("PUNK"))
            return 4;

        // IndieLofi — brushed, shuffled, soft half-time.
        if (contains ("60S")
         || contains ("BRUSH")
         || contains ("BEBOP")
         || contains ("CHICAGO+BLUES")
         || contains ("HALF+TIME")
         || contains ("SIDESTICK"))
            return 3;

        // NuRock — syncopated, funk-leaning, busier hats.
        if (contains ("FUNKED")
         || contains ("FUNKY")
         || contains ("JAM")
         || contains ("HATGROOVES")
         || contains ("HEADBOP")
         || contains ("LATIN"))
            return 1;

        // AltRock — laid-back, slightly behind-the-beat rock grooves.
        if (contains ("DRUMGROOVES 0")       // DRUMGROOVES 06-09
         || contains ("DRUMGROOVES 1")       // DRUMGROOVES 10-19
         || contains ("MOREDRUMGROOVES")
         || contains ("EVENMOREDRUM")
         || contains ("SOLID"))
            return 2;

        // PopRock — standard backbeat fallback (BAR BAND BASIC, ESSENTIAL,
        // CROWD, FOUR ON FLOOR, etc.).
        return 0;
    }

    // Returns indices (into starterGrooveLibrary()) of grooves belonging
    // to the given kit. Never empty — if a keyword filter would leave a
    // kit with zero grooves we fall back to the full library.
    inline const std::vector<int>& starterIndicesForKit (int kitIndex)
    {
        static const std::vector<std::vector<int>> kBuckets = []
        {
            const auto& lib = starterGrooveLibrary();
            std::vector<std::vector<int>> buckets (5);
            for (int i = 0; i < static_cast<int> (lib.size()); ++i)
            {
                const int k = kitIndexForStarterGroove (lib[(size_t) i].name);
                buckets[(size_t) k].push_back (i);
            }
            // Distribute evenly: if any bucket is empty, fill it from
            // the global library so the dropdown is never blank.
            std::vector<int> all;
            all.reserve (lib.size());
            for (int i = 0; i < static_cast<int> (lib.size()); ++i) all.push_back (i);
            for (auto& b : buckets) if (b.empty()) b = all;
            return buckets;
        } ();

        if (kitIndex < 0 || kitIndex >= static_cast<int> (kBuckets.size()))
            return kBuckets[0];
        return kBuckets[(size_t) kitIndex];
    }
}
