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
FORMAT_VERSION = 2

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
    notes: list[Note] = field(default_factory=list)
    derived: bool = False
    complexity: float = 0.0
    intensity: float = 0.0
    section: int = SEC_VERSE
    fill_styles: int = 0
    swing: float = 0.0
    char_mask: int = 0


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
def score_complexity(notes: list[Note], bars: int) -> float:
    """0..1 from onset density, syncopation and limb independence."""
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


def assign_characters(p: Phrase) -> int:
    mask = 0
    for bit, (_, styles, cx, inten, radius) in enumerate(CHARACTERS):
        if not any(p.style == s or p.style.startswith(s + "/") for s in styles):
            continue
        d = ((p.complexity - cx) ** 2 + (p.intensity - inten) ** 2) ** 0.5
        if d <= radius:
            mask |= 1 << bit
    if mask == 0:
        # Never orphan a real take: give it to the nearest character that
        # accepts its style, else to the nearest character outright.
        best, best_d = 0, 9.9
        for bit, (_, styles, cx, inten, _r) in enumerate(CHARACTERS):
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
                  drummer: str, kind: int, min_notes: int) -> list[Phrase]:
    if not notes:
        return []
    span = bars * BEATS_PER_BAR
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
        rel = [Note(n.lane, n.beat - idx * span, n.velocity) for n in window]
        rel = [n for n in rel if -0.05 <= n.beat < span]
        p = Phrase(kind=kind, bars=bars, bpm=bpm, style=style, drummer=drummer,
                   notes=rel)
        p.complexity = score_complexity(rel, bars)
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
        if row["time_signature"] != "4-4":
            continue
        path = gmd_root / row["midi_filename"]
        if not path.exists():
            continue
        try:
            notes = refine_snare(read_notes(path))
        except (OSError, ValueError, EOFError):
            continue
        bpm = int(row["bpm"])
        drummer = row["drummer"]
        if row["beat_type"] == "fill":
            phrases += slice_phrases(notes, 1, bpm, style, drummer, KIND_FILL, 5)
            phrases += slice_phrases(notes, 2, bpm, style, drummer, KIND_FILL, 10)
        else:
            phrases += slice_phrases(notes, 2, bpm, style, drummer, KIND_BEAT, 8)
            phrases += slice_phrases(notes, 4, bpm, style, drummer, KIND_BEAT, 16)
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
               notes, True)
    p.complexity = norm(score_complexity(notes, p.bars), "c")
    p.intensity = norm(score_intensity(notes), "i")
    p.swing = measure_swing(notes)
    p.section = base.section
    p.fill_styles = base.fill_styles
    p.char_mask = base.char_mask
    return p


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

        ranked = sorted(mine, key=lambda p: p.complexity)
        donors = ranked[-6:]
        step = max(1, len(ranked) // 48)
        bases = ranked[::step]

        for base in bases:
            if not empty:
                break
            for notes in voicings(base, donors):
                cx = axis_cell(norm(score_complexity(notes, base.bars), "c"))
                rows = sorted(cy for (bx, cy) in empty if bx == cx)
                for cy in rows:
                    p = derive(base, notes, (cy + 0.5) / 10.0)
                    if cell_of(p) in empty:
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
                "<BBBBHBHBBH",
                p.kind,
                p.bars,
                int(round(p.complexity * 255)),
                int(round(p.intensity * 255)),
                p.bpm,
                p.section,
                p.char_mask,
                p.fill_styles,
                int(round(p.swing * 255)),
                len(p.notes)))
            for n in p.notes:
                grid, dev = quantise(n.beat)
                f.write(struct.pack("<BBHb", n.lane, n.velocity, grid, dev))


def write_manifest(phrases: list[Phrase], out: Path, corpus: Path) -> None:
    per_char = defaultdict(int)
    for p in phrases:
        for bit, (name, *_rest) in enumerate(CHARACTERS):
            if p.char_mask & (1 << bit):
                per_char[name] += 1
    manifest = {
        "corpus": corpus.name,
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
        ],
    }
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
    phrases = densify(phrases)
    write_corpus(phrases, args.out)
    if args.manifest:
        write_manifest(phrases, args.manifest, args.out)
    report(phrases)
    print(f"bytes: {args.out.stat().st_size}")


if __name__ == "__main__":
    main()
