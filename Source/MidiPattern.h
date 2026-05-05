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

        // v1.6.1-rc.24 — one-shot flag removed alongside the FL-style
        // chromatic piano-roll component (which was the only writer).
        // Drum samples already play as full-length one-shots inside
        // SampleKit; melodic / chromatic input now flows in via the
        // host's piano roll through the rc.24 host-MIDI capture path
        // and is gated by ordinary MIDI noteOff like every DAW host.
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

        // v1.6.1-rc.20-fix5 — true when the region was committed via
        // commitManualPatternAsRegion (i.e. the user pressed ADD TO
        // ARRANGEMENT while editing the FL-style piano roll). The live
        // render + MIDI export paths skip the GM-drum whitelist (rc.3)
        // for these regions so chromatic synth/pad/phrase notes survive
        // after the user leaves manual mode. AI/STARTER regions leave
        // this false and still get the whitelist scrub.
        bool                  isManualOrigin = false;
    };
}
