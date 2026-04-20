"""Stub Python AI backend for AI Drum VST.

This module is a placeholder for a future Magenta / diffusion / custom
model. For now it returns deterministic MIDI patterns so the C++ plugin
can be wired end-to-end. Swap :func:`generate` for a real model call
when ready.

Intended integration paths:

1. **Embed**: load this module via pybind11 inside the JUCE plugin
   (``import ai_drum_backend; ai_drum_backend.generate(...)``).
2. **Subprocess / IPC**: run the plugin's "Generate" button as a
   subprocess call for quick prototyping without packaging Python.

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
    density: float = 0.5
    tempo_bpm: float = 120.0
    numerator: int = 4
    denominator: int = 4
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


def _groove(r: GenerationRequest, rng: random.Random) -> MidiPattern:
    pattern = MidiPattern(length_beats=float(r.numerator))
    steps = r.numerator * 2
    for step in range(steps):
        beat = 0.5 * step
        if step % 4 == 0:
            pattern.notes.append(MidiNote(KICK, 0.95, beat, 0.25))
        if step % 4 == 2:
            pattern.notes.append(MidiNote(SNARE, 0.9, beat, 0.25))
        vel = 0.55 + 0.25 * rng.random() * r.variation
        pattern.notes.append(MidiNote(CLOSED_HAT, vel, beat, 0.125))
        if rng.random() < r.density * 0.2 and step % 4 not in (0, 2):
            pattern.notes.append(MidiNote(SNARE, 0.35, beat, 0.125))
    if rng.random() < r.variation:
        pattern.notes.append(MidiNote(OPEN_HAT, 0.7, r.numerator - 0.5, 0.25))
    return pattern


def _fill(r: GenerationRequest, rng: random.Random) -> MidiPattern:
    pattern = MidiPattern(length_beats=float(r.numerator))
    toms = (HIGH_TOM, MID_TOM, LOW_TOM)
    steps = r.numerator * 4
    for step in range(steps):
        beat = 0.25 * step
        tom = toms[(step // 2) % 3]
        vel = 0.6 + 0.3 * rng.random()
        pattern.notes.append(MidiNote(tom, vel, beat, 0.125))
        if rng.random() < r.density * 0.5:
            pattern.notes.append(MidiNote(SNARE, 0.5 + 0.3 * rng.random(), beat, 0.125))
    pattern.notes.append(MidiNote(CRASH, 1.0, 0.0, 1.0))
    return pattern


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
    return pattern.to_dict()


if __name__ == "__main__":
    import json

    print(json.dumps(generate({"mode": "groove", "variation": 0.7, "density": 0.4}),
                     indent=2))
