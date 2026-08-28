#!/usr/bin/env python3
"""Compile the human-performance groove corpus for HumHouse Drums X.

Source data: Magenta Groove MIDI Dataset (GMD), CC-BY 4.0 - real drummers
playing to a click on a Roland TD-11, so every note carries genuine
micro-timing and velocity.

The compiler is where all the offline analysis lives:

  * maps the TD-11 kit onto the canonical 30-piece articulation map,
    deriving ghost notes, flams, buzz rolls and hi-hat openness steps that
    the raw MIDI only implies;
  * slices performances into bar-aligned phrases and scores each on the
    complexity / loudness plane the XY pad navigates;
  * splits every onset into a musical grid position plus the drummer's own
    deviation from it, so the runtime Feel control can scale real human
    timing instead of adding noise;
  * classifies phrases into song sections and fills into playing styles;
  * assigns phrases to characters (Rock, Hard Rock, Punk, ...);
  * learns a per-lane velocity transition matrix used for correlated
    velocity variation at playback time.

Usage:
    python3 tools/corpusx/build_corpus.py --gmd /path/to/groove \
        --out content/rock_corpus.hhc --manifest content/content_manifest.json
"""

from __future__ import annotations

import argparse
import csv
import json
import struct
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

import mido

import lanes as L

MAGIC = b"HHCX"
FORMAT_VERSION = 4

BEATS_PER_BAR = 4.0
GRID = 48                      # grid units per beat (holds 16ths and triplets)
DEV_UNITS = 512.0              # deviation units per beat

KIND_BEAT, KIND_FILL = 0, 1

SEC_INTRO, SEC_VERSE, SEC_CHORUS, SEC_BRIDGE, SEC_OUTRO, SEC_FILL = range(6)

FS_STRAIGHT, FS_TRIPLET, FS_ROLL, FS_SYNCOPATED, FS_TOM_LED, FS_CYMBAL_LED = (
    1, 2, 4, 8, 16, 32)

VEL_BUCKETS = 8

# Characters are landing spots on one corpus, exactly like Logic's drummers:
# a source-style filter plus a preferred region of the complexity/loudness
# plane. A phrase may belong to several characters.
CHARACTERS = [
    # name,          styles,                                   cx,   int,  radius
    ("Pop Rock",     ("pop", "rock/indie", "rock"),            0.32, 0.42, 0.30),
    ("Rock",         ("rock", "rock/groove8"),                 0.48, 0.55, 0.30),
    ("Hard Rock",    ("rock", "funk/rock", "punk"),            0.58, 0.78, 0.30),
    ("Punk",         ("punk", "rock"),                         0.70, 0.86, 0.32),
    ("Metal",        ("punk", "rock", "rock/prog"),            0.82, 0.90, 0.30),
    ("Shuffle",      ("rock/shuffle", "blues/shuffle",
                      "funk/purdieshuffle", "neworleans"),     0.50, 0.55, 0.40),
    ("Half Time",    ("rock/halftime", "rock"),                0.38, 0.60, 0.32),
    ("Roots Rock",   ("country", "rock/rockabilly", "rock/folk",
                      "neworleans/funk", "soul"),              0.42, 0.50, 0.34),
    ("Prog",         ("rock/prog", "jazz/fusion", "funk"),     0.78, 0.62, 0.34),
    # Radius 0: no source take joins this character, it is built by
    # double_kick() from the hardest-hitting takes (see that function).
    ("Metal 2x Kick", ("punk", "rock", "rock/prog"),           0.92, 0.94, 0.0),
]

# Source styles we accept at all. Everything else is a different product.
STYLE_PREFIXES = ("rock", "punk", "pop", "country", "blues", "funk",
                  "soul", "neworleans", "jazz/fusion")


@dataclass
class Note:
    lane: int
    beat: float
    velocity: int


@dataclass
class Phrase:
    kind: int
    bars: int
    bpm: int
    style: str
    drummer: str
    sig_num: int = 4
    sig_den: int = 4
    notes: list[Note] = field(default_factory=list)
    derived: bool = False
    complexity: float = 0.0
    intensity: float = 0.0
    section: int = SEC_VERSE
    fill_styles: int = 0
    swing: float = 0.0
    char_mask: int = 0

    @property
    def beats_per_bar(self) -> float:
        return self.sig_num * 4.0 / self.sig_den


# --------------------------------------------------------------------------
# reading
# --------------------------------------------------------------------------
def read_notes(path: Path) -> list[Note]:
    """Flattens a GMD file into lane/beat/velocity onsets.

    Hi-hat pedal position (CC4) is tracked so that open-hat onsets land on
    the right rung of the openness ladder rather than collapsing to one
    "open hat" articulation.
    """
    mid = mido.MidiFile(path)
    tpq = mid.ticks_per_beat
    notes: list[Note] = []
    for track in mid.tracks:
        tick = 0
        pedal = 0                      # 0 = fully open, 127 = clamped shut
        for msg in track:
            tick += msg.time
            if msg.type == "control_change" and msg.control == 4:
                pedal = msg.value
                continue
            if msg.type != "note_on" or msg.velocity == 0:
                continue
            lane = L.TD11_TO_LANE.get(msg.note)
            if lane is None:
                continue
            if lane in (L.HAT_OPEN2, L.HAT_OPEN3):
                # 0..127 pedal -> four openness steps, most open first.
                step = 3 - min(3, pedal // 32)
                lane = L.HAT_LADDER[2 + step]
            notes.append(Note(lane, tick / tpq, msg.velocity))
    notes.sort(key=lambda n: n.beat)
    return notes


def refine_snare(notes: list[Note]) -> list[Note]:
    """Splits the raw snare lane into ghosts, flams and buzz rolls."""
    out: list[Note] = []
    snare_idx = [i for i, n in enumerate(notes) if n.lane == L.SNARE]
    flammed: set[int] = set()
    rolled: set[int] = set()

    for a, b in zip(snare_idx, snare_idx[1:]):
        gap = notes[b].beat - notes[a].beat
        if gap <= 0.06 and b not in flammed:
            # grace note immediately before a stroke = flam
            flammed.add(a)
            flammed.add(b)

    # A run of >= 3 tightly spaced snare strokes is a roll, not three hits.
    run: list[int] = []
    for i in snare_idx:
        if run and notes[i].beat - notes[run[-1]].beat > 0.2:
            if len(run) >= 3:
                rolled.update(run)
            run = []
        run.append(i)
    if len(run) >= 3:
        rolled.update(run)

    for i, n in enumerate(notes):
        lane = n.lane
        if lane == L.SNARE:
            if i in rolled:
                lane = L.SNARE_ROLL
            elif i in flammed:
                lane = L.SNARE_FLAM
            elif n.velocity < 42:
                lane = L.SNARE_GHOST
        out.append(Note(lane, n.beat, n.velocity))
    return out


# --------------------------------------------------------------------------
# scoring
# --------------------------------------------------------------------------
def score_complexity(notes: list[Note], bars: float) -> float:
    """0..1 from onset density, syncopation and limb independence.

    `bars` is measured in 4/4 bars, so an odd metre is scored on the same
    density scale as everything else.
    """
    if not notes:
        return 0.0
    span = bars * BEATS_PER_BAR
    density_score = min(len(notes) / span / 6.0, 1.0)

    syncopated = 0
    for n in notes:
        pos16 = (n.beat * 4.0) % 4.0
        frac = abs(pos16 - round(pos16))
        slot = round(pos16) % 4
        if frac > 0.25 or slot in (1, 3):
            syncopated += 1
    sync_score = min(syncopated / len(notes) * 2.0, 1.0)

    # Limb independence: how many distinct limbs are busy at once, measured
    # as simultaneous onsets across families.
    families = (L.SNARE_FAMILY, L.TOM_FAMILY, L.CYMBAL_FAMILY,
                L.RIDE_FAMILY, L.HAT_FAMILY, {L.KICK})
    used = sum(1 for f in families if any(n.lane in f for n in notes))
    limb_score = min(used / 4.0, 1.0)

    return max(0.0, min(1.0, 0.45 * density_score + 0.3 * sync_score
                        + 0.25 * limb_score))


def score_musicality(notes: list[Note], bars: int, beats_per_bar: float) -> float:
    """0..1 - how much the take sounds like a song rather than a warm-up.

    The library is a practice room: alongside the songs it holds fills played
    over no groove, hand-drills, bars where the drummer lost the click and
    bars stuffed past thirty hits. Those are the takes that read as "spray",
    and no amount of downstream thinning rescues them, so they are scored
    here and the poor ones never reach the corpus at all.
    """
    if not notes:
        return 0.0

    span = bars * beats_per_bar
    kicks = [n for n in notes if n.lane == L.KICK]
    snares = [n for n in notes if n.lane in L.SNARE_FAMILY
              and n.lane != L.SNARE_GHOST]
    pulse = [n for n in notes if n.lane in L.HAT_FAMILY or n.lane in L.RIDE_FAMILY]

    # A backbeat: something cracks in the second half of every bar, and the
    # kick states the downbeat. Without both, the bar has no floor.
    backbeat = 0
    downbeat = 0
    for bar in range(bars):
        base = bar * beats_per_bar
        if any(base + beats_per_bar * 0.4 <= n.beat < base + beats_per_bar * 0.95
               for n in snares):
            backbeat += 1
        if any(abs(n.beat - base) < 0.35 for n in kicks):
            downbeat += 1
    foundation = 0.65 * (backbeat / bars) + 0.35 * (downbeat / bars)

    # On the grid: a drummer pushes and pulls by a few milliseconds, they do
    # not land between the 16ths. Triplets count as on the grid too.
    on_grid = 0
    for n in notes:
        s16 = n.beat * 4.0
        s12 = n.beat * 3.0
        if min(abs(s16 - round(s16)) / 4.0, abs(s12 - round(s12)) / 3.0) < 0.055:
            on_grid += 1
    grid = on_grid / len(notes)

    # A steady subdivision under the groove. Hats or ride that wander in and
    # out at random intervals are a drill, not a part.
    steady = 0.5
    if len(pulse) >= 4:
        gaps = [b.beat - a.beat for a, b in zip(pulse, pulse[1:])
                if b.beat - a.beat < beats_per_bar]
        if gaps:
            common = max(set(round(g, 2) for g in gaps),
                         key=lambda g: sum(1 for x in gaps if abs(x - g) < 0.06))
            steady = sum(1 for g in gaps if abs(g - common) < 0.06) / len(gaps)

    # Density sanity: rock lives near 8-16 hits a bar; past the mid twenties
    # every stroke masks the last one.
    per_bar = len(notes) / bars
    if per_bar <= 18.0:
        room = 1.0
    else:
        room = max(0.0, 1.0 - (per_bar - 18.0) / 12.0)

    # Clutter: more than three drums struck together is a crash landing, not
    # a voicing.
    clutter = 0
    i = 0
    while i < len(notes):
        j = i
        while j < len(notes) and notes[j].beat - notes[i].beat < 0.04:
            j += 1
        if j - i > 3:
            clutter += j - i
        i = max(j, i + 1)
    tidy = max(0.0, 1.0 - clutter / len(notes) * 3.0)

    # A part repeats. A drummer playing a song plays the same bar again with
    # small differences; a warm-up or a run of ideas never states the same
    # figure twice, which is exactly what "spray" sounds like. Compared as sets
    # of (lane, sixteenth) so a few ghost notes or a moved hat do not count
    # against the take.
    def figure(lo: float, hi: float) -> set[tuple[int, int]]:
        return {(n.lane, round((n.beat - lo) * 4.0)) for n in notes
                if lo <= n.beat < hi}

    halves = [(bar * beats_per_bar, (bar + 1) * beats_per_bar)
              for bar in range(bars)] if bars >= 2 else \
             [(0.0, beats_per_bar * 0.5), (beats_per_bar * 0.5, beats_per_bar)]
    scores = []
    for (a_lo, a_hi), (b_lo, b_hi) in zip(halves, halves[1:]):
        a, b = figure(a_lo, a_hi), figure(b_lo, b_hi)
        if a or b:
            scores.append(len(a & b) / max(1, len(a | b)))
    stated = sum(scores) / len(scores) if scores else 0.5
    # Two statements of the same figure with ornament differences sit near
    # 0.5, which is a part; the scale is stretched so that reads as good.
    stated = min(1.0, stated * 1.9)

    # No dead patches: something - time keeping, kick or snare - marks every
    # beat of the bar, the way a played groove does.
    marked = 0
    for bar in range(bars):
        for b in range(int(beats_per_bar)):
            at = bar * beats_per_bar + b
            if any(abs(n.beat - at) < 0.3 or at < n.beat < at + 1.0
                   for n in notes):
                marked += 1
    covered = marked / max(1, bars * int(beats_per_bar))

    # Silence in the middle of a groove means the take stopped, or the slice
    # caught the end of one.
    edges = [0.0] + [n.beat for n in notes] + [span]
    hole = max(b - a for a, b in zip(edges, edges[1:]))
    flowing = 1.0 if hole <= beats_per_bar else max(0.0, 1.0 - (hole - beats_per_bar))

    return (0.27 * foundation + 0.16 * grid + 0.13 * steady
            + 0.11 * room + 0.06 * tidy + 0.06 * flowing
            + 0.13 * stated + 0.08 * covered)


# A groove has to hold up on its own, so it is judged hard. A fill is a
# departure by definition - it only has to be played in time and stay tidy.
MIN_MUSICALITY_BEAT = 0.80
MIN_MUSICALITY_FILL = 0.42

# A groove the pad can hold: no more strokes a bar than a drummer plays as a
# part, time that never stops, and a backbeat that answers. Past this it is a
# fill, and the busy corner of the pad reading as one long fill is exactly
# what it must not do.
MAX_GROOVE_HITS_PER_BAR = 26


def plays_as_groove(notes: list[Note], bars: int, beats_per_bar: float) -> bool:
    if not notes or bars < 1:
        return False
    if len(notes) > MAX_GROOVE_HITS_PER_BAR * bars:
        return False

    span = bars * beats_per_bar
    time_lanes = L.HAT_FAMILY | {L.RIDE_BOW, L.RIDE_BELL, L.RIDE_EDGE} \
                 | L.CYMBAL_FAMILY
    edges = [0.0] + sorted(n.beat for n in notes
                           if n.lane in time_lanes) + [span]
    if max(b - a for a, b in zip(edges, edges[1:])) > beats_per_bar:
        return False

    # Somewhere in every bar a snare has to answer - on 2 or 4 straight, on 3
    # in a half-time feel - otherwise the bar has no shape to play against.
    for b in range(bars):
        answers = [n for n in notes
                   if n.lane in L.SNARE_FAMILY
                   and b * beats_per_bar <= n.beat < (b + 1) * beats_per_bar]
        if not any(abs((n.beat % beats_per_bar) - want) < 0.3
                   for n in answers for want in (1.0, 2.0, 3.0)):
            return False
    return True


def score_intensity(notes: list[Note]) -> float:
    """0..1 - how hard the phrase is hit, weighted toward backbeat lanes."""
    if not notes:
        return 0.0
    weights = {L.KICK: 1.2, L.SNARE: 1.4, L.SNARE_RIM: 1.4, L.SNARE_FLAM: 1.4,
               L.CRASH_L: 1.3, L.CRASH_R: 1.3, L.CRASH_3: 1.3, L.CHINA: 1.3,
               L.TOM_1: 1.1, L.TOM_2: 1.1, L.TOM_3: 1.1, L.TOM_4: 1.1,
               L.SNARE_GHOST: 0.5, L.HAT_PEDAL: 0.6}
    total = sum(n.velocity * weights.get(n.lane, 1.0) for n in notes)
    weight = sum(weights.get(n.lane, 1.0) for n in notes)
    mean = total / weight / 127.0
    loud = sum(1 for n in notes if n.velocity >= 100) / len(notes)
    return max(0.0, min(1.0, 0.75 * mean + 0.25 * loud))


def measure_swing(notes: list[Note]) -> float:
    """How late the off-beat 8ths sit, 0 = straight, 1 = full triplet."""
    offs = []
    for n in notes:
        if n.lane not in L.HAT_FAMILY and n.lane not in L.RIDE_FAMILY:
            continue
        pos = n.beat % 1.0
        if 0.35 < pos < 0.75:
            offs.append(pos)
    if len(offs) < 4:
        return 0.0
    mean = sum(offs) / len(offs)
    # 0.5 = straight 8th, 0.667 = triplet.
    return max(0.0, min(1.0, (mean - 0.5) / (2.0 / 3.0 - 0.5)))


def classify_section(p: Phrase) -> int:
    """Coarse song-section tag, used for section-aware performance."""
    notes = p.notes
    if p.kind == KIND_FILL:
        return SEC_FILL
    crash_on_one = any(n.lane in L.CYMBAL_FAMILY and (n.beat % BEATS_PER_BAR) < 0.2
                       for n in notes)
    ride_led = sum(1 for n in notes if n.lane in L.RIDE_FAMILY) > \
        sum(1 for n in notes if n.lane in L.HAT_FAMILY)
    sidestick = sum(1 for n in notes if n.lane in (L.SIDE_STICK, L.SNARE_GHOST)) \
        > len(notes) * 0.15

    if crash_on_one and p.intensity > 0.62:
        return SEC_CHORUS
    if sidestick or (ride_led and p.intensity < 0.5):
        return SEC_BRIDGE
    if p.intensity < 0.38 and p.complexity < 0.4:
        return SEC_INTRO
    return SEC_VERSE


def classify_fill_style(p: Phrase) -> int:
    notes = p.notes
    if not notes:
        return FS_STRAIGHT
    mask = 0
    trip = sum(1 for n in notes
               if min(abs((n.beat * 3.0) % 1.0), 1.0 - (n.beat * 3.0) % 1.0) < 0.08
               and abs((n.beat * 4.0) % 1.0) > 0.12)
    if trip >= max(3, len(notes) // 4):
        mask |= FS_TRIPLET
    if any(n.lane == L.SNARE_ROLL for n in notes):
        mask |= FS_ROLL
    toms = sum(1 for n in notes if n.lane in L.TOM_FAMILY)
    if toms >= len(notes) * 0.35:
        mask |= FS_TOM_LED
    cyms = sum(1 for n in notes if n.lane in L.CYMBAL_FAMILY)
    if cyms >= len(notes) * 0.25:
        mask |= FS_CYMBAL_LED
    off = sum(1 for n in notes if abs((n.beat * 4.0) % 1.0 - 0.5) < 0.2)
    if off >= len(notes) * 0.3:
        mask |= FS_SYNCOPATED
    if mask == 0 or not (mask & (FS_TRIPLET | FS_ROLL | FS_SYNCOPATED)):
        mask |= FS_STRAIGHT
    return mask


# How many characters one take may belong to, and how much further than the
# nearest accepting character a rival may sit and still claim it. Generic
# "rock" is accepted by six characters whose radii overlap, so without this a
# single bar joins most of the roster and Pop Rock plays Hard Rock's grooves.
MAX_CHARACTERS_PER_TAKE = 2
CHARACTER_MARGIN = 1.18

# Half Time is a feel rather than a region of the plane: the engine plays its
# takes at half rate, so it claims every take it accepts instead of competing
# with the straight-time characters for them.
OPEN_CHARACTERS = ("Half Time",)


def assign_characters(p: Phrase) -> int:
    near: list[tuple[float, int]] = []
    for bit, (_, styles, cx, inten, radius) in enumerate(CHARACTERS):
        if not any(p.style == s or p.style.startswith(s + "/") for s in styles):
            continue
        d = ((p.complexity - cx) ** 2 + (p.intensity - inten) ** 2) ** 0.5
        if d <= radius:
            near.append((d, bit))

    # A take belongs to the character it actually sounds like, plus at most one
    # neighbour close behind it, so each character keeps its own vocabulary of
    # grooves and fills instead of the whole roster sharing one pool.
    mask = sum(1 << bit for _d, bit in near
               if CHARACTERS[bit][0] in OPEN_CHARACTERS)
    if near:
        near.sort()
        limit = near[0][0] * CHARACTER_MARGIN + 0.02
        for d, bit in near[:MAX_CHARACTERS_PER_TAKE]:
            if d <= limit:
                mask |= 1 << bit

    if mask == 0:
        # Never orphan a real take: give it to the nearest character that
        # accepts its style, else to the nearest character outright.
        best, best_d = 0, 9.9
        for bit, (_, styles, cx, inten, radius) in enumerate(CHARACTERS):
            if radius <= 0.0:
                continue
            style_ok = any(p.style == s or p.style.startswith(s + "/")
                           for s in styles)
            d = ((p.complexity - cx) ** 2 + (p.intensity - inten) ** 2) ** 0.5
            d += 0.0 if style_ok else 0.5
            if d < best_d:
                best, best_d = bit, d
        mask = 1 << best
    return mask


# --------------------------------------------------------------------------
# slicing
# --------------------------------------------------------------------------
def slice_phrases(notes: list[Note], bars: int, bpm: int, style: str,
                  drummer: str, kind: int, min_notes: int,
                  sig: tuple[int, int] = (4, 4)) -> list[Phrase]:
    if not notes:
        return []
    beats_per_bar = sig[0] * 4.0 / sig[1]
    span = bars * beats_per_bar
    buckets: dict[int, list[Note]] = defaultdict(list)
    for n in notes:
        buckets[int(n.beat // span)].append(n)

    out: list[Phrase] = []
    for idx in range(int(notes[-1].beat // span) + 1):
        window = buckets.get(idx, [])
        if len(window) < min_notes:
            continue
        if min(n.beat for n in window) - idx * span > 0.75:
            continue          # a tail, not a phrase
        # And the other end: a window whose last strokes fall short of the bar
        # is a take cut off mid-phrase, so it loops with a hole where the turn
        # around should be. Fills are allowed to stop early - they hand over to
        # the downbeat that follows them.
        if kind == KIND_BEAT \
                and (idx + 1) * span - max(n.beat for n in window) > 0.75:
            continue
        rel = [Note(n.lane, n.beat - idx * span, n.velocity) for n in window]
        rel = [n for n in rel if -0.05 <= n.beat < span]
        floor = MIN_MUSICALITY_FILL if kind == KIND_FILL else MIN_MUSICALITY_BEAT
        if score_musicality(rel, bars, beats_per_bar) < floor:
            continue
        p = Phrase(kind=kind, bars=bars, bpm=bpm, style=style, drummer=drummer,
                   sig_num=sig[0], sig_den=sig[1], notes=rel)
        p.complexity = score_complexity(rel, span / BEATS_PER_BAR)
        p.intensity = score_intensity(rel)
        p.swing = measure_swing(rel)
        p.section = classify_section(p)
        p.fill_styles = classify_fill_style(p) if kind == KIND_FILL else 0
        p.char_mask = assign_characters(p)
        out.append(p)
    return out


def build(gmd_root: Path) -> list[Phrase]:
    info = list(csv.DictReader(open(gmd_root / "info.csv")))
    phrases: list[Phrase] = []
    for row in info:
        style = row["style"]
        if not any(style == s or style.startswith(s) for s in STYLE_PREFIXES):
            continue
        num, den = (int(v) for v in row["time_signature"].split("-"))
        path = gmd_root / row["midi_filename"]
        if not path.exists():
            continue
        try:
            notes = refine_snare(read_notes(path))
        except (OSError, ValueError, EOFError):
            continue
        bpm = int(row["bpm"])
        drummer = row["drummer"]
        sig = (num, den)
        scale = num * 4.0 / den / BEATS_PER_BAR
        if row["beat_type"] == "fill":
            phrases += slice_phrases(notes, 1, bpm, style, drummer, KIND_FILL,
                                     max(3, int(5 * scale)), sig)
            phrases += slice_phrases(notes, 2, bpm, style, drummer, KIND_FILL,
                                     max(6, int(10 * scale)), sig)
        else:
            phrases += slice_phrases(notes, 2, bpm, style, drummer, KIND_BEAT,
                                     max(5, int(8 * scale)), sig)
            phrases += slice_phrases(notes, 4, bpm, style, drummer, KIND_BEAT,
                                     max(10, int(16 * scale)), sig)
    return phrases


# --------------------------------------------------------------------------
# densification
# --------------------------------------------------------------------------
ORNAMENT = L.HAT_FAMILY | L.RIDE_FAMILY | {L.SNARE_GHOST, L.PERC}
SKELETON = {L.KICK, L.SNARE, L.SNARE_RIM, L.SNARE_FLAM, L.SNARE_ROLL,
            L.SIDE_STICK} | L.TOM_FAMILY | L.CYMBAL_FAMILY


def axis_cell(value: float) -> int:
    """The XY cell as the runtime sees it, after 8-bit quantisation."""
    return min(9, int(round(value * 255) / 255.0 * 10.0))


def cell_of(p: "Phrase") -> tuple[int, int]:
    return axis_cell(p.complexity), axis_cell(p.intensity)


def thinned(notes: list[Note], keep_every: int) -> list[Note]:
    """Drops ornament strokes - the way a drummer simplifies a groove."""
    out: list[Note] = []
    seen = 0
    for n in notes:
        if n.lane in ORNAMENT:
            seen += 1
            if seen % keep_every != 0:
                continue
        out.append(n)
    return out


def merged(base: list[Note], donor: list[Note]) -> list[Note]:
    """Base skeleton plus a busier real take's ornamentation.

    Both halves stay human: nothing is synthesised, the ornament layer is
    simply borrowed from another performance of the same character.
    """
    out = [n for n in base if n.lane in SKELETON]
    out += [Note(n.lane, n.beat, n.velocity)
            for n in donor if n.lane in ORNAMENT]
    out.sort(key=lambda n: n.beat)
    return out


def scaled(notes: list[Note], gain: float) -> list[Note]:
    return [Note(n.lane, n.beat,
                 max(1, min(127, int(round(n.velocity * gain)))))
            for n in notes]


def hit_intensity(notes: list[Note], target: float) -> list[Note]:
    """Finds the velocity gain that puts the phrase on a loudness row."""
    lo, hi = 0.08, 2.6
    best = notes
    for _ in range(18):
        gain = (lo + hi) / 2.0
        best = scaled(notes, gain)
        if score_intensity(best) < target:
            lo = gain
        else:
            hi = gain
    return best


# Raw complexity/loudness scores only ever occupy the middle of their 0..1
# range - no real drummer plays a groove that scores 0 or 1 - so the axes are
# calibrated to the corpus itself. Both pad extremes then address real takes.
CAL: dict[str, tuple[float, float]] = {"c": (0.0, 1.0), "i": (0.0, 1.0)}


def norm(value: float, axis: str) -> float:
    lo, hi = CAL[axis]
    return max(0.0, min(1.0, (value - lo) / max(1e-6, hi - lo)))


def denorm(value: float, axis: str) -> float:
    lo, hi = CAL[axis]
    return lo + value * (hi - lo)


def calibrate(phrases: list[Phrase]) -> None:
    """Maps the corpus' own score range onto the full pad, then rescales."""
    beats = [p for p in phrases if p.kind == KIND_BEAT]
    if not beats:
        return
    for axis, get in (("c", lambda p: p.complexity),
                      ("i", lambda p: p.intensity)):
        values = sorted(get(p) for p in beats)
        lo = values[len(values) // 100]
        hi = values[max(0, len(values) - 1 - len(values) // 100)]
        CAL[axis] = (lo, hi)
    for p in phrases:
        p.complexity = norm(p.complexity, "c")
        p.intensity = norm(p.intensity, "i")


def stripped(notes: list[Note], drop: set[int]) -> list[Note]:
    return [n for n in notes if n.lane not in drop]


def voicings(base: Phrase, donors: list[Phrase]) -> list[list[Note]]:
    """Re-voicings of one real take, from sparsest to busiest.

    Sparse ends of the pad come from thinning or dropping the ornament and
    colour layers; busy ends come from borrowing the ornaments of a busier
    real take of the same character. No onset is ever invented.
    """
    out = [list(base.notes)]
    bare = stripped(base.notes, ORNAMENT | L.TOM_FAMILY | L.CYMBAL_FAMILY)
    out.append(bare)
    out.append(stripped(base.notes, ORNAMENT))
    for keep in (2, 3, 4, 6):
        out.append(thinned(base.notes, keep))
    for d in donors:
        out.append(merged(base.notes, d.notes))
        out.append(merged(base.notes, d.notes)
                   + [Note(n.lane, n.beat, n.velocity)
                      for n in d.notes if n.lane in L.TOM_FAMILY])
    return [v for v in out if len(v) >= 4]


def derive(base: Phrase, notes: list[Note], want_i: float) -> Phrase:
    notes = hit_intensity(sorted(notes, key=lambda n: n.beat),
                          denorm(want_i, "i"))
    p = Phrase(base.kind, base.bars, base.bpm, base.style, base.drummer,
               base.sig_num, base.sig_den, notes, True)
    p.complexity = norm(score_complexity(notes, p.bars * p.beats_per_bar
                                                  / BEATS_PER_BAR), "c")
    p.intensity = norm(score_intensity(notes), "i")
    p.swing = measure_swing(notes)
    p.section = base.section
    p.fill_styles = base.fill_styles
    p.char_mask = base.char_mask
    return p


DOUBLE_KICK_BIT = next(bit for bit, (name, *_r) in enumerate(CHARACTERS)
                       if name == "Metal 2x Kick")


def kick_runs(bpm: int) -> list[list[float]]:
    """Two-footed kick figures, as offsets within one beat."""
    if bpm >= 168:                       # already fast: 8ths are two feet
        return [[0.0],                   # feet drop out, groove still metal
                [0.0, 0.5],
                [0.0, 0.5, 0.75],        # gallop
                [0.0, 0.25, 0.5, 0.75]]
    return [[0.0, 0.5],                  # sparse: one foot keeps 8ths
            [0.0, 0.25, 0.5, 0.75],      # sustained 16ths
            [0.0, 0.5, 0.75],            # gallop
            [0.0, 0.25, 0.5]]            # broken run


def double_kick(phrases: list[Phrase]) -> list[Phrase]:
    """Builds the Metal 2x Kick character: real takes, two-footed kick lane.

    The GMD was recorded on a single pedal - across its 1150 files the longest
    run of 16th-note kicks is 13 strokes and only five files sustain more than
    five - so a double-kick groove cannot be sampled out of it. This is the one
    place where kick onsets are placed rather than performed. Everything above
    the feet is left exactly as the drummer played it, and the feet borrow that
    drummer's own kick velocities and their deviation from the grid, alternating
    lead and follow foot, so the run breathes instead of machine-gunning.
    """
    added: list[Phrase] = []
    sources = [p for p in phrases
               if p.kind == KIND_BEAT and not p.derived
               and p.intensity >= 0.55 and p.sig_den == 4
               and 88 <= p.bpm <= 200
               and any(p.style.startswith(s) for s in ("rock", "punk"))]
    sources.sort(key=lambda p: (-p.intensity, -p.complexity))
    sources = sources[:220]

    for base in sources:
        kicks = [n for n in base.notes if n.lane == L.KICK]
        if len(kicks) < 3:
            continue

        # The drummer's own foot: how hard, and how far off the grid.
        vels = [n.velocity for n in kicks]
        devs = [n.beat - round(n.beat * 4.0) / 4.0 for n in kicks]
        upper = [n for n in base.notes if n.lane != L.KICK]
        span = base.bars * base.beats_per_bar

        for run in kick_runs(base.bpm):
            feet: list[Note] = []
            i = 0
            beat = 0.0
            while beat < span:
                for off in run:
                    at = beat + off
                    if at >= span:
                        break
                    lead = (i % 2) == 0
                    v = vels[i % len(vels)]
                    if not lead:
                        v = int(round(v * 0.93))      # follow foot sits back
                    dev = devs[i % len(devs)] * (1.0 if lead else 1.35)
                    feet.append(Note(L.KICK, max(0.0, at + dev),
                                     max(1, min(127, v))))
                    i += 1
                beat += 1.0

            notes = sorted(upper + feet, key=lambda n: n.beat)
            for row in (0.40, 0.58, 0.72, 0.86, 0.97):
                added.append(derive(base, notes, row))
                added[-1].char_mask = 1 << DOUBLE_KICK_BIT
    return phrases + added


LOUD_BITS = [bit for bit, (name, *_r) in enumerate(CHARACTERS)
             if name in ("Hard Rock", "Punk", "Metal", "Rock")]
LOUD_MASK = sum(1 << bit for bit in LOUD_BITS)

# Riding the crash on every quarter is a punk/metal chorus sound; plain Rock
# keeps its hats.
CRASH_MASK = sum(1 << bit for bit, (name, *_r) in enumerate(CHARACTERS)
                 if name in ("Hard Rock", "Punk", "Metal", "Metal 2x Kick"))


def crash_time(phrases: list[Phrase]) -> list[Phrase]:
    """Crash-ridden takes: the loud chorus where the cymbal time is crashes.

    Punk, alternative and metal choruses do not keep time on a hat - the
    drummer rides a crash on every quarter (every eighth when the tempo is
    slow enough to carry it). Everything the drummer played below the cymbals
    is untouched; only the timekeeping lane is re-voiced, hand alternating so
    the ride reads as two arms rather than one retriggered sample.
    """
    added: list[Phrase] = []
    sources = [p for p in phrases
               if p.kind == KIND_BEAT and not p.derived
               and p.intensity >= 0.58 and p.sig_den == 4
               and any(p.style.startswith(s) for s in ("rock", "punk"))]
    sources.sort(key=lambda p: (-p.intensity, -p.complexity))
    sources = sources[:260]

    for base in sources:
        keep = [n for n in base.notes
                if n.lane not in L.HAT_FAMILY and n.lane not in L.RIDE_FAMILY
                and n.lane not in L.CYMBAL_FAMILY]
        cymbals = [n for n in base.notes
                   if n.lane in L.HAT_FAMILY or n.lane in L.RIDE_FAMILY]
        if len(keep) < 4 or not cymbals:
            continue

        span = base.bars * base.beats_per_bar
        kicks = sorted(n.beat for n in keep if n.lane == L.KICK)

        # A crash needs time to open: eighths only where the tempo leaves room.
        steps = [1.0] if base.bpm > 150 else [1.0, 0.5]
        for step in steps:
            ride: list[Note] = []
            i = 0
            beat = 0.0
            while beat < span:
                lead = (i % 2) == 0
                # A crash is the loudest thing in the bar, the off hand lands a
                # touch lighter, and a crash landing with a kick digs in. The
                # beat is exact: a crash off the grid is the thing that reads as
                # a mistake rather than as a player.
                v = 116 if lead else 106
                if any(abs(k - beat) < 0.02 for k in kicks):
                    v += 6
                ride.append(Note(L.CRASH_L if lead else L.CRASH_R,
                                 beat, max(1, min(127, v))))
                beat += step
                i += 1

            notes = sorted(keep + ride, key=lambda n: n.beat)
            for row in (0.82, 0.92, 1.0):
                p = derive(base, notes, row)
                # Crash time is a chorus device for the loud characters, and
                # it stays with the characters the source take belongs to so
                # each drummer keeps its own vocabulary.
                p.char_mask = (base.char_mask & CRASH_MASK) or CRASH_MASK
                added.append(p)
    return phrases + added


def double_snare(phrases: list[Phrase]) -> list[Phrase]:
    """Two-snare backbeats: the driving figure the corpus is thin on.

    The extra stroke is the drummer's own snare - its velocity and its
    distance from the grid are borrowed from the strokes either side of it -
    placed either as a double on the backbeat or as the pickup into the next
    one, which is what makes punk and alternative choruses drive.
    """
    added: list[Phrase] = []
    sources = [p for p in phrases
               if p.kind == KIND_BEAT and not p.derived
               and p.intensity >= 0.48 and p.sig_den == 4
               and any(p.style.startswith(s) for s in ("rock", "punk", "pop"))]
    sources.sort(key=lambda p: (-p.intensity, -p.complexity))
    sources = sources[:300]

    for base in sources:
        snares = [n for n in base.notes if n.lane == L.SNARE]
        if len(snares) < 2:
            continue

        occupied = {round(n.beat * 4.0) / 4.0 for n in base.notes
                    if n.lane in (L.SNARE, L.SNARE_GHOST, L.SNARE_RIM)}

        for offset in (0.25, 0.5):
            extra: list[Note] = []
            for i, n in enumerate(snares):
                at = round((n.beat + offset) * 4.0) / 4.0
                if at in occupied:
                    continue
                dev = n.beat - round(n.beat * 4.0) / 4.0
                # The second stroke of a double is played by the off hand, so
                # it is lighter and sits marginally later.
                v = int(round(n.velocity * 0.86))
                extra.append(Note(L.SNARE, at + dev * 1.25, max(1, min(127, v))))
                if i >= 7:
                    break
            if not extra:
                continue

            notes = sorted(base.notes + extra, key=lambda n: n.beat)
            for row in (0.62, 0.78, 0.9):
                p = derive(base, notes, row)
                p.char_mask = (base.char_mask & LOUD_MASK) or LOUD_MASK
                added.append(p)
    return phrases + added


HALF_TIME_BIT = next(bit for bit, (name, *_r) in enumerate(CHARACTERS)
                     if name == "Half Time")


def half_time_recast(phrases: list[Phrase]) -> list[Phrase]:
    """Half-time versions of straight-time takes.

    A half-time feel is how a drummer plays an existing groove, not a separate
    library: the backbeat moves to 3, the cymbal drops to the half-rate pulse
    and the kick keeps only the strokes that carry the bar. Recasting real
    takes this way gives the feel a pool of its own instead of leaving it with
    whatever the straight-time characters did not claim - which is why it was
    the one character with holes in the pad.
    """
    added: list[Phrase] = []
    sources = [p for p in phrases
               if p.kind == KIND_BEAT and not p.derived
               and p.sig_den == 4 and p.sig_num == 4]
    sources.sort(key=lambda p: -score_musicality(p.notes, p.bars,
                                                 p.beats_per_bar))
    sources = sources[:420]

    for base in sources:
        bar = base.beats_per_bar
        notes: list[Note] = []
        for n in base.notes:
            in_bar = n.beat - (n.beat // bar) * bar
            near = round(in_bar * 2.0) / 2.0

            if n.lane in L.SNARE_FAMILY:
                # One backbeat a bar, on 3, and the ghost strokes stay.
                if n.lane == L.SNARE and abs(near - 2.0) > 0.26:
                    continue
            elif n.lane in L.HAT_FAMILY or n.lane in (L.RIDE_BOW, L.RIDE_BELL,
                                                      L.RIDE_EDGE):
                # Time is kept on the quarter, not the eighth.
                if abs(in_bar - round(in_bar)) > 0.12:
                    continue
            elif n.lane == L.KICK:
                if abs(near - 1.5) < 0.26 or abs(near - 3.5) < 0.26:
                    continue
            notes.append(Note(n.lane, n.beat, n.velocity))

        snares = [n for n in notes if n.lane == L.SNARE]
        if len(notes) < 6 or not snares:
            continue

        # The bar has to answer on 3: if the source's backbeat did not land
        # there, the closest snare stroke is moved onto it.
        for b in range(int(base.bars)):
            at = b * bar + 2.0
            if not any(abs(n.beat - at) < 0.26 for n in snares):
                donor = min(snares, key=lambda n: abs((n.beat % bar) - 2.0))
                notes.append(Note(L.SNARE, at, donor.velocity))
        notes.sort(key=lambda n: n.beat)

        for row in (0.2, 0.35, 0.5, 0.65, 0.8, 0.95):
            p = derive(base, notes, row)
            p.char_mask = 1 << HALF_TIME_BIT
            added.append(p)
    return phrases + added


def densify(phrases: list[Phrase]) -> list[Phrase]:
    """Fills every empty complexity x loudness cell, per character.

    A cell is filled by re-voicing the nearest real take of that character -
    thinning or borrowing ornamentation for the complexity axis, scaling the
    velocity curve for the loudness axis - so the drummer's own micro-timing
    is preserved in every cell of the pad.
    """
    added: list[Phrase] = []
    beats = [p for p in phrases if p.kind == KIND_BEAT]
    for bit, _char in enumerate(CHARACTERS):
        mine = [p for p in beats if p.char_mask & (1 << bit)]
        if not mine:
            continue
        empty = {(cx, cy) for cx in range(10) for cy in range(10)}
        empty -= {cell_of(p) for p in mine}

        # Re-voice the character's best-playing takes, not merely its nearest
        # ones, so a corner of the pad is a good bar played harder rather than
        # whatever happened to sit closest to it.
        best = sorted(mine, key=lambda p: -score_musicality(
            p.notes, p.bars, p.beats_per_bar))[:max(24, len(mine) * 3 // 4)]
        ranked = sorted(best, key=lambda p: p.complexity)

        # Ornaments are borrowed from the busiest bars in the library, not only
        # from this character's own, or a sparse character - a half-time feel
        # especially - has nothing to reach the busy side of the pad with.
        donors = ranked[-6:] + sorted(beats, key=lambda p: -p.complexity)[:6]
        step = max(1, len(ranked) // 48)
        bases = ranked[::step]

        # Two passes: hold derived bars to the same standard as recorded ones,
        # then let anything fill whatever cells are still empty, since an empty
        # cell means the pad has nothing to play there at all.
        # The first pass only accepts bars that play as a part; the second
        # keeps the density ceiling but drops the rest, since a cell with
        # nothing in it makes the pad reach across the plane instead.
        for floor, strict in ((MIN_MUSICALITY_BEAT - 0.06, True), (0.0, False)):
            for base in bases:
                if not empty:
                    break
                for notes in voicings(base, donors):
                    cx = axis_cell(norm(score_complexity(
                        notes, base.bars * base.beats_per_bar / BEATS_PER_BAR), "c"))
                    rows = sorted(cy for (bx, cy) in empty if bx == cx)
                    for cy in rows:
                        p = derive(base, notes, (cy + 0.5) / 10.0)
                        if cell_of(p) not in empty:
                            continue
                        if score_musicality(p.notes, p.bars,
                                            p.beats_per_bar) < floor:
                            continue
                        if strict and not plays_as_groove(p.notes, p.bars,
                                                          p.beats_per_bar):
                            continue
                        if len(p.notes) > MAX_GROOVE_HITS_PER_BAR * p.bars:
                            continue
                        empty.discard(cell_of(p))
                        added.append(p)
    return phrases + added


# --------------------------------------------------------------------------
# quantisation + velocity model
# --------------------------------------------------------------------------
def quantise(beat: float) -> tuple[int, int]:
    """Splits an onset into (grid position, human deviation).

    The grid holds both binary and triplet subdivisions, so a shuffled 8th
    keeps its musical identity instead of being recorded as a late 8th.
    """
    raw = beat * GRID
    best = None
    for step in (6, 8):                      # 32nds and triplet 16ths
        cand = round(raw / step) * step
        if best is None or abs(raw - cand) < abs(raw - best):
            best = cand
    dev = int(round((beat - best / GRID) * DEV_UNITS))
    return int(best), max(-127, min(127, dev))


def velocity_model(phrases: list[Phrase]) -> list[list[int]]:
    """Per-lane velocity transition matrix, VEL_BUCKETS x VEL_BUCKETS.

    Learned from consecutive strokes on the same lane so playback can vary
    velocity the way a drummer does (a hard hit is usually followed by a
    softer one) rather than with uniform noise.
    """
    counts = [[[0] * VEL_BUCKETS for _ in range(VEL_BUCKETS)]
              for _ in range(L.NUM_LANES)]
    for p in phrases:
        last: dict[int, int] = {}
        for n in p.notes:
            b = min(VEL_BUCKETS - 1, n.velocity * VEL_BUCKETS // 128)
            if n.lane in last:
                counts[n.lane][last[n.lane]][b] += 1
            last[n.lane] = b

    rows: list[list[int]] = []
    for lane in range(L.NUM_LANES):
        for src in range(VEL_BUCKETS):
            row = counts[lane][src]
            total = sum(row)
            if total == 0:
                rows.append([32] * VEL_BUCKETS)   # uniform fallback
            else:
                rows.append([max(0, min(255, round(v * 255 / total)))
                             for v in row])
    return rows


# --------------------------------------------------------------------------
# output
# --------------------------------------------------------------------------
def write_corpus(phrases: list[Phrase], out: Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    model = velocity_model(phrases)
    with open(out, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<III", FORMAT_VERSION, len(phrases),
                            len(CHARACTERS)))
        for name, *_ in CHARACTERS:
            raw = name.encode("ascii")
            f.write(struct.pack("<B", len(raw)))
            f.write(raw)
        for row in model:
            f.write(bytes(row))
        for p in phrases:
            f.write(struct.pack(
                "<BBBBBBHBHBBBH",
                p.kind,
                p.bars,
                p.sig_num,
                p.sig_den,
                int(round(p.complexity * 255)),
                int(round(p.intensity * 255)),
                p.bpm,
                p.section,
                p.char_mask,
                p.fill_styles,
                int(round(p.swing * 255)),
                1 if p.derived else 0,
                len(p.notes)))
            for n in p.notes:
                grid, dev = quantise(n.beat)
                f.write(struct.pack("<BBHb", n.lane, n.velocity, grid, dev))


def write_manifest(phrases: list[Phrase], out: Path, corpus: Path) -> None:
    # The corpus is often built to a scratch path before being copied in, and
    # the kits and their credits live alongside it: the plug-in looks the
    # library up by the name written here, and the licences require the kit
    # credits to ship, so neither may be lost to a rebuild.
    previous = {}
    if out.exists():
        previous = json.loads(out.read_text())

    per_char = defaultdict(int)
    for p in phrases:
        for bit, (name, *_rest) in enumerate(CHARACTERS):
            if p.char_mask & (1 << bit):
                per_char[name] += 1
    manifest = {
        "corpus": previous.get("corpus") or corpus.name,
        "format_version": FORMAT_VERSION,
        "phrases": len(phrases),
        "beats": sum(1 for p in phrases if p.kind == KIND_BEAT),
        "fills": sum(1 for p in phrases if p.kind == KIND_FILL),
        "derived": sum(1 for p in phrases if p.derived),
        "characters": {k: v for k, v in sorted(per_char.items())},
        "articulations": L.NUM_LANES,
        "attribution": [
            {
                "name": "Groove MIDI Dataset",
                "source": "https://magenta.tensorflow.org/datasets/groove",
                "license": "CC-BY 4.0",
                "credit": "Gillick, Roberts, Engel, Eck, Bamman - "
                          "Learning to Groove with Inverse Sequence "
                          "Transformations (2019)",
            }
        ] + [a for a in previous.get("attribution", [])
             if a.get("name") != "Groove MIDI Dataset"],
    }
    if "kits" in previous:
        manifest["kits"] = previous["kits"]
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, indent=2) + "\n")


def report(phrases: list[Phrase]) -> None:
    beats = [p for p in phrases if p.kind == KIND_BEAT]
    fills = [p for p in phrases if p.kind == KIND_FILL]
    print(f"phrases: {len(phrases)} ({len(beats)} beats, {len(fills)} fills, "
          f"{sum(1 for p in phrases if p.derived)} re-voiced)")
    for bit, (name, *_rest) in enumerate(CHARACTERS):
        mine = [p for p in beats if p.char_mask & (1 << bit)]
        cells = {cell_of(p) for p in mine}
        print(f"  {name:<12} phrases={len(mine):<5} grid={len(cells)}/100")
    cells = {cell_of(p) for p in beats}
    print(f"overall 10x10 cells populated: {len(cells)}/100")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--gmd", required=True, type=Path,
                    help="path to the extracted groove/ folder of the GMD")
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--manifest", type=Path)
    args = ap.parse_args()

    phrases = build(args.gmd)
    calibrate(phrases)
    phrases = double_kick(phrases)
    phrases = crash_time(phrases)
    phrases = double_snare(phrases)
    phrases = half_time_recast(phrases)
    phrases = densify(phrases)
    write_corpus(phrases, args.out)
    if args.manifest:
        write_manifest(phrases, args.manifest, args.out)
    report(phrases)
    print(f"bytes: {args.out.stat().st_size}")


if __name__ == "__main__":
    main()
