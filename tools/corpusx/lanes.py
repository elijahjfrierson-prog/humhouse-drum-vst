"""Canonical 30-piece articulation map, shared by the corpus tools.

The ids here are the contract between the compiler, the runtime
PerformanceEngine, the sampler and the MIDI exporter. Keep in sync with
SourceX/GrooveCorpus.h.
"""

from __future__ import annotations

(
    KICK,
    SNARE,
    SNARE_RIM,
    SIDE_STICK,
    SNARE_GHOST,
    SNARE_FLAM,
    SNARE_ROLL,
    HAT_CLOSED,
    HAT_TIGHT,
    HAT_OPEN1,
    HAT_OPEN2,
    HAT_OPEN3,
    HAT_OPEN4,
    HAT_PEDAL,
    HAT_SPLASH,
    HAT_BELL,
    RIDE_BOW,
    RIDE_BELL,
    RIDE_EDGE,
    RIDE_CRASH,
    CRASH_L,
    CRASH_R,
    CRASH_3,
    CHINA,
    SPLASH,
    TOM_1,
    TOM_2,
    TOM_3,
    TOM_4,
    PERC,
) = range(30)

NUM_LANES = 30

NAMES = {
    KICK: "Kick",
    SNARE: "Snare",
    SNARE_RIM: "Snare Rim",
    SIDE_STICK: "Side Stick",
    SNARE_GHOST: "Snare Ghost",
    SNARE_FLAM: "Snare Flam",
    SNARE_ROLL: "Snare Roll",
    HAT_CLOSED: "Hat Closed",
    HAT_TIGHT: "Hat Tight",
    HAT_OPEN1: "Hat Open 1",
    HAT_OPEN2: "Hat Open 2",
    HAT_OPEN3: "Hat Open 3",
    HAT_OPEN4: "Hat Open 4",
    HAT_PEDAL: "Hat Pedal",
    HAT_SPLASH: "Hat Splash",
    HAT_BELL: "Hat Bell",
    RIDE_BOW: "Ride Bow",
    RIDE_BELL: "Ride Bell",
    RIDE_EDGE: "Ride Edge",
    RIDE_CRASH: "Ride Crash",
    CRASH_L: "Crash L",
    CRASH_R: "Crash R",
    CRASH_3: "Crash 3",
    CHINA: "China",
    SPLASH: "Splash",
    TOM_1: "Tom 1",
    TOM_2: "Tom 2",
    TOM_3: "Tom 3",
    TOM_4: "Tom 4",
    PERC: "Perc",
}

# Roland TD-11 note map as documented with the Groove MIDI Dataset.
TD11_TO_LANE = {
    36: KICK,
    38: SNARE,
    40: SNARE_RIM,
    37: SIDE_STICK,
    48: TOM_1,
    50: TOM_1,
    45: TOM_2,
    47: TOM_2,
    43: TOM_3,
    58: TOM_3,
    42: HAT_CLOSED,
    22: HAT_TIGHT,
    46: HAT_OPEN2,
    26: HAT_OPEN3,
    44: HAT_PEDAL,
    49: CRASH_L,
    55: CRASH_L,
    57: CRASH_R,
    52: CRASH_3,
    51: RIDE_BOW,
    59: RIDE_EDGE,
    53: RIDE_BELL,
}

# Hats whose openness the Hat Openness control interpolates between.
HAT_LADDER = [HAT_CLOSED, HAT_TIGHT, HAT_OPEN1, HAT_OPEN2, HAT_OPEN3, HAT_OPEN4]

SNARE_FAMILY = {SNARE, SNARE_RIM, SIDE_STICK, SNARE_GHOST, SNARE_FLAM, SNARE_ROLL}
TOM_FAMILY = {TOM_1, TOM_2, TOM_3, TOM_4}
CYMBAL_FAMILY = {CRASH_L, CRASH_R, CRASH_3, CHINA, SPLASH, RIDE_CRASH}
RIDE_FAMILY = {RIDE_BOW, RIDE_BELL, RIDE_EDGE, RIDE_CRASH}
HAT_FAMILY = set(HAT_LADDER) | {HAT_PEDAL, HAT_SPLASH, HAT_BELL}

# General MIDI note per lane, for export. Several articulations share a GM note
# because GM has no equivalent; the plugin's own MIDI input map is wider.
GM_NOTE = {
    KICK: 36,
    SNARE: 38,
    SNARE_RIM: 40,
    SIDE_STICK: 37,
    SNARE_GHOST: 38,
    SNARE_FLAM: 38,
    SNARE_ROLL: 38,
    HAT_CLOSED: 42,
    HAT_TIGHT: 42,
    HAT_OPEN1: 46,
    HAT_OPEN2: 46,
    HAT_OPEN3: 46,
    HAT_OPEN4: 46,
    HAT_PEDAL: 44,
    HAT_SPLASH: 46,
    HAT_BELL: 42,
    RIDE_BOW: 51,
    RIDE_BELL: 53,
    RIDE_EDGE: 59,
    RIDE_CRASH: 51,
    CRASH_L: 49,
    CRASH_R: 57,
    CRASH_3: 52,
    CHINA: 52,
    SPLASH: 55,
    TOM_1: 48,
    TOM_2: 45,
    TOM_3: 43,
    TOM_4: 41,
    PERC: 56,
}
