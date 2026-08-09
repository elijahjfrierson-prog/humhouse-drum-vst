#!/usr/bin/env python3
"""Slice HumHouse's raw multi-hit kit recordings into a velocity-layered,
round-robin sample set for HumHouse Drums X.

Each source WAV is one microphone take containing many hits of a single kit
piece at one dynamic ("Snare Medium_1.wav" = ~40 medium snare hits). We detect
onsets, cut each hit at the next onset (or when it decays), normalise nothing
(the recorded dynamic IS the velocity layer), then write mono 16-bit 44.1 kHz
files named `<kit>__<piece>_<n>.wav` so KitEngine can map them to lanes.

    python3 tools/kitx/slice_kit.py --src <dir-of-wavs> --out Resources/KitX
"""

from __future__ import annotations

import argparse
import math
import os
import re
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

TARGET_RATE = 44100

# Source file name -> (piece, dynamic rank). Dynamic rank only orders the
# takes; the runtime sorts layers by measured loudness anyway.
PIECE_PATTERNS = [
    (r"^kick", "kick"),
    (r"^snare", "snare"),
    (r"^small_tom", "tom_high"),
    (r"^mid_tom|^rack_tom", "tom_mid"),
    (r"^floor_tom", "tom_low"),
    (r"^left_crash", "left_crash"),
    (r"^right_crash", "right_crash"),
    (r"^crash", "right_crash"),
    (r"^ride_bell", "ride_bell"),
    (r"^ride", "ride"),
    (r"^hat_closed|^closed_hat|^hihat_closed", "hat_closed"),
    (r"^hat_open|^open_hat|^hihat_open", "hat_open"),
    (r"^hat_pedal|^pedal_hat", "hat_pedal"),
    (r"^china", "china"),
]

# Longest slice we keep, per piece. Cymbals ring; drums do not.
MAX_SECONDS = {
    "left_crash": 3.2,
    "right_crash": 3.2,
    "china": 3.0,
    "ride": 2.2,
    "ride_bell": 2.2,
    "hat_open": 1.4,
    "tom_high": 1.6,
    "tom_mid": 1.8,
    "tom_low": 2.2,
}
DEFAULT_MAX_SECONDS = 1.2

MAX_SLICES_PER_SOURCE = 12     # keeps the bundled kit a sane size
SILENCE_DB = -62.0

# A hit has to reach this fraction of the take's own peak to count. The raw
# recordings are one piece at one dynamic with long silences between strikes,
# so anything quieter than this is bleed, stick noise or room tail.
ONSET_PEAK_FRACTION = 0.22


def classify(stem: str, overrides: dict[str, str]) -> str | None:
    key = re.sub(r"[\s\-]+", "_", stem.strip().lower())
    if key in overrides:
        return overrides[key]
    key = re.sub(r"_(light|light_medium|medium|heavy|heaviest)(_\d+)?$", "", key)
    key = re.sub(r"_\d+$", "", key)
    for pattern, piece in PIECE_PATTERNS:
        if re.match(pattern, key):
            return piece
    return None


def to_mono_44k(x: np.ndarray, sr: int) -> np.ndarray:
    mono = x.mean(axis=1) if x.ndim > 1 else x
    if sr == TARGET_RATE:
        return mono.astype(np.float32)
    n = int(round(len(mono) * TARGET_RATE / sr))
    src = np.linspace(0.0, 1.0, len(mono), endpoint=False)
    dst = np.linspace(0.0, 1.0, n, endpoint=False)
    return np.interp(dst, src, mono).astype(np.float32)


def find_onsets(mono: np.ndarray, sr: int, hop: int = 256) -> list[int]:
    frames = len(mono) // hop
    env = np.array([np.sqrt(np.mean(mono[i * hop:(i + 1) * hop] ** 2) + 1e-12)
                    for i in range(frames)])
    peak = env.max()
    if peak <= 0:
        return []

    gate = peak * ONSET_PEAK_FRACTION
    back = max(1, int(0.03 * sr / hop))     # 30 ms before the candidate
    onsets: list[int] = []
    last = -10 ** 9
    for i in range(back, len(env)):
        if env[i] < gate or env[i] <= env[i - 1]:
            continue
        # A strike is a >9 dB jump over the level just before it, not a bump in
        # a decaying cymbal tail.
        if env[i] < env[i - back] * 2.8:
            continue
        pos = i * hop
        if pos - last < int(0.12 * sr):
            continue
        # Back off to where the transient actually starts.
        floor = peak * 0.02
        start = pos
        while start > 0 and abs(mono[start]) > floor and pos - start < hop * 8:
            start -= 1
        onsets.append(start)
        last = pos
    return onsets


def trim_tail(slice_: np.ndarray, sr: int) -> np.ndarray:
    floor = 10 ** (SILENCE_DB / 20) * max(1e-9, np.abs(slice_).max())
    win = 256
    end = len(slice_)
    for i in range(len(slice_) - win, 0, -win):
        if np.abs(slice_[i:i + win]).max() > floor:
            end = min(len(slice_), i + win * 2)
            break
    return slice_[:end]


def fade(slice_: np.ndarray, sr: int) -> np.ndarray:
    out = slice_.copy()
    attack = min(48, len(out))
    out[:attack] *= np.linspace(0.0, 1.0, attack)
    release = min(int(0.02 * sr), len(out))
    out[-release:] *= np.linspace(1.0, 0.0, release)
    return out


def slice_file(path: Path, piece: str) -> list[np.ndarray]:
    audio, sr = sf.read(str(path), always_2d=False)
    mono = to_mono_44k(audio, sr)
    sr = TARGET_RATE

    onsets = find_onsets(mono, sr)
    if not onsets:
        return []

    limit = int(MAX_SECONDS.get(piece, DEFAULT_MAX_SECONDS) * sr)
    slices: list[np.ndarray] = []
    for i, start in enumerate(onsets):
        stop = onsets[i + 1] if i + 1 < len(onsets) else len(mono)
        stop = min(stop, start + limit, len(mono))
        chunk = mono[start:stop]
        if len(chunk) < int(0.05 * sr):
            continue
        chunk = fade(trim_tail(chunk, sr), sr)
        slices.append(chunk)

    # Keep the loudest, most representative hits, evenly spread through the take
    # so we get genuine round robins rather than the first N in a row.
    if len(slices) > MAX_SLICES_PER_SOURCE:
        idx = np.linspace(0, len(slices) - 1, MAX_SLICES_PER_SOURCE).round().astype(int)
        slices = [slices[i] for i in dict.fromkeys(idx.tolist())]
    return slices


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, help="directory containing the raw kit WAVs")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--kit", default="SoCalRock", help="kit prefix")
    ap.add_argument("--map", action="append", default=[],
                    help="explicit `filestem=piece` mapping, repeatable")
    args = ap.parse_args()

    overrides: dict[str, str] = {}
    for entry in args.map:
        stem, _, piece = entry.partition("=")
        overrides[re.sub(r"[\s\-]+", "_", stem.strip().lower())] = piece.strip()

    src = Path(args.src)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    counters: dict[str, int] = {}
    total = 0
    for wav in sorted(src.rglob("*.wav")):
        piece = classify(wav.stem, overrides)
        if piece is None:
            print(f"  skip (unclassified): {wav.name}")
            continue
        chunks = slice_file(wav, piece)
        for chunk in chunks:
            counters[piece] = counters.get(piece, 0) + 1
            name = f"{args.kit}__{piece}_{counters[piece]:02d}.wav"
            peak = float(np.abs(chunk).max())
            if peak > 0.99:
                chunk = chunk * (0.99 / peak)
            sf.write(str(out / name), chunk, TARGET_RATE, subtype="PCM_16")
            total += 1
        print(f"  {wav.name} -> {piece}: {len(chunks)} slices")

    print(f"\n{total} samples written to {out}")
    for piece, n in sorted(counters.items()):
        print(f"  {piece:12s} {n}")
    size = sum(f.stat().st_size for f in out.glob('*.wav'))
    print(f"  total {size / 1e6:.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
