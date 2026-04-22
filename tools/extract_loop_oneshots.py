#!/usr/bin/env python3
"""Extract per-drum one-shots from a looped drum audio file.

Strategy: run onset detection on *band-split* versions of the loop so
kicks (<150 Hz), snares (200-1500 Hz + noise) and cymbals (>4 kHz) get
detected independently — a single onset can carry multiple labels (e.g.
kick + hat simultaneously) and that's fine. We then pick the cleanest
exemplar of each drum by looking for onsets that fire in *one* band
only (isolated hits).

Existing pattern MIDI (StarterGrooves.generated.h) is NOT touched.
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np
import soundfile as sf
import librosa
import librosa.util
from scipy import signal

REPO = Path(__file__).resolve().parent.parent
LOOPS_WAV = REPO / "incoming_loops" / "wav"
KIT_DIR = REPO / "Resources" / "DefaultKit"

SR = 44100
MAX_ONESHOT_S = 0.70
CYMBAL_MAX_S = 1.80
MIN_ONESHOT_S = 0.08

KIND_FILES = {
    "kick": "{kit}__kick.wav",
    "snare": "{kit}__snare.wav",
    "hat_closed": "{kit}__hat_closed.wav",
    "hat_open": "{kit}__hat_open.wav",
    "hat_pedal": "{kit}__hat_pedal.wav",
    "tom_low": "{kit}__tom_low.wav",
    "tom_mid": "{kit}__tom_mid.wav",
    "tom_high": "{kit}__tom_high.wav",
    "crash": "{kit}__crash.wav",
    "ride": "{kit}__ride.wav",
    "ride_bell": "{kit}__ride_bell.wav",
    "china": "{kit}__china.wav",
}


# --------------------------------------------------------------------------
# Loading
# --------------------------------------------------------------------------
def load_loop(path: Path) -> np.ndarray:
    y, sr = sf.read(str(path), dtype="float32", always_2d=False)
    if y.ndim > 1:
        y = y.mean(axis=1)
    if sr != SR:
        y = librosa.resample(y, orig_sr=sr, target_sr=SR)
    y = y - np.mean(y)
    peak = float(np.max(np.abs(y)) or 1.0)
    y = (y / peak) * 0.85
    return y.astype(np.float32)


def bandpass(y: np.ndarray, lo: Optional[float], hi: Optional[float]) -> np.ndarray:
    nyq = SR / 2
    if lo is None:
        sos = signal.butter(4, hi / nyq, btype="low", output="sos")
    elif hi is None:
        sos = signal.butter(4, lo / nyq, btype="high", output="sos")
    else:
        sos = signal.butter(4, [lo / nyq, hi / nyq], btype="band", output="sos")
    return signal.sosfiltfilt(sos, y).astype(np.float32)


def detect_onsets_in_band(y_band: np.ndarray, delta: float = 0.08) -> np.ndarray:
    hop = 256
    env = librosa.onset.onset_strength(y=y_band, sr=SR, hop_length=hop)
    onsets = librosa.onset.onset_detect(
        onset_envelope=env, sr=SR, hop_length=hop,
        backtrack=True, units="samples",
        pre_max=3, post_max=3, pre_avg=8, post_avg=8,
        delta=delta, wait=8,
    )
    return onsets


# --------------------------------------------------------------------------
# Band-wise onset buckets
# --------------------------------------------------------------------------
@dataclass
class BandOnsets:
    kick: np.ndarray
    snare: np.ndarray
    hat: np.ndarray
    cymbal: np.ndarray


def find_band_onsets(y: np.ndarray) -> BandOnsets:
    y_sub   = bandpass(y, None, 150)       # kick fundamental
    y_snare = bandpass(y, 180, 500)        # snare fundamental
    y_hat   = bandpass(y, 5000, 12000)     # hat band
    y_cym   = bandpass(y, 8000, 16000)     # cymbal shimmer

    return BandOnsets(
        kick=detect_onsets_in_band(y_sub, 0.10),
        snare=detect_onsets_in_band(y_snare, 0.08),
        hat=detect_onsets_in_band(y_hat, 0.09),
        cymbal=detect_onsets_in_band(y_cym, 0.07),
    )


def isolated_hits(primary: np.ndarray,
                  others: List[np.ndarray],
                  tolerance_s: float = 0.030) -> np.ndarray:
    """Return hits in `primary` that do NOT have any neighbour in
    `others` within ±tolerance_s."""
    if primary.size == 0:
        return primary
    tol = int(tolerance_s * SR)
    keep = []
    for o in primary:
        clean = True
        for neigh in others:
            if neigh.size == 0:
                continue
            # nearest distance
            idx = np.searchsorted(neigh, o)
            candidates = []
            if idx > 0:
                candidates.append(neigh[idx - 1])
            if idx < neigh.size:
                candidates.append(neigh[idx])
            if any(abs(int(c) - int(o)) <= tol for c in candidates):
                clean = False
                break
        if clean:
            keep.append(o)
    return np.array(keep, dtype=np.int64)


# --------------------------------------------------------------------------
# Hit scoring
# --------------------------------------------------------------------------
def hit_rms(y: np.ndarray, start: int, length_s: float = 0.100) -> float:
    end = min(len(y), start + int(length_s * SR))
    seg = y[start:end]
    if seg.size == 0:
        return 0.0
    return float(np.sqrt(np.mean(seg ** 2)))


def hit_band_rms(y_band: np.ndarray, start: int, length_s: float = 0.080) -> float:
    return hit_rms(y_band, start, length_s)


# --------------------------------------------------------------------------
# Extraction
# --------------------------------------------------------------------------
def extract_window(y: np.ndarray, start: int, max_len_s: float,
                   fade_out_s: float = 0.020) -> np.ndarray:
    end = min(len(y), start + int(max_len_s * SR))
    seg = y[start:end].copy()
    if seg.size < int(MIN_ONESHOT_S * SR):
        seg = np.concatenate(
            [seg, np.zeros(int(MIN_ONESHOT_S * SR) - seg.size, dtype=np.float32)]
        )
    fi = int(0.001 * SR)
    fo = min(int(fade_out_s * SR), max(1, seg.size // 3))
    seg[:fi] *= np.linspace(0, 1, fi, dtype=np.float32)
    seg[-fo:] *= np.linspace(1, 0, fo, dtype=np.float32)
    peak = float(np.max(np.abs(seg)) or 1.0)
    seg = (seg / peak) * 0.82
    return seg


def pick_best_isolated(candidates: np.ndarray,
                        y_band: np.ndarray,
                        y_full: np.ndarray,
                        top_n: int = 5) -> List[int]:
    """Return top-N candidate sample indices by in-band RMS."""
    if candidates.size == 0:
        return []
    scored = sorted(
        candidates,
        key=lambda s: hit_band_rms(y_band, int(s)),
        reverse=True,
    )
    return [int(s) for s in scored[:top_n]]


def save_wav(path: Path, seg: np.ndarray) -> None:
    sf.write(str(path), seg.astype(np.float32), SR, subtype="PCM_16")


# --------------------------------------------------------------------------
# Per-kit processor
# --------------------------------------------------------------------------
def process_loop(kit_name: str, loop_path: Path) -> None:
    print(f"\n=== {kit_name}: {loop_path.name} ===")
    y = load_loop(loop_path)

    y_sub   = bandpass(y, None, 180)
    y_mid   = bandpass(y, 180, 2500)
    y_hi    = bandpass(y, 4000, 12000)
    y_vhi   = bandpass(y, 8000, 16000)

    bands = find_band_onsets(y)
    print(f"  onsets: kick={len(bands.kick)}, snare={len(bands.snare)}, "
          f"hat={len(bands.hat)}, cymbal={len(bands.cymbal)}")

    # Isolated kicks: in sub band, not in snare band (some overlap with hat is
    # OK — hats nearly always ride over kicks in these loops)
    iso_kick = isolated_hits(bands.kick, [bands.snare])
    # Isolated snares: in snare band, not in kick band nearby
    iso_snare = isolated_hits(bands.snare, [bands.kick])
    # Isolated hats: in hat band, not concurrent with kick or snare
    iso_hat = isolated_hits(bands.hat, [bands.kick, bands.snare])
    # Cymbals: in cymbal band with sustained tail (long RMS in vhi after start)
    iso_cym = bands.cymbal

    print(f"  isolated: kick={len(iso_kick)}, snare={len(iso_snare)}, "
          f"hat={len(iso_hat)}, cym={len(iso_cym)}")

    # If no isolated kicks, fall back to all kick-band onsets scored by sub rms
    kick_candidates = iso_kick if iso_kick.size else bands.kick
    snare_candidates = iso_snare if iso_snare.size else bands.snare
    hat_candidates = iso_hat if iso_hat.size else bands.hat

    picks: Dict[str, int] = {}
    if kick_candidates.size:
        best = pick_best_isolated(kick_candidates, y_sub, y, top_n=3)
        picks["kick"] = best[0]
    if snare_candidates.size:
        best = pick_best_isolated(snare_candidates, y_mid, y, top_n=3)
        picks["snare"] = best[0]
    if hat_candidates.size:
        best = pick_best_isolated(hat_candidates, y_hi, y, top_n=5)
        # choose shortest/cleanest (closed) and longest (open) if possible
        picks["hat_closed"] = best[0]
        picks["hat_open"] = best[1] if len(best) > 1 else best[0]

    # Cymbals: we look for onsets whose tail (250-1000ms) has sustained vhi RMS.
    if iso_cym.size:
        scored = []
        for s in iso_cym:
            tail_start = int(s) + int(0.25 * SR)
            tail_end   = min(len(y_vhi), int(s) + int(1.0 * SR))
            if tail_end <= tail_start:
                continue
            tail_rms = float(np.sqrt(np.mean(y_vhi[tail_start:tail_end] ** 2)))
            scored.append((tail_rms, int(s)))
        scored.sort(reverse=True)
        if scored:
            picks["crash"] = scored[0][1]
            # ride = shorter tail but still cymbal
            mid_tail = [p for p in scored if p[0] < scored[0][0] * 0.6] or scored
            picks["ride"] = mid_tail[-1][1]

    # ------------------------------------------------------------------
    # Fallbacks: if still missing anything, derive from existing neighbours.
    # ------------------------------------------------------------------
    if "kick" not in picks and "snare" in picks:
        # look BEFORE each snare hit, -250..-50 ms, for sub-band energy peak
        snare_s = picks["snare"]
        search_lo = max(0, snare_s - int(0.25 * SR))
        search_hi = max(0, snare_s - int(0.05 * SR))
        if search_hi > search_lo:
            window = y_sub[search_lo:search_hi]
            peak_idx = int(np.argmax(np.abs(window))) + search_lo
            picks["kick"] = peak_idx

    # ------------------------------------------------------------------
    # Extract and save
    # ------------------------------------------------------------------
    def write(kind: str, sample_start: int, max_len_s: float, fade_out: float = 0.020) -> None:
        fname = KIND_FILES[kind].format(kit=kit_name)
        seg = extract_window(y, int(sample_start), max_len_s, fade_out)
        save_wav(KIT_DIR / fname, seg)
        print(f"    wrote {fname}  ({seg.size/SR:.2f}s)  ← {kind} @ {sample_start/SR:.2f}s")

    def writeseg(kind: str, seg: np.ndarray) -> None:
        fname = KIND_FILES[kind].format(kit=kit_name)
        save_wav(KIT_DIR / fname, seg.astype(np.float32))
        print(f"    wrote {fname}  ({seg.size/SR:.2f}s)  ← {kind} (derived)")

    # Core drums
    for kind in ("kick", "snare"):
        if kind in picks:
            write(kind, picks[kind], MAX_ONESHOT_S, 0.020)
        else:
            print(f"    ! no {kind}; leaving existing {kind} file")

    for kind in ("hat_closed", "hat_open"):
        if kind in picks:
            max_len = 0.25 if kind == "hat_closed" else 0.55
            write(kind, picks[kind], max_len, 0.015)
        else:
            print(f"    ! no {kind}; leaving existing {kind} file")

    for kind in ("crash", "ride"):
        if kind in picks:
            write(kind, picks[kind], CYMBAL_MAX_S, 0.150)
        else:
            print(f"    ! no {kind}; leaving existing {kind} file")

    # -----------  Derived secondary samples  ----------------------------
    # hat_pedal: closed hat but darker & shorter
    if "hat_closed" in picks:
        seg = extract_window(y, picks["hat_closed"], 0.15, 0.010)
        # low-pass emphasis
        seg = bandpass(seg, None, 5000)
        peak = float(np.max(np.abs(seg)) or 1.0)
        seg = (seg / peak) * 0.70
        writeseg("hat_pedal", seg)

    # ride_bell: ride hit with hi-pass + attack emphasis
    if "ride" in picks:
        seg = extract_window(y, picks["ride"], 1.2, 0.100)
        seg_hp = bandpass(seg, 3500, None)
        mix = seg * 0.4 + seg_hp * 0.9
        peak = float(np.max(np.abs(mix)) or 1.0)
        writeseg("ride_bell", (mix / peak) * 0.82)

    # china: crash with mid cut & extra bite
    if "crash" in picks:
        seg = extract_window(y, picks["crash"], 1.5, 0.150)
        seg_hp = bandpass(seg, 6000, None)
        mix = seg * 0.5 + seg_hp * 0.9
        peak = float(np.max(np.abs(mix)) or 1.0)
        writeseg("china", (mix / peak) * 0.78)

    # Toms: derive from kick by pitch-shift (since these loops don't have toms)
    if "kick" in picks:
        base = extract_window(y, picks["kick"], 0.45, 0.060)
        # Pitch-shift up 6, 12, 19 semitones for low/mid/high tom
        for kind, semis in (("tom_low", 6), ("tom_mid", 12), ("tom_high", 19)):
            shifted = librosa.effects.pitch_shift(base, sr=SR, n_steps=semis)
            peak = float(np.max(np.abs(shifted)) or 1.0)
            writeseg(kind, (shifted / peak) * 0.78)

    # Snare velocity layers: ghost / mid / accent
    if "snare" in picks:
        base = extract_window(y, picks["snare"], MAX_ONESHOT_S, 0.020)
        for i, gain in enumerate((0.22, 0.62, 1.00), start=1):
            v = base * gain
            peak = float(np.max(np.abs(v)) or 1.0)
            if peak > 0.98:
                v = (v / peak) * 0.98
            save_wav(KIT_DIR / f"{kit_name}__snare_{i}.wav", v.astype(np.float32))
            print(f"    wrote {kit_name}__snare_{i}.wav")


def main() -> int:
    loops = {
        "NuRock":    LOOPS_WAV / "NuRock.wav",
        "PopRock":   LOOPS_WAV / "PopRock.wav",
        "AltRock":   LOOPS_WAV / "AltRock.wav",
        "IndieLofi": LOOPS_WAV / "IndieLofi.wav",
        "Thrash":    LOOPS_WAV / "Thrash.wav",
    }
    for kit, path in loops.items():
        if not path.exists():
            print(f"MISSING: {path}")
            return 1
        process_loop(kit, path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
