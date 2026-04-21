#!/usr/bin/env python3
"""
v1.5.0 — install user-provided real drum samples into Resources/DefaultKit/.

The user delivered 47 real .wav files spanning kicks / snares / hats / rides /
crashes.  We map them across the five bundled kits (PopRock, NuRock, AltRock,
IndieLofi, Thrash) and overwrite the synth-generated kick/snare/hat/ride/crash
slots.  Toms / china / ride_bell / hat_pedal remain from the synth generator
since no user samples were supplied for those voices.

Reads:
    ~/attachments/**/*.wav                   (originals from the user)
Writes:
    Resources/DefaultKit/<Kit>__<slot>.wav   (48 kHz stereo 16-bit PCM, trimmed)
"""

from __future__ import annotations

import glob
import os
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly


HOME = Path.home()
ATTACH = HOME / "attachments"
REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "Resources" / "DefaultKit"
OUT.mkdir(parents=True, exist_ok=True)

TARGET_SR = 48_000
TARGET_PEAK_DB = -0.8  # normalise to -0.8 dBFS so hot samples don't clip

# --- Resolve the user-supplied WAVs by filename fragment ---------------------

def find_attachment(fragment: str) -> Path:
    """Locate the attachment WAV whose filename contains the fragment."""
    hits = []
    for f in glob.glob(str(ATTACH / "**" / "*.wav"), recursive=True):
        if fragment.lower().replace(" ", "+") in os.path.basename(f).lower() \
                or fragment.lower() in os.path.basename(f).lower().replace("+", " "):
            hits.append(f)
    if not hits:
        raise FileNotFoundError(f"No attachment matching {fragment!r}")
    # prefer the shortest basename (exact-ish match)
    hits.sort(key=lambda p: len(os.path.basename(p)))
    return Path(hits[0])


# --- Per-kit mapping (slot -> attachment fragment) ---------------------------
# 5 kits × 8 real-sample slots (kick, snare, snare_1..3, hat_closed, hat_open,
# ride, crash).  Per-kit toms/china/ride_bell/hat_pedal stay from synth gen.

KITS = {
    "PopRock": {  # bright, punchy, round kick + bright wooden snare
        "kick":       "KSHMR Acoustic Kick",
        "snare":      "Pop Punk Snare",
        "snare_1":    "Air Snare",
        "snare_2":    "747 Snare",
        "snare_3":    "Dawn Snare",
        "hat_closed": "HiHat from Addictive Drums",
        "hat_open":   "CLA OpenHat",
        "ride":       "CLA Ride",
        "crash":      "crash - pretty",
    },
    "NuRock": {  # Deftones-style, darker, cracky, body-forward
        "kick":       "Deftones Kick",
        "snare":      "Deftones Snare",
        "snare_1":    "adt_snare_deep_1 edited",
        "snare_2":    "Deftones Snare",
        "snare_3":    "dirty snare",
        "hat_closed": "Deftones Hat",
        "hat_open":   "Deftones Open Hat",
        "ride":       "Cool Ride",
        "crash":      "Travis Barker Crash",
    },
    "AltRock": {  # grungy warm wash, Heart-Shaped-Box territory
        "kick":       "Heart Shaped Kick",
        "snare":      "Nice Snare",
        "snare_1":    "Snare Indie",
        "snare_2":    "Nice Snare",
        "snare_3":    "Uncanny long arms snare",
        "hat_closed": "Heart Shaped Hat",
        "hat_open":   "Heart Shaped Open Hat",
        "ride":       "Air Ride",
        "crash":      "Crash - Winter",
    },
    "IndieLofi": {  # dull, soft-thuddy, background / shoegaze / slowcore
        "kick":       "Shoegaze Kick",
        "snare":      "Shoegaze Snare",
        "snare_1":    "Slowdive Snare",
        "snare_2":    "Shoegaze Snare",
        "snare_3":    "underscores snare",
        "hat_closed": "TS_NEON_hihat_one_shot_half_open_salsa",
        "hat_open":   "oh - barely",
        "ride":       "Dreamy Ride 2",
        "crash":      "crash - pretty",
    },
    "Thrash": {  # tight, choppy, aggressive
        "kick":       "kick - damn",
        "snare":      "dirty snare",
        "snare_1":    "snare - trek_2",
        "snare_2":    "dirty snare",
        "snare_3":    "TS_NEON_snare_one_shot_pearly_pizzica",
        "hat_closed": "Deftones Hat",
        "hat_open":   "HR OpenHat",
        "ride":       "Uncanny long arms ride",
        "crash":      "Travis Barker Crash",
    },
}


def load_and_prepare(path: Path, slot: str) -> np.ndarray:
    """Load, resample to 48 kHz, force to stereo, trim/pad, normalise."""
    data, sr = sf.read(str(path), dtype="float32", always_2d=False)
    if data.ndim == 1:
        data = np.stack([data, data], axis=1)
    # stereo [N,2]
    # Resample to target
    if sr != TARGET_SR:
        # rational up/down
        from math import gcd
        g = gcd(TARGET_SR, int(sr))
        up = TARGET_SR // g
        down = int(sr) // g
        data = resample_poly(data, up, down, axis=0).astype(np.float32)

    # Strip leading near-silence (-60 dBFS threshold)
    thresh = 10 ** (-60.0 / 20.0)
    mag = np.max(np.abs(data), axis=1)
    first = int(np.argmax(mag > thresh)) if np.any(mag > thresh) else 0
    first = max(0, first - 64)  # keep 64-sample pre-roll
    data = data[first:]

    # Max lengths per slot (keep ROOM AMT knob in charge of tails)
    max_len_s = {
        "kick": 0.9,
        "snare": 1.3,
        "snare_1": 1.1, "snare_2": 1.2, "snare_3": 1.3,
        "hat_closed": 0.55,
        "hat_open": 1.8,
        "ride": 2.5,
        "crash": 3.0,
    }.get(slot, 2.0)
    max_len = int(TARGET_SR * max_len_s)
    if data.shape[0] > max_len:
        data = data[:max_len]
        # apply a ~6 ms fade-out so we don't click
        fade = int(TARGET_SR * 0.006)
        env = np.linspace(1.0, 0.0, fade, dtype=np.float32)
        data[-fade:] *= env[:, None]

    # Normalise peak
    peak = float(np.max(np.abs(data))) or 1e-9
    target = 10 ** (TARGET_PEAK_DB / 20.0)
    data = data * (target / peak)

    return data.astype(np.float32)


def main() -> None:
    print(f"Output: {OUT}")
    written = 0
    for kit, slots in KITS.items():
        for slot, frag in slots.items():
            src = find_attachment(frag)
            y = load_and_prepare(src, slot)
            out = OUT / f"{kit}__{slot}.wav"
            sf.write(str(out), y, TARGET_SR, subtype="PCM_16")
            print(f"  [{kit:10s}] {slot:10s} <- {src.name} ({y.shape[0]/TARGET_SR:.2f}s)")
            written += 1
    print(f"Wrote {written} samples.")


if __name__ == "__main__":
    main()
