#!/usr/bin/env python3
"""rc.11 — install the user's 17 (Bay Grunge) Yamaha Maple oneshot WAVs into
Resources/DefaultKit/.

Naming convention follows SampleKit::stripVelocitySuffix +
SampleKit::kindFromStem:

   BayGrungeMaple__<stem>_<layer>.wav   (layer 1 = softest, N = hardest)

Pulls from /home/ubuntu/ai-drum-vst/tools/bay_grunge_kit/ which the agent
populated from the user's rc.11 attachment block.
"""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

REPO         = Path(__file__).resolve().parents[1]
KIT_DIR      = REPO / "Resources" / "DefaultKit"
SRC_DIR      = REPO / "tools" / "bay_grunge_kit"
TARGET_SR    = 48_000
TARGET_PEAK  = 10 ** (-0.8 / 20.0)
PREFIX       = "BayGrungeMaple__"


# Source-WAV → output stem map. Light → heaviest order matches the rc.8
# layer convention so velocity layering picks the heavier sample as the
# user hits harder.
SAMPLES = {
    # Kick: 3 velocity layers
    "kick_1":      "Kick+Light_1.wav",
    "kick_2":      "Kick+Medium_1.wav",
    "kick_3":      "Kick+Heavy_1.wav",
    # Snare: 5 velocity layers (Light → Heaviest)
    "snare_1":     "Snare+Light_1.wav",
    "snare_2":     "Snare+Light+Medium_1.wav",
    "snare_3":     "Snare+Medium_1.wav",
    "snare_4":     "Snare+Heavy_1.wav",
    "snare_5":     "Snare+Heaviest_1.wav",
    # Floor Tom = Kind::LowTom — 2 layers
    "tom_low_1":   "Floor+Tom+Light_1.wav",
    "tom_low_2":   "Floor+Tom+Heavy_1.wav",
    # Small Tom — only 1 take in this batch ("Small Tom Heavy 2"). We
    # alias it across both Small Tom slots so the velocity layers still
    # have a sample even if the user hits softly.
    "tom_high_1":  "Small+Tom+Heavy+2.wav",
    "tom_high_2":  "Small+Tom+Heavy+2.wav",
    "tom_mid_1":   "Small+Tom+Heavy+2.wav",
    "tom_mid_2":   "Small+Tom+Heavy+2.wav",
    # Left Crash = Kind::Crash — 3 layers
    "crash_1":     "Left+Crash+Light_1.wav",
    "crash_2":     "Left+Crash+Medium_1.wav",
    "crash_3":     "Left+Crash+Heavy_1.wav",
    # Right Crash = Kind::China — 3 layers
    "china_1":     "Right+Crash+Light_1.wav",
    "china_2":     "Right+Crash+Medium_1.wav",
    "china_3":     "Right+Crash+Heavy_1.wav",
    # No ride / hat WAVs in this kit batch — fall back to the synth slots
    # (the engine fills them via DrumSynth if a Kind has no loaded layer).
}


def to_target_sr_mono(wav: np.ndarray, sr: int) -> np.ndarray:
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
    if len(wav) == 0:
        return wav
    thresh = 10 ** (-50 / 20.0)
    abs_wav = np.abs(wav)
    above = np.flatnonzero(abs_wav > thresh)
    start = max(0, int(above[0]) - int(0.005 * TARGET_SR)) if len(above) else 0
    wav = wav[start:]
    max_n = int(max_seconds * TARGET_SR)
    if len(wav) > max_n:
        wav = wav[:max_n]
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


MAX_LEN = {
    "kick":     0.6,
    "snare":    0.9,
    "tom_low":  1.6,
    "tom_high": 1.4,
    "tom_mid":  1.4,
    "crash":    3.5,
    "china":    3.5,
}


def stem_root(name: str) -> str:
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
    out = KIT_DIR / f"{PREFIX}{out_stem}.wav"
    sf.write(str(out), wav, TARGET_SR, subtype="PCM_16")
    print(f"  -> {out.name}  ({len(wav) / TARGET_SR:.2f}s, sr={TARGET_SR})")


def main() -> None:
    KIT_DIR.mkdir(parents=True, exist_ok=True)

    # Wipe any stale BayGrungeMaple__*.wav from a previous run.
    for stale in sorted(KIT_DIR.glob(f"{PREFIX}*.wav")):
        stale.unlink()

    print("[1/1] installing (Bay Grunge) Yamaha Maple oneshots:")
    for stem, src_name in SAMPLES.items():
        src = SRC_DIR / src_name
        install_one(stem, src)

    manifest = sorted(p.name for p in KIT_DIR.glob(f"{PREFIX}*.wav"))
    print(f"\nFinal kit ({len(manifest)} files):")
    for f in manifest:
        print(f"  {f}")

    # Update the per-kit manifest with the new kit alongside the existing
    # NuRockYamaha entry (purely informational — not consumed at runtime).
    manifest_path = KIT_DIR / "MANIFEST.json"
    payload = {}
    if manifest_path.exists():
        try:
            payload = json.loads(manifest_path.read_text())
        except Exception:
            payload = {}
    if "kits" not in payload:
        # Coerce old single-kit format into the new multi-kit layout.
        legacy_files = [n for n in payload.get("files", [])
                        if n.startswith("NuRockYamaha__")]
        payload = {"kits": {"NuRockYamaha": legacy_files} if legacy_files else {}}
    payload["kits"]["BayGrungeMaple"] = manifest
    manifest_path.write_text(json.dumps(payload, indent=2))


if __name__ == "__main__":
    main()
