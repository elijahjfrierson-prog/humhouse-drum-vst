#!/usr/bin/env python3
"""Slice the user-supplied "Heavy" recordings into per-velocity one-shots and
write them into Resources/DefaultKit/ with the HeavyStudio__ prefix so they
ship as the second bundled kit (alongside NuRockYamaha).

Each input WAV is a multi-hit recording of a single drum at varying velocities.
We onset-detect, window 1.5 s around each onset, sort by RMS (soft -> hard),
and export at most N layers per kind.
"""
from __future__ import annotations

import shutil
from pathlib import Path
from typing import List, Tuple

import numpy as np
import soundfile as sf
import librosa
from scipy import signal

REPO = Path(__file__).resolve().parent.parent
ATTACH = Path("/home/ubuntu/attachments")
KIT_DIR = REPO / "Resources" / "DefaultKit"
KIT_NAME = "HeavyStudio"

# Map (kind suffix -> attachment glob fragment, layer count, window seconds).
SOURCES: List[Tuple[str, str, int, float]] = [
    ("kick",         "Kick+Heavy_1.wav",         3, 1.20),
    ("snare",        "Snare+Heavy_1.wav",        5, 1.10),
    # Floor / low / mid / high tom — only "Floor Tom" + "Small Tom" recordings
    # exist; pitch-shift is unnecessary because DrumKit profile picks a
    # different MIDI note per slot, but we keep distinct one-shots so each
    # slot has its own attack transient.
    ("tom_low",      "Floor+Tom+Heavy_1.wav",    3, 1.50),
    ("tom_mid",      "Small+Tom+Heavy.wav",      3, 1.20),
    ("tom_high",     "Small+Tom+Heavy+2.wav",    3, 1.10),
    # Left crash routes to Kind::Crash, right crash routes to Kind::China
    # (mirrors the NuRockYamaha L/R split via SampleKit::kindFromStem).
    ("left_crash",   "Left+Crash+Heavy_1.wav",   3, 2.50),
    ("right_crash",  "Right+Crash+Heavy_1.wav",  3, 2.50),
    ("ride",         "Ride+Heavy_1.wav",         3, 2.00),
]

SR = 48000
PRE_ROLL_S = 0.005      # 5 ms head-room before onset
ONSET_DELTA = 0.04
MIN_GAP_S = 0.20


def load_mono(path: Path) -> np.ndarray:
    y, sr = sf.read(str(path), dtype="float32", always_2d=False)
    if y.ndim > 1:
        y = y.mean(axis=1)
    if sr != SR:
        y = librosa.resample(y, orig_sr=sr, target_sr=SR)
    y = y - np.mean(y)
    peak = float(np.max(np.abs(y)) or 1.0)
    return (y / peak * 0.95).astype(np.float32)


def detect_onsets(y: np.ndarray, delta: float, min_gap_s: float) -> np.ndarray:
    hop = 256
    env = librosa.onset.onset_strength(y=y, sr=SR, hop_length=hop)
    onsets = librosa.onset.onset_detect(
        onset_envelope=env, sr=SR, hop_length=hop,
        delta=delta, wait=int(min_gap_s * SR / hop),
        backtrack=True,
    )
    return librosa.frames_to_samples(onsets, hop_length=hop)


def find_attachment(name: str) -> Path:
    for child in ATTACH.iterdir():
        if not child.is_dir():
            continue
        candidate = child / name
        if candidate.exists():
            return candidate
    raise FileNotFoundError(name)


def slice_kind(kind: str, src_name: str, layers: int, window_s: float) -> List[Path]:
    src = find_attachment(src_name)
    print(f"[{KIT_NAME}] {kind:10s}  src={src}")
    y = load_mono(src)

    onsets = detect_onsets(y, ONSET_DELTA, MIN_GAP_S)
    if len(onsets) == 0:
        raise RuntimeError(f"no onsets detected in {src}")

    pre = int(PRE_ROLL_S * SR)
    win = int(window_s * SR)

    candidates: List[Tuple[float, np.ndarray]] = []
    for i, on in enumerate(onsets):
        start = max(0, on - pre)
        # Stop at the next onset so we never bleed two hits into one slice.
        next_on = onsets[i + 1] if i + 1 < len(onsets) else len(y)
        stop = min(start + win, next_on, len(y))
        seg = y[start:stop].copy()
        if len(seg) < int(0.05 * SR):
            continue
        # Apply a 25 ms exponential fade-out so loops/segments don't click
        # at the end of the next-onset boundary.
        fade = min(int(0.025 * SR), len(seg) // 4)
        if fade > 0:
            ramp = np.linspace(1.0, 0.0, fade, dtype=np.float32) ** 2
            seg[-fade:] *= ramp
        rms = float(np.sqrt(np.mean(seg.astype(np.float64) ** 2)))
        candidates.append((rms, seg))

    # Sort soft -> hard, keep evenly-spaced N layers.
    candidates.sort(key=lambda x: x[0])
    if len(candidates) <= layers:
        chosen = candidates
    else:
        idx = np.linspace(0, len(candidates) - 1, layers, dtype=int)
        chosen = [candidates[i] for i in idx]

    KIT_DIR.mkdir(parents=True, exist_ok=True)
    out_paths: List[Path] = []
    for n, (_rms, seg) in enumerate(chosen, start=1):
        out = KIT_DIR / f"{KIT_NAME}__{kind}_{n}.wav"
        sf.write(str(out), seg, SR, subtype="PCM_24")
        out_paths.append(out)
        print(f"          layer {n}/{len(chosen)}  rms={_rms:.4f}  -> {out.name}")
    return out_paths


def alias_from_nurock(suffixes: List[str]) -> List[Path]:
    """For kinds we don't have user-supplied recordings of (closed/pedal/open
    hat, china, ride bell, side-stick), copy the existing NuRockYamaha samples
    under the HeavyStudio prefix so the kit isn't silent on those hits."""
    out: List[Path] = []
    for s in suffixes:
        # Find any NuRockYamaha source with this suffix.
        pattern = f"NuRockYamaha__{s}_*.wav"
        sources = sorted(KIT_DIR.glob(pattern))
        if not sources:
            print(f"[{KIT_NAME}] WARNING: no NuRockYamaha source for {s}")
            continue
        for n, src in enumerate(sources, start=1):
            dst = KIT_DIR / f"{KIT_NAME}__{s}_{n}.wav"
            shutil.copyfile(src, dst)
            out.append(dst)
            print(f"[{KIT_NAME}] alias {s:10s} layer {n}  -> {dst.name}")
    return out


def main() -> None:
    written: List[Path] = []
    for kind, name, layers, win in SOURCES:
        written.extend(slice_kind(kind, name, layers, win))
    written.extend(alias_from_nurock([
        "hat_closed", "hat_pedal", "hat_open", "ride_bell", "china", "snare_ghost",
    ]))
    print(f"\n[{KIT_NAME}] total {len(written)} files written under "
          f"{KIT_DIR.relative_to(REPO)}")


if __name__ == "__main__":
    main()
