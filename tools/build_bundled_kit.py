#!/usr/bin/env python3
"""v1.6.1-rc.2 — build the bundled kit with 100% correct routing.

Strategy:
1. Regenerate every voice (kick, hat, tom, ride, crash, china, ride_bell)
   from the procedural synth in ``generate_default_kit.py``. Synth voices
   are guaranteed to contain audio that matches their slot label — a kick
   filter outputs a kick, a ride filter outputs a ride, etc.
2. Overlay the user's 16 reference snare WAVs (every filename contains
   "snare", so classification is trivially correct) across the snare
   slots for all 5 kits. Each kit gets a main + 3 velocity-layer snares.
3. Audit the resulting Resources/DefaultKit/ and fail loudly if any
   file's measured spectral content disagrees with its slot label.

The output is what the plugin ships. No ambiguous filename matching,
no slot cross-contamination.
"""
from __future__ import annotations

import glob
import os
import re
import subprocess
import sys
from math import gcd
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

REPO = Path(__file__).resolve().parents[1]
KIT_DIR = REPO / "Resources" / "DefaultKit"
ATTACH = Path.home() / "attachments"
TARGET_SR = 48_000
TARGET_PEAK = 10 ** (-0.8 / 20.0)


# ---------- step 1: regen all synth voices via generate_default_kit ------
def regen_synth():
    print("[1/3] regenerating synth voices via generate_default_kit.py")
    r = subprocess.run([sys.executable, str(REPO / "tools" / "generate_default_kit.py")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        raise SystemExit("generate_default_kit.py failed")
    print("   ok — all 5 kits regenerated")


# ---------- step 2: overlay user snares ---------------------------------
# The user's 16 reference snare files. Filename check: every one of these
# must contain the word "snare" (case-insensitive). This is the ONLY
# criterion we trust — the user has explicitly confirmed they are snares.
SNARE_FRAGMENTS = [
    "adt_snare_deep_1",
    "Air+Snare",
    "Dawn+Snare",
    "Deftones+Snare",
    "dirty+snare",
    "Heart+Shaped+Snare",
    "Nice+Snare",
    "Pop+Punk+Snare",
    "Shoegaze+Snare",
    "Slowdive+Snare",
    "snare+-+trek_2",
    "Snare+Indie",
    "TS_NEON_snare_one_shot_pearly_pizzica",
    "Uncanny+long+arms+snare",
    "underscores+snare",
    "747+Snare",
]

# 5 kits × 4 snare slots (main + 3 velocity layers) = 20 target slots.
# We have 16 reference snares; distribute them intentionally.
SNARE_MAP = {
    "PopRock":   ["Pop+Punk+Snare",  "Air+Snare",                "Dawn+Snare",            "747+Snare"],
    "NuRock":    ["Deftones+Snare",  "adt_snare_deep_1",         "Heart+Shaped+Snare",    "dirty+snare"],
    "AltRock":   ["Nice+Snare",      "Snare+Indie",              "Uncanny+long+arms+snare","Heart+Shaped+Snare"],
    "IndieLofi": ["Shoegaze+Snare",  "Slowdive+Snare",           "underscores+snare",     "Snare+Indie"],
    "Thrash":    ["snare+-+trek_2",  "TS_NEON_snare_one_shot_pearly_pizzica", "dirty+snare","Pop+Punk+Snare"],
}


def find_attachment(fragment: str) -> Path:
    """Locate an attachment WAV whose basename contains the fragment.

    Strict: the basename MUST also contain the word "snare" (case-
    insensitive) — we won't install a non-snare into a snare slot even
    if the fragment happens to appear in another file's name.
    """
    frag = fragment.lower().replace("+", " ")
    hits = []
    for f in glob.glob(str(ATTACH / "**" / "*.wav"), recursive=True):
        basename = os.path.basename(f)
        normalised = basename.lower().replace("+", " ")
        if frag not in normalised:
            continue
        if "snare" not in normalised:
            continue  # reject cross-category matches
        hits.append(f)
    if not hits:
        raise FileNotFoundError(f"no snare WAV matching {fragment!r}")
    hits.sort(key=lambda p: len(os.path.basename(p)))
    return Path(hits[0])


def prepare_snare(path: Path) -> np.ndarray:
    data, sr = sf.read(str(path), dtype="float32", always_2d=False)
    if data.ndim == 1:
        data = np.stack([data, data], axis=1)
    if sr != TARGET_SR:
        g = gcd(TARGET_SR, int(sr))
        data = resample_poly(data, TARGET_SR // g, int(sr) // g, axis=0).astype(np.float32)
    # Strip leading silence (< -60 dBFS)
    mag = np.max(np.abs(data), axis=1)
    thr = 10 ** (-60.0 / 20.0)
    first = int(np.argmax(mag > thr)) if np.any(mag > thr) else 0
    data = data[first:]
    # Cap at 1.2 s — snares rarely ring longer than this in a drum-pattern context
    cap = int(1.2 * TARGET_SR)
    if len(data) > cap:
        data = data[:cap]
        # gentle fade-out for the last 20 ms
        fade_n = int(0.02 * TARGET_SR)
        fade = np.linspace(1.0, 0.0, fade_n)
        data[-fade_n:, 0] *= fade
        data[-fade_n:, 1] *= fade
    # Normalise to -0.8 dBFS
    pk = float(np.max(np.abs(data))) + 1e-12
    data = data * (TARGET_PEAK / pk)
    return data.astype(np.float32)


VEL_SCALE = [0.62, 0.78, 0.90, 1.00]  # main, vl1, vl2, vl3 (main = softest)


def install_snares():
    print("[2/3] installing 16 user reference snare WAVs")
    slot_names = ["snare", "snare_1", "snare_2", "snare_3"]
    for kit, fragments in SNARE_MAP.items():
        assert len(fragments) == 4, kit
        for slot, frag, vel in zip(slot_names, fragments, VEL_SCALE):
            src = find_attachment(frag)
            y = prepare_snare(src) * vel
            # Re-clip to -0.8 dBFS in case velocity scaling pushed us over
            pk = float(np.max(np.abs(y))) + 1e-12
            if pk > TARGET_PEAK:
                y = y * (TARGET_PEAK / pk)
            out = KIT_DIR / f"{kit}__{slot}.wav"
            sf.write(str(out), y, TARGET_SR, subtype="PCM_16")
            print(f"   {kit:9s}  {slot:7s}  <- {src.name}")


# ---------- step 3: audit final kit -------------------------------------
def _centroid(y: np.ndarray, sr: int, win_s: float = 0.08) -> float:
    n = min(len(y), int(win_s * sr))
    if n < 16:
        return 0.0
    w = y[:n] * np.hanning(n)
    mag = np.abs(np.fft.rfft(w))
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    tot = mag.sum() + 1e-12
    return float((mag * freqs).sum() / tot)


def _band_ratio(y, sr, lo, hi, win_s=0.08):
    n = min(len(y), int(win_s * sr))
    if n < 16:
        return 0.0
    w = y[:n] * np.hanning(n)
    mag = np.abs(np.fft.rfft(w))
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    m = (freqs >= lo) & (freqs < hi)
    return float(mag[m].sum() / (mag.sum() + 1e-12))


# Spectral envelopes each slot must plausibly satisfy. If a file violates
# its slot envelope we FAIL the build rather than ship a mis-routed kit.
# Only flag GROSS mismatches — e.g. a "kick" that is all high-frequency
# sizzle with no low end, or a "snare" that's a pure sub-bass. User-
# provided snares span a huge tonal range (airy 9 kHz snappy cracks →
# body-heavy 400 Hz thuds); we trust the filename as the user explicitly
# instructed.
SLOT_EXPECT = {
    "kick":       lambda c, lo, hi: lo > 0.25,           # must have low-end body
    "snare":      lambda c, lo, hi: lo < 0.75,           # must not be all sub-bass
    "hat_closed": lambda c, lo, hi: hi > 0.30,           # must have high air
    "hat_open":   lambda c, lo, hi: hi > 0.30,
    "hat_pedal":  lambda c, lo, hi: hi > 0.15,
    "ride":       lambda c, lo, hi: hi > 0.10 or c > 1000,  # ride has stick tick OR bell
    "ride_bell":  lambda c, lo, hi: hi > 0.10 or c > 1000,
    "crash":      lambda c, lo, hi: hi > 0.25,
    "china":      lambda c, lo, hi: hi > 0.25,
    "tom_low":    lambda c, lo, hi: lo > 0.15,
    "tom_mid":    lambda c, lo, hi: lo > 0.10,
    "tom_high":   lambda c, lo, hi: c < 4000,
}


def audit_kit():
    print("[3/3] auditing Resources/DefaultKit/ slot↔content alignment")
    problems = []
    for f in sorted(KIT_DIR.glob("*.wav")):
        base = f.stem
        if "__" not in base:
            continue
        _, rest = base.split("__", 1)
        m = re.match(r"^(.+?)(?:_([0-9]))?$", rest)
        slot = m.group(1) if m else rest
        if slot not in SLOT_EXPECT:
            problems.append(f"unknown slot {slot!r} in {f.name}")
            continue
        y, sr = sf.read(str(f), always_2d=False)
        if y.ndim > 1:
            y = y.mean(axis=1)
        c = _centroid(y, sr)
        lo = _band_ratio(y, sr, 20, 250)
        hi = _band_ratio(y, sr, 3500, 16000)
        ok = SLOT_EXPECT[slot](c, lo, hi)
        if not ok:
            problems.append(f"{f.name}: slot={slot} centroid={c:.0f}Hz low%={lo*100:.1f} hi%={hi*100:.1f}")
    if problems:
        print("AUDIT FAILED — fix these before shipping:")
        for p in problems:
            print(f"   {p}")
        raise SystemExit(1)
    print(f"   ok — all {len(list(KIT_DIR.glob('*.wav')))} bundled WAVs match their slot")


if __name__ == "__main__":
    regen_synth()
    install_snares()
    audit_kit()
