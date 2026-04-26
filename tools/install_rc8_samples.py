#!/usr/bin/env python3
"""rc.8 — install the user's 21 new oneshot WAVs into Resources/DefaultKit/.

Naming convention follows SampleKit::stripVelocitySuffix +
SampleKit::kindFromStem:

   <KitName>__<stem>_<layer>.wav    (layer 1 = softest, N = hardest)

Kit prefix flips from "Thrash__" to "NuRockYamaha__" so the in-memory
binary blob no longer collides with old loaded patterns.

The hi-hat closed / open / pedal WAVs from the previous Thrash kit are
preserved (the user explicitly asked to keep them) but each is trimmed
to a single-strike oneshot if it was a loop.
"""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

REPO     = Path(__file__).resolve().parents[1]
KIT_DIR  = REPO / "Resources" / "DefaultKit"
ATTACH   = Path.home() / "attachments"
TARGET_SR = 48_000
TARGET_PEAK = 10 ** (-0.8 / 20.0)
NEW_PREFIX  = "NuRockYamaha__"
OLD_PREFIX  = "Thrash__"


# ---------- attachment lookup ------------------------------------------
# UUIDs from the user's rc.8 attachment block. Light → heaviest order.
NEW_SAMPLES = {
    # Kick: 3 velocity layers
    "kick_1": "5b8e8452-4759-4bd6-9270-92922d29710e/Kick+Light_1.wav",
    "kick_2": "ec204825-b824-498b-9447-147111cd6997/Kick+Medium_1.wav",
    "kick_3": "70f9914b-6fa9-42c2-b693-dc1011c6e489/Kick+Heavy_1.wav",
    # Snare: 5 velocity layers (Light → Heaviest)
    "snare_1": "8346a4e2-9030-49b5-89e7-9290a980e241/Snare+Light_1.wav",
    "snare_2": "3d2ab78b-1e9e-4ab3-b2b7-3fca33c845e6/Snare+Light+Medium_1.wav",
    "snare_3": "2a11269f-9d78-4341-90dd-b5e0b34e49ee/Snare+Medium_1.wav",
    "snare_4": "e5a7c3e9-fe69-46d9-a4f3-f82ad427d56b/Snare+Heavy_1.wav",
    "snare_5": "c03c39e9-007b-4aad-aacf-2b91f25ac2fa/Snare+Heaviest_1.wav",
    # Floor Tom = Kind::LowTom — 2 layers
    "tom_low_1": "c6c714a4-e3d0-40d0-8dd1-afa589d1805e/Floor+Tom+Light_1.wav",
    "tom_low_2": "d935c2e2-99df-4b38-823e-3bf2ea6aa3ca/Floor+Tom+Heavy_1.wav",
    # Small Tom = Kind::HighTom + Kind::MidTom (2 takes, both heavy)
    "tom_high_1": "3dfb2322-5275-4ef8-9720-e0249e38ef7e/Small+Tom+Heavy.wav",
    "tom_high_2": "f77fad6d-6adc-4361-84cd-c447ce5e1cce/Small+Tom+Heavy_2.wav",
    "tom_mid_1":  "3dfb2322-5275-4ef8-9720-e0249e38ef7e/Small+Tom+Heavy.wav",
    "tom_mid_2":  "f77fad6d-6adc-4361-84cd-c447ce5e1cce/Small+Tom+Heavy_2.wav",
    # Left Crash = Kind::Crash — 3 layers
    "crash_1": "38c2d834-90b7-45d3-975d-e3d256095830/Left+Crash+Light_1.wav",
    "crash_2": "7d1c5c09-f500-494c-abd5-358ba419df06/Left+Crash+Medium_1.wav",
    "crash_3": "e2cbc6da-6415-44c5-92a0-af1722ccd2e6/Left+Crash+Heavy_1.wav",
    # Right Crash = Kind::China — 3 layers
    "china_1": "c5988f1b-325a-4ac9-89d9-a68befd9e418/Right+Crash+Light_1.wav",
    "china_2": "9b8bcad3-129d-458b-b270-c9d8f495a370/Right+Crash+Medium_1.wav",
    "china_3": "1a27bdf5-1619-4114-a5fe-61da05babd8e/Right+Crash+Heavy_1.wav",
    # Ride = Kind::Ride — 3 layers
    "ride_1": "81172eb3-831f-418a-8e94-45dc9d7e1362/Ride+Light_1.wav",
    "ride_2": "1bf886f5-9a3b-44fc-816f-48abd48ec105/Ride+Medium_1.wav",
    "ride_3": "47932947-b851-455d-bab0-26156bd7172c/Ride+Heavy_1.wav",
    # Ride bell — re-use the heaviest ride sample (keeps the slot loaded so
    # GM note 53 doesn't fall through to the synth fallback).
    "ride_bell_1": "47932947-b851-455d-bab0-26156bd7172c/Ride+Heavy_1.wav",
}


# ---------- helpers ----------------------------------------------------
def to_target_sr_mono(wav: np.ndarray, sr: int) -> np.ndarray:
    """Convert any-channel-count audio at any rate down to 48 kHz mono."""
    if wav.ndim == 2:
        wav = wav.mean(axis=1)
    if sr != TARGET_SR:
        from math import gcd as _gcd
        g = _gcd(int(sr), TARGET_SR)
        up, down = TARGET_SR // g, sr // g
        wav = resample_poly(wav, up, down).astype(np.float32)
    else:
        wav = wav.astype(np.float32)
    return wav


def trim_oneshot(wav: np.ndarray, max_seconds: float) -> np.ndarray:
    """Keep one strike: find the first transient, drop everything before it,
    cap the tail to ``max_seconds``, and apply a 30 ms fade-out so the tail
    never clicks into silence."""
    if len(wav) == 0:
        return wav
    # Pre-roll trim: drop leading silence below -50 dB.
    thresh = 10 ** (-50 / 20.0)
    abs_wav = np.abs(wav)
    above = np.flatnonzero(abs_wav > thresh)
    start = max(0, int(above[0]) - int(0.005 * TARGET_SR)) if len(above) else 0
    wav = wav[start:]
    # Cap length.
    max_n = int(max_seconds * TARGET_SR)
    if len(wav) > max_n:
        wav = wav[:max_n]
    # Fade out.
    fade_n = min(len(wav), int(0.030 * TARGET_SR))
    if fade_n > 0:
        ramp = np.linspace(1.0, 0.0, fade_n, dtype=np.float32)
        wav[-fade_n:] *= ramp
    return wav


def normalize_peak(wav: np.ndarray) -> np.ndarray:
    peak = float(np.max(np.abs(wav))) if len(wav) else 0.0
    if peak <= 0:
        return wav
    return (wav * (TARGET_PEAK / peak)).astype(np.float32)


# Per-stem max length cap (in seconds). One-shots only — no loops.
MAX_LEN = {
    "kick":     0.6,
    "snare":    0.9,
    "tom_low":  1.6,
    "tom_high": 1.4,
    "tom_mid":  1.4,
    "crash":    3.5,
    "china":    3.5,
    "ride":     1.6,
    "ride_bell": 1.6,
    "hat_closed": 0.35,
    "hat_open":   1.6,
    "hat_pedal":  0.35,
}


def stem_root(name: str) -> str:
    # "tom_low_2" -> "tom_low";  "kick_1" -> "kick"
    parts = name.rsplit("_", 1)
    if len(parts) == 2 and parts[1].isdigit():
        return parts[0]
    return name


def install_one(out_stem: str, src_path: Path) -> None:
    if not src_path.exists():
        raise FileNotFoundError(src_path)
    wav, sr = sf.read(str(src_path), dtype="float32", always_2d=False)
    wav = to_target_sr_mono(wav, sr)
    wav = trim_oneshot(wav, MAX_LEN[stem_root(out_stem)])
    wav = normalize_peak(wav)
    out = KIT_DIR / f"{NEW_PREFIX}{out_stem}.wav"
    sf.write(str(out), wav, TARGET_SR, subtype="PCM_16")
    print(f"  -> {out.name}  ({len(wav) / TARGET_SR:.2f}s, sr={TARGET_SR})")


def main() -> None:
    KIT_DIR.mkdir(parents=True, exist_ok=True)

    # Step 1 — wipe the old Thrash__*.wav files so the binary blob doesn't
    # carry stale loops alongside the new oneshots.
    for old in sorted(KIT_DIR.glob(f"{OLD_PREFIX}*.wav")):
        old.unlink()
        print(f"  removed {old.name}")

    # Step 2 — also wipe any stray NuRockYamaha__*.wav from a previous run.
    for stale in sorted(KIT_DIR.glob(f"{NEW_PREFIX}*.wav")):
        stale.unlink()

    # Step 3 — install the new oneshots from the user's attachments.
    print("[1/2] installing user oneshots:")
    for stem, rel in NEW_SAMPLES.items():
        src = ATTACH / rel
        install_one(stem, src)

    # Step 4 — re-trim & re-prefix the previous Thrash hi-hat WAVs so the
    # NuRockYamaha kit ships with hats. The user said: keep the hi-hat,
    # cut to a oneshot if it was a loop.
    print("[2/2] importing + trimming previous hi-hat samples:")
    src_kits = sorted(REPO.glob("Resources/DefaultKit_legacy_*"))
    legacy = REPO / "Resources" / "DefaultKit_legacy"
    if legacy.exists():
        for stem_legacy, stem_new in [
            ("Thrash__hat_closed.wav", "hat_closed_1"),
            ("Thrash__hat_open.wav",   "hat_open_1"),
            ("Thrash__hat_pedal.wav",  "hat_pedal_1"),
        ]:
            src = legacy / stem_legacy
            if not src.exists():
                print(f"  skip {stem_legacy} (not in legacy backup)")
                continue
            wav, sr = sf.read(str(src), dtype="float32", always_2d=False)
            wav = to_target_sr_mono(wav, sr)
            wav = trim_oneshot(wav, MAX_LEN[stem_root(stem_new)])
            wav = normalize_peak(wav)
            out = KIT_DIR / f"{NEW_PREFIX}{stem_new}.wav"
            sf.write(str(out), wav, TARGET_SR, subtype="PCM_16")
            print(f"  -> {out.name}  ({len(wav) / TARGET_SR:.2f}s)")
    else:
        print("  (no legacy hat backup found — synth hat fallback will be used)")

    # Step 5 — write a manifest for inspection.
    manifest = sorted(p.name for p in KIT_DIR.glob(f"{NEW_PREFIX}*.wav"))
    (KIT_DIR / "MANIFEST.json").write_text(
        json.dumps({"kit": "NuRockYamaha", "files": manifest}, indent=2)
    )
    print(f"\nFinal kit ({len(manifest)} files):")
    for f in manifest:
        print(f"  {f}")


if __name__ == "__main__":
    main()
