"""Stub Python AI backend for AI Drum VST.

This module is a placeholder for a future Magenta / diffusion / custom
model. For now it returns deterministic MIDI patterns so the C++ plugin
can be wired end-to-end. Swap :func:`generate` for a real model call
when ready.

Intended integration paths:

1. **Embed**: load this module via pybind11 inside the JUCE plugin
   (``import ai_drum_backend; ai_drum_backend.generate(...)``).
2. **Subprocess / IPC**: run the plugin's "+" button as a subprocess
   call for quick prototyping without packaging Python.

Either way the Python side returns a list of note dicts that the C++
side converts into :cpp:class:`aidrum::MidiPattern`.
"""

from __future__ import annotations

import random
from dataclasses import dataclass, field
from typing import Literal

Mode = Literal["groove", "fill"]


@dataclass
class GenerationRequest:
    mode: Mode = "groove"
    variation: float = 0.5
    complexity: float = 0.5
    velocity: float = 1.0
    humanize: float = 0.25
    tempo_bpm: float = 120.0
    numerator: int = 4
    denominator: int = 4
    length_beats: float = 4.0  # 0.25 (1/16) … 8.0 (2 bars)
    seed: int = 0  # 0 -> random


@dataclass
class MidiNote:
    note: int
    velocity: float  # 0..1
    start_beat: float
    length_beat: float


@dataclass
class MidiPattern:
    length_beats: float = 4.0
    notes: list[MidiNote] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            "length_beats": self.length_beats,
            "notes": [n.__dict__ for n in self.notes],
        }


# General MIDI drum map
KICK, SNARE = 36, 38
CLOSED_HAT, OPEN_HAT = 42, 46
LOW_TOM, MID_TOM, HIGH_TOM = 41, 45, 48
CRASH = 49

SIXTEENTH = 0.25


def _groove(r: GenerationRequest, rng: random.Random) -> MidiPattern:
    length = max(SIXTEENTH, r.length_beats)
    pattern = MidiPattern(length_beats=length)

    num_sixteenths = int(round(length / SIXTEENTH))
    kick_prob = 0.35 + 0.25 * r.complexity
    ghost_prob = 0.05 + 0.35 * r.complexity
    open_hat_prob = 0.05 + 0.20 * r.variation

    for step in range(num_sixteenths):
        beat = SIXTEENTH * step
        sixteenth_in_bar = step % 16
        quarter_in_bar = sixteenth_in_bar // 4
        is_downbeat = sixteenth_in_bar % 4 == 0
        is_kick_beat = quarter_in_bar in (0, 2)
        is_snare_beat = quarter_in_bar in (1, 3)

        if is_downbeat and is_kick_beat:
            pattern.notes.append(MidiNote(KICK, 0.95, beat, 0.25))
        if is_downbeat and is_snare_beat:
            pattern.notes.append(MidiNote(SNARE, 0.9, beat, 0.25))
        if (not is_downbeat) and sixteenth_in_bar % 4 == 3 and rng.random() < kick_prob * 0.6:
            pattern.notes.append(MidiNote(KICK, 0.78, beat, 0.25))
        if not (is_downbeat and is_snare_beat) and sixteenth_in_bar % 4 != 0 and rng.random() < ghost_prob:
            pattern.notes.append(MidiNote(SNARE, 0.30 + 0.20 * rng.random(), beat, 0.125))

        if sixteenth_in_bar % 2 == 0:
            vel = 0.55 + 0.25 * rng.random() * r.variation
            pattern.notes.append(MidiNote(CLOSED_HAT, vel, beat, 0.125))
        elif rng.random() < r.complexity * 0.85:
            pattern.notes.append(MidiNote(CLOSED_HAT, 0.45 + 0.25 * rng.random(), beat, 0.0625))

        if sixteenth_in_bar == 14 and rng.random() < open_hat_prob:
            pattern.notes.append(MidiNote(OPEN_HAT, 0.65, beat, 0.25))

    return pattern


def _fill(r: GenerationRequest, rng: random.Random) -> MidiPattern:
    length = max(SIXTEENTH, r.length_beats)
    pattern = MidiPattern(length_beats=length)
    num_sixteenths = int(round(length / SIXTEENTH))
    toms = (HIGH_TOM, MID_TOM, LOW_TOM)

    for step in range(num_sixteenths):
        beat = SIXTEENTH * step
        tom_idx = min(len(toms) - 1, (step * len(toms)) // max(1, num_sixteenths))
        pattern.notes.append(MidiNote(toms[tom_idx], 0.60 + 0.25 * rng.random(), beat, 0.125))
        if rng.random() < r.complexity * 0.6:
            pattern.notes.append(MidiNote(SNARE, 0.50 + 0.30 * rng.random(), beat, 0.125))
        if step % 4 == 0:
            pattern.notes.append(MidiNote(KICK, 0.85, beat, 0.25))

    pattern.notes.append(MidiNote(CRASH, 1.0, 0.0, 1.0))
    pattern.notes.append(MidiNote(KICK, 0.95, 0.0, 0.25))
    return pattern


def _finalize(pattern: MidiPattern, r: GenerationRequest, rng: random.Random) -> None:
    timing_jitter = 0.04 * r.humanize
    vel_jitter = 0.20 * r.humanize
    eps = 1e-3
    max_beat = max(0.0, pattern.length_beats - eps)
    for n in pattern.notes:
        n.velocity = max(0.01, min(1.0, n.velocity * r.velocity + vel_jitter * (rng.random() * 2 - 1)))
        n.start_beat = max(0.0, min(max_beat, n.start_beat + timing_jitter * (rng.random() * 2 - 1)))


def generate(request: GenerationRequest | dict | None = None) -> dict:
    """Generate a MIDI pattern and return it as a plain dict.

    Accepts either a :class:`GenerationRequest` or a dict (so C++ callers
    can pass JSON-like kwargs without caring about Python dataclasses).
    """
    if request is None:
        req = GenerationRequest()
    elif isinstance(request, dict):
        req = GenerationRequest(**request)
    else:
        req = request

    seed = req.seed or random.randint(1, 2**63 - 1)
    rng = random.Random(seed)

    pattern = _fill(req, rng) if req.mode == "fill" else _groove(req, rng)
    _finalize(pattern, req, random.Random(seed ^ 0x9E3779B97F4A7C15))
    return pattern.to_dict()


if __name__ == "__main__":
    import json

    print(json.dumps(generate({"mode": "groove", "variation": 0.7, "complexity": 0.4,
                               "humanize": 0.3, "length_beats": 4.0}), indent=2))
