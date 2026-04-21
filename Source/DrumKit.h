#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aidrum
{
    // ------------------------------------------------------------------
    // 20 drumkit models (v0.7.0), each labeled by Genre — Brand (Style).
    // Each kit remaps GM drum notes to different articulations so samplers
    // (FL Slicex, Logic Drum Kit Designer, Superior Drummer, Addictive
    // Drums, Kontakt libraries, etc.) render distinct timbres:
    //   - GM 35 "Acoustic Bass Drum" = round/boomy kick
    //   - GM 36 "Bass Drum 1"        = tight/clicky kick
    //   - GM 38 "Acoustic Snare"     = fat wooden snare
    //   - GM 40 "Electric Snare"     = tight gated / synth snare
    //   - GM 37 "Side Stick"         = rim / cross-stick
    //   - GM 39 "Hand Clap"          = clap (hip-hop / pop / 808)
    //   - GM 42/44/46                = closed / pedal / open hats
    //   - GM 49/57 crashes, 51/53 ride/bell, 52 china (metal accents)
    // Velocity/ghost/accent curves shape the feel on top.
    // ------------------------------------------------------------------
    enum class DrumKit : int
    {
        // Jazz
        LudwigBebopJazz = 0,
        GretschCoolJazz,

        // Classic rock / '70s
        LudwigSupraphonicClassicRock,
        LudwigVistaliteSeventiesRock,

        // Hard rock
        TamaRockstarHardRock,
        YamahaStudioHardRock,

        // Shoegaze / indie / grunge
        DWCollectorsShoegaze,
        PearlMastersGrunge,
        YamahaRecordingIndie,

        // Funk / R&B
        LudwigBlackBeautyFunk,
        YamahaLiveCustomFunk,
        DWPerformanceRnB,

        // Metal family
        SonorSQ2Thrash,
        TamaStarclassicMetal,
        PearlReferenceMetalcore,
        YamahaPHXProgMetal,

        // Country
        LudwigClassicMapleCountry,

        // Electronic / pop
        Roland808HipHop,
        Roland909Trap,
        AkaiLayeredPop,

        Count
    };

    struct DrumKitProfile
    {
        // GM note substitutions for each drum voice.
        int  kick       = 36;
        int  snare      = 38;
        int  ghostSnare = 38;   // what low-velocity snare ghosts become
        int  sideStick  = 37;
        int  clap       = 39;
        int  closedHat  = 42;
        int  pedalHat   = 44;
        int  openHat    = 46;
        int  ride       = 51;
        int  rideBell   = 53;
        int  crash      = 49;
        int  crashAlt   = 57;
        int  china      = 52;
        int  lowTom     = 41;
        int  midTom     = 45;
        int  highTom    = 48;

        // Tonality / feel modifiers.
        float velocityScale   = 1.0f;  // overall velocity multiplier
        float ghostBoost      = 1.0f;  // multiplies genre ghost density
        float accentBoost     = 1.0f;  // adds to accent velocity
        float ghostThreshold  = 0.55f; // snares <= this get ghostSnare swap
        bool  preferChinaForFill = false; // metal kits prefer china on fills
    };

    const DrumKitProfile&           drumKitProfile     (DrumKit kit);
    const std::vector<std::string>& drumKitDisplayNames();

    // v1.3.0 Per-kit accent colour (ARGB). Each kit gets a distinct shell
    // tint on the KitVisualizer so the 20 kits look visually different even
    // before you hear them — walnut amber, jet black, sunburst red, etc.
    std::uint32_t drumKitAccent (DrumKit kit);
}
