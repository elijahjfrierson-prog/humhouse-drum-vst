// v1.6.1-rc.6 — single bundled kit. The rc.3..rc.5 groove-filtering
// (kit-specific buckets so a NuRock groove wouldn't sound like an
// AltRock groove) is no longer meaningful now that there is only one
// bundled kit. Every call returns the full STARTER library; the
// workflow the product is now built around is "bring your own sample
// pack via LOAD KIT", not "pick from 6 built-ins".
#pragma once

#include "StarterGrooves.generated.h"

#include <string_view>
#include <vector>

namespace aidrum
{
    // Single-kit world: the kit index is irrelevant. Kept as a
    // free function so callsites (PluginEditor, PluginProcessor) don't
    // need to know the kit count has collapsed to one.
    inline int kitIndexForStarterGroove (std::string_view /*name*/)
    {
        return 0;
    }

    // Returns indices (into starterGrooveLibrary()) of grooves
    // available for the given kit. With a single bundled kit this
    // is always the full library, independent of kitIndex. Never
    // empty as long as StarterGrooves.generated.h has at least one
    // entry (which the build enforces).
    inline const std::vector<int>& starterIndicesForKit (int /*kitIndex*/)
    {
        static const std::vector<int> kAll = []
        {
            const auto& lib = starterGrooveLibrary();
            std::vector<int> all;
            all.reserve (lib.size());
            for (int i = 0; i < static_cast<int> (lib.size()); ++i)
                all.push_back (i);
            return all;
        } ();
        return kAll;
    }
}
