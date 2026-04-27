#pragma once

#include <cstdint>
#include <vector>

namespace aidrum
{
    struct MidiNote
    {
        int     noteNumber = 36;   // e.g. 36 = kick, 38 = snare
        float   velocity   = 0.8f; // 0..1
        double  startBeat  = 0.0;  // quarter notes
        double  lengthBeat = 0.25; // quarter notes
    };

    struct MidiPattern
    {
        double                lengthInBeats = 4.0; // one bar of 4/4 by default
        std::vector<MidiNote> notes;

        // v1.6.1-rc.5 — remember whether this region was generated as a
        // Fill (drum roll / transition) or a Groove (backbone). A live-
        // knob regen reads this flag so a Fill region stays a Fill after
        // the user tweaks variation/complexity/etc., instead of silently
        // decaying into a Groove. Starter grooves appended via
        // appendStarterGroove leave this false (they are grooves).
        bool                  isFill = false;

        // v1.6.1-rc.14 — per-region INTENSITY override. 0..1 = explicit
        // velocity scale for this region (overrides the global INTENSITY
        // knob); a sentinel value < 0 means "inherit the global INTENSITY".
        // Lets the user dial pre-chorus soft, chorus slammed, bridge
        // somber by spinning the knob inside each region tile in the
        // arrangement strip without touching neighbouring regions.
        // Consumed by spliceMandatoryFillIntoRegion + shapeVelocity().
        float                 regionIntensity = -1.0f;
    };
}
