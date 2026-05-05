// v1.6.1-rc.11 — starter grooves now include user-supplied SoCal centerstones
// + BFD3 palettes + analyzer-generated starters. Order is intentional:
// SoCal entries land FIRST so they sit at the top of the dropdown as the
// featured "centerstone" grooves, then the analyzer library, then the BFD
// palettes. Indices are stable because the three sections never reorder at
// runtime.
#pragma once

#include "StarterGrooves.generated.h"
#include "SoCalGrooves.generated.h"
#include "BfdPaletteGrooves.generated.h"

#include <string_view>
#include <vector>

namespace aidrum
{
    // v1.6.1-rc.11 — canonical concatenation of (SoCal centerstones |
    // analyzer STARTERS | BFD palettes). Used by every UI + processor
    // callsite.
    inline const std::vector<StarterGroove>& allStarterGrooves()
    {
        static const std::vector<StarterGroove> kAll = []
        {
            const auto& s = socalGrooveLibrary();
            const auto& a = starterGrooveLibrary();
            const auto& b = bfdPaletteGrooveLibrary();
            std::vector<StarterGroove> v;
            v.reserve (s.size() + a.size() + b.size());
            for (const auto& x : s) v.push_back (x);
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

    // v1.6.1-rc.24 — SoCal-only subset, used by the COMPOSE / cycler /
    // RANDOMIZE pads. The user explicitly asked for "intelligence pad
    // grooves only randomized grooves added" + "fills must always be in
    // the patterns" + "those imports have fills baked in". The SoCal
    // entries are the imported-with-fills set; analyzer + BFD palettes
    // are basic patterns that don't carry baked-in fills. Both pads now
    // pull from this subset so every region the user lands has a fill
    // present (either baked-in from SoCal or spliced via
    // spliceMandatoryFillIntoRegion). The full merged library is still
    // exposed via starterIndicesForKit() for the explicit STARTER
    // dropdown so the user can audition the analyzer/BFD entries
    // manually if they want.
    //
    // SoCal grooves are concatenated FIRST in allStarterGrooves(), so
    // their indices are simply [0 .. socalGrooveLibrary().size()-1].
    inline const std::vector<int>& socalIndicesForKit (int /*kitIndex*/)
    {
        static const std::vector<int> kSoCalIdx = []
        {
            const auto& s = socalGrooveLibrary();
            std::vector<int> v;
            v.reserve (s.size());
            for (int i = 0; i < static_cast<int> (s.size()); ++i)
                v.push_back (i);
            return v;
        } ();
        return kSoCalIdx;
    }
}
