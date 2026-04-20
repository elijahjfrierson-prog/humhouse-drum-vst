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
    };
}
