#!/usr/bin/env python3
"""Compile a human-performance groove corpus for HumHouse Drums X.

Source data: Magenta Groove MIDI Dataset (GMD), CC-BY 4.0 — real drummers
playing to a click on an electronic kit, so every note carries genuine
micro-timing and velocity. We keep only the rock/punk material (the plugin
ships one kit, one genre family) and slice it into bar-aligned phrases that
the runtime PerformanceEngine selects between.

Usage:
    python3 tools/corpusx/build_corpus.py --gmd /path/to/groove --out content/rock_corpus.hhc
"""

from __future__ import annotations

import argparse
import csv
import struct
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

import mido

MAGIC = b"HHCX"
FORMAT_VERSION = 1

BEATS_PER_BAR = 4.0

# Canonical HumHouse lane ids. The runtime kit maps these to samples, and the
# MIDI exporter maps them to the General MIDI notes listed alongside.
LANE_KICK = 0
LANE_SNARE = 1
LANE_SNARE_RIM = 2
LANE_SIDESTICK = 3
LANE_HAT_CLOSED = 4
LANE_HAT_PEDAL = 5
LANE_HAT_OPEN = 6
LANE_TOM_HI = 7
LANE_TOM_MID = 8
LANE_TOM_FLOOR = 9
LANE_CRASH_L = 10
LANE_CRASH_R = 11
LANE_RIDE = 12
LANE_RIDE_BELL = 13
NUM_LANES = 14

# Roland TD-11 note map used by the GMD recordings.
TD11_TO_LANE = {
    36: LANE_KICK,
    38: LANE_SNARE,
    40: LANE_SNARE_RIM,
    37: LANE_SIDESTICK,
    42: LANE_HAT_CLOSED,
    22: LANE_HAT_CLOSED,
    44: LANE_HAT_PEDAL,
    46: LANE_HAT_OPEN,
    26: LANE_HAT_OPEN,
    48: LANE_TOM_HI,
    50: LANE_TOM_HI,
    45: LANE_TOM_MID,
    47: LANE_TOM_MID,
    43: LANE_TOM_FLOOR,
    58: LANE_TOM_FLOOR,
    49: LANE_CRASH_L,
    55: LANE_CRASH_L,
    57: LANE_CRASH_R,
    52: LANE_CRASH_R,
    51: LANE_RIDE,
    59: LANE_RIDE,
    53: LANE_RIDE_BELL,
}

# Styles that belong in a rock session-drummer product.
ROCK_STYLES = ("rock", "punk", "pop")

KIND_BEAT = 0
KIND_FILL = 1


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
    notes: list[Note] = field(default_factory=list)
    complexity: float = 0.0
    intensity: float = 0.0


def read_notes(path: Path) -> list[Note]:
    """Flattens a GMD MIDI file into lane/beat/velocity note-ons."""
    mid = mido.MidiFile(path)
    tpq = mid.ticks_per_beat
    notes: list[Note] = []
    for track in mid.tracks:
        tick = 0
        for msg in track:
            tick += msg.time
            if msg.type != "note_on" or msg.velocity == 0:
                continue
            lane = TD11_TO_LANE.get(msg.note)
            if lane is None:
                continue
            notes.append(Note(lane, tick / tpq, msg.velocity))
    notes.sort(key=lambda n: n.beat)
    return notes


def score_complexity(notes: list[Note], bars: int) -> float:
    """0..1 — onset density, off-grid syncopation and limb variety combined."""
    if not notes:
        return 0.0
    span_beats = bars * BEATS_PER_BAR
    density = len(notes) / span_beats  # onsets per beat
    density_score = min(density / 6.0, 1.0)

    syncopated = 0
    for n in notes:
        pos16 = (n.beat * 4.0) % 4.0  # position within the beat, in 16ths
        # Anything that is not on the beat or the 8th counts as syncopation;
        # the "e" and "a" 16ths count double.
        frac = abs(pos16 - round(pos16))
        slot = round(pos16) % 4
        if frac > 0.25:
            syncopated += 1
        elif slot in (1, 3):
            syncopated += 1
    sync_score = min(syncopated / max(len(notes), 1) * 2.0, 1.0)

    variety = len({n.lane for n in notes}) / NUM_LANES
    variety_score = min(variety * 2.2, 1.0)

    return max(0.0, min(1.0, 0.5 * density_score + 0.3 * sync_score + 0.2 * variety_score))


def score_intensity(notes: list[Note]) -> float:
    """0..1 — how hard the phrase is being hit, weighted toward backbeat lanes."""
    if not notes:
        return 0.0
    weights = {LANE_KICK: 1.2, LANE_SNARE: 1.4, LANE_SNARE_RIM: 1.4,
               LANE_CRASH_L: 1.3, LANE_CRASH_R: 1.3, LANE_TOM_HI: 1.1,
               LANE_TOM_MID: 1.1, LANE_TOM_FLOOR: 1.1}
    total = sum(n.velocity * weights.get(n.lane, 1.0) for n in notes)
    weight = sum(weights.get(n.lane, 1.0) for n in notes)
    mean = total / weight / 127.0
    loud = sum(1 for n in notes if n.velocity >= 100) / len(notes)
    return max(0.0, min(1.0, 0.75 * mean + 0.25 * loud))


def slice_phrases(notes: list[Note], bars: int, bpm: int, style: str,
                  kind: int, min_notes: int) -> list[Phrase]:
    """Cuts a performance into non-overlapping bar-aligned windows."""
    if not notes:
        return []
    span = bars * BEATS_PER_BAR
    last = notes[-1].beat
    buckets: dict[int, list[Note]] = defaultdict(list)
    for n in notes:
        buckets[int(n.beat // span)].append(n)

    phrases: list[Phrase] = []
    for idx in range(int(last // span) + 1):
        window = buckets.get(idx, [])
        if len(window) < min_notes:
            continue
        # Drop a window whose opening beat is empty — it is a tail, not a phrase.
        if min(n.beat for n in window) - idx * span > 0.75:
            continue
        rel = [Note(n.lane, n.beat - idx * span, n.velocity) for n in window]
        # Micro-timing can drag a hit a hair past the window edge; keep it in range.
        rel = [n for n in rel if -0.05 <= n.beat < span]
        p = Phrase(kind=kind, bars=bars, bpm=bpm, style=style, notes=rel)
        p.complexity = score_complexity(rel, bars)
        p.intensity = score_intensity(rel)
        phrases.append(p)
    return phrases


def build(gmd_root: Path) -> list[Phrase]:
    info = list(csv.DictReader(open(gmd_root / "info.csv")))
    phrases: list[Phrase] = []
    for row in info:
        style = row["style"].split("/")[0]
        if style not in ROCK_STYLES or row["time_signature"] != "4-4":
            continue
        path = gmd_root / row["midi_filename"]
        if not path.exists():
            continue
        try:
            notes = read_notes(path)
        except (OSError, ValueError, EOFError):
            continue
        bpm = int(row["bpm"])
        if row["beat_type"] == "fill":
            phrases += slice_phrases(notes, 1, bpm, style, KIND_FILL, min_notes=5)
            phrases += slice_phrases(notes, 2, bpm, style, KIND_FILL, min_notes=10)
        else:
            phrases += slice_phrases(notes, 2, bpm, style, KIND_BEAT, min_notes=8)
    return phrases


def write_corpus(phrases: list[Phrase], out: Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<II", FORMAT_VERSION, len(phrases)))
        for p in phrases:
            f.write(struct.pack(
                "<BBBBHH",
                p.kind,
                p.bars,
                int(round(p.complexity * 255)),
                int(round(p.intensity * 255)),
                p.bpm,
                len(p.notes)))
            for n in p.notes:
                f.write(struct.pack("<BBf", n.lane, n.velocity, n.beat))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--gmd", required=True, type=Path,
                    help="path to the extracted groove/ folder of the GMD dataset")
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    phrases = build(args.gmd)
    beats = [p for p in phrases if p.kind == KIND_BEAT]
    fills = [p for p in phrases if p.kind == KIND_FILL]
    write_corpus(phrases, args.out)

    print(f"phrases: {len(phrases)} ({len(beats)} beats, {len(fills)} fills)")
    print(f"bytes:   {args.out.stat().st_size}")
    grid = defaultdict(int)
    for p in beats:
        grid[(int(p.complexity * 9.999), int(p.intensity * 9.999))] += 1
    empty = [(c, i) for c in range(10) for i in range(10) if (c, i) not in grid]
    print(f"10x10 XY cells populated: {100 - len(empty)}/100")


if __name__ == "__main__":
    main()
