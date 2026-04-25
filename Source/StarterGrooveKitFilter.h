// v1.6.1-rc.7 — starter grooves now include user-supplied BFD3 palettes.
// Merged library = analyzer-generated StarterGrooves (119) + BFD palette
// extracts (~55). Kit filter is still a pass-through (single bundled kit
// world), but indices now span the merged list.
#pragma once

#include "StarterGrooves.generated.h"
#include "BfdPaletteGrooves.generated.h"

#include <string_view>
#include <vector>

namespace aidrum
{
    // v1.6.1-rc.7 — concatenation of the analyzer-generated STARTER library
    // and the BFD palette extracts. This is the canonical list used by
    // every UI + processor callsite; indices are stable because the two
    // halves never reorder at runtime.
    inline const std::vector<StarterGroove>& allStarterGrooves()
    {
        static const std::vector<StarterGroove> kAll = []
        {
            const auto& a = starterGrooveLibrary();
            const auto& b = bfdPaletteGrooveLibrary();
            std::vector<StarterGroove> v;
            v.reserve (a.size() + b.size());
            for (const auto& x : a) v.push_back (x);
            for (const auto& x : b) v.push_back (x);
            return v;
        } ();
        return kAll;
    }

    // Single-kit world: the kit index is irrelevant. Kept as a free
    // function so callsites (PluginEditor, PluginProcessor) don't need
    // to know the kit count has collapsed to one.
    inline int kitIndexForStarterGroove (std::string_view /*name*/)
    {
        return 0;
    }

    // Returns indices (into allStarterGrooves()) of grooves available
    // for the given kit. With a single bundled kit this is always the
    // full merged library.
    inline const std::vector<int>& starterIndicesForKit (int /*kitIndex*/)
    {
        static const std::vector<int> kAllIdx = []
        {
            const auto& lib = allStarterGrooves();
            std::vector<int> all;
            all.reserve (lib.size());
            for (int i = 0; i < static_cast<int> (lib.size()); ++i)
                all.push_back (i);
            return all;
        } ();
        return kAllIdx;
    }
}
