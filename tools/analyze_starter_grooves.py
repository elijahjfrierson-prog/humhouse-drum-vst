#!/usr/bin/env python3
"""Onset-detect + band-classify a bank of drum-groove WAVs and emit a C++
header declaring them as StarterGroove templates the plugin can show in its
"STARTER GROOVES" dropdown.

Usage:
    python3 tools/analyze_starter_grooves.py \
        --input tools/starter_grooves \
        --output Source/StarterGrooves.generated.h
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
import re

import librosa
import numpy as np

# General-MIDI drum numbers the plugin already uses in AIBackend.cpp.
K_KICK = 36
K_SNARE = 38
K_LOW_TOM = 41
K_CLOSED_HAT = 42
K_MID_TOM = 45
K_OPEN_HAT = 46
K_HIGH_TOM = 48
K_CRASH = 49
K_RIDE = 51

BAND_EDGES = {
    "sub":  (20.0, 80.0),     # kick fundamental
    "kick": (60.0, 180.0),    # kick body
    "low":  (180.0, 400.0),   # tom / snare body
    "mid":  (400.0, 2000.0),  # snare crack
    "hi":   (4000.0, 9000.0), # hat / ride stick
    "air":  (9000.0, 16000.0) # crash / open hat
}

CANDIDATE_BPMS = [70, 75, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 138, 144, 150, 160, 170, 180]


def band_energy(stft_mag, sr, edges):
    freqs = np.linspace(0.0, sr / 2.0, stft_mag.shape[0])
    lo, hi = edges
    mask = (freqs >= lo) & (freqs < hi)
    if not mask.any():
        return np.zeros(stft_mag.shape[1])
    return stft_mag[mask, :].mean(axis=0)


def classify_onset(stft_mag, sr, frame_idx, hop):
    """Pick the most likely GM drum for a single onset frame."""
    # Take a short window around the onset for spectral analysis.
    half = 3
    f0 = max(0, frame_idx - 1)
    f1 = min(stft_mag.shape[1], frame_idx + half + 1)
    spec = stft_mag[:, f0:f1].mean(axis=1)
    freqs = np.linspace(0.0, sr / 2.0, spec.shape[0])

    def band(lo, hi):
        m = (freqs >= lo) & (freqs < hi)
        return float(spec[m].mean()) if m.any() else 0.0

    sub_e = band(*BAND_EDGES["sub"])
    kick_e = band(*BAND_EDGES["kick"])
    low_e = band(*BAND_EDGES["low"])
    mid_e = band(*BAND_EDGES["mid"])
    hi_e = band(*BAND_EDGES["hi"])
    air_e = band(*BAND_EDGES["air"])

    low_total = sub_e + kick_e
    body_total = low_e
    high_total = hi_e + air_e

    # Heuristic classifier. Tuned by inspection; not perfect but classifies
    # enough of each groove correctly for the generator's purposes.
    # 1. If there's strong sub/kick energy and not much high sizzle → kick.
    # 2. Strong mid-crack (1-4 kHz) + moderate body → snare.
    # 3. Low-mid only, no crack → tom (pitch-pick by low-vs-mid ratio).
    # 4. Mostly high sizzle → hat/ride. Distinguish open vs closed by
    #    post-peak sustain.
    ratio_low_high = low_total / max(high_total, 1e-6)
    ratio_mid_body = mid_e / max(low_e, 1e-6)

    # Decay: how long the onset rings (seconds from the onset frame to when
    # the high-band energy drops below 40% of the peak).
    decay_frames = 0
    if high_total > 1e-6 and frame_idx < stft_mag.shape[1] - 4:
        peak = stft_mag[:, frame_idx:frame_idx + 1].mean()
        thresh = 0.4 * peak
        for k in range(frame_idx, min(stft_mag.shape[1], frame_idx + 30)):
            if stft_mag[:, k].mean() < thresh:
                break
            decay_frames += 1

    if ratio_low_high > 2.5 and low_total > 0.02:
        return K_KICK

    # Snare: mid-crack dominates; body present.
    if mid_e > 0.05 and ratio_mid_body > 0.6 and high_total > 0.01:
        return K_SNARE

    # Pure high: hat / ride / crash.
    if high_total > max(low_total, body_total) * 1.4:
        if decay_frames > 12:
            # Long sustain → open hat (air-heavy) or ride (mid-high heavy).
            return K_OPEN_HAT if air_e > hi_e * 1.3 else K_RIDE
        return K_CLOSED_HAT

    # Tom: low-mid body only, not enough high sizzle to be a hat.
    if body_total > 0.03 and high_total < body_total * 0.8:
        # Pitch-pick by sub-vs-low ratio.
        if sub_e > low_e:
            return K_LOW_TOM
        if low_e > mid_e * 1.2:
            return K_MID_TOM
        return K_HIGH_TOM

    # Fallback: treat as a closed hat accent rather than drop the onset.
    return K_CLOSED_HAT


def estimate_bpm(y, sr, onset_env):
    """Use librosa's beat-tracker; fall back to 120 if it fails."""
    try:
        tempo, _ = librosa.beat.beat_track(onset_envelope=onset_env, sr=sr)
        bpm = float(np.asarray(tempo).flatten()[0])
        if not math.isfinite(bpm) or bpm <= 0:
            return 120.0
        # Tempo tracker sometimes doubles / halves. Prefer 80..170 BPM.
        while bpm > 180.0:
            bpm *= 0.5
        while bpm < 70.0:
            bpm *= 2.0
        return float(round(bpm))
    except Exception:
        return 120.0


def quantise_to_grid(onset_times, bpm, grid="1/16"):
    div = {"1/8": 2, "1/16": 4, "1/32": 8}[grid]
    step = 60.0 / bpm / div
    return np.round(onset_times / step) * step


def analyze_wav(path: pathlib.Path):
    y, sr = librosa.load(str(path), sr=None, mono=True)
    if sr < 22050:
        # Rare, but upsample so the spectral classifier has enough bins.
        y = librosa.resample(y, orig_sr=sr, target_sr=44100)
        sr = 44100

    # Strip leading silence so bar-1 starts at t=0.
    nz = np.where(np.abs(y) > 0.005)[0]
    if nz.size:
        y = y[nz[0]:]

    hop = 256
    stft = np.abs(librosa.stft(y, n_fft=1024, hop_length=hop))
    onset_env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)
    onset_frames = librosa.onset.onset_detect(
        onset_envelope=onset_env,
        sr=sr,
        hop_length=hop,
        backtrack=True,
        delta=0.15,
        wait=2,
    )
    if onset_frames.size == 0:
        return None

    onset_times = librosa.frames_to_time(onset_frames, sr=sr, hop_length=hop)
    bpm = estimate_bpm(y, sr, onset_env)
    quantised = quantise_to_grid(onset_times, bpm, "1/16")

    beats = []
    for t_raw, t_q, f in zip(onset_times, quantised, onset_frames):
        note = classify_onset(stft, sr, int(f), hop)
        beat_pos = t_q * (bpm / 60.0)
        # Velocity from local RMS around the onset (0.5 .. 1.0).
        win = y[int(t_raw * sr):int(t_raw * sr) + int(0.04 * sr)]
        if win.size == 0:
            vel = 0.85
        else:
            rms = float(np.sqrt(np.mean(win * win)))
            vel = float(np.clip(0.55 + rms * 6.0, 0.55, 1.0))
        beats.append({"note": int(note), "beat": float(beat_pos), "vel": round(vel, 3), "len": 0.12})

    # Trim the starter down to the first musically-sensible window: 4 or 8
    # beats depending on the audio length. The analyzer often captures more
    # than 1 bar of source, but we only want a loopable seed.
    raw_len_beats = onset_times[-1] * bpm / 60.0
    if raw_len_beats <= 4.5:
        total_beats = 4
    elif raw_len_beats <= 9.0:
        total_beats = 8
    else:
        total_beats = 8  # still cap at 2 bars so regions stay snappy
    # Filter beats to just what fits in total_beats.
    beats = [b for b in beats if b["beat"] < total_beats]

    # Dedup hits landing on the same 1/32 slot for the same drum (likely
    # flams from the detector, not actual flams in the source).
    seen = set()
    uniq = []
    for b in beats:
        key = (b["note"], round(b["beat"] * 8))
        if key in seen:
            continue
        seen.add(key)
        uniq.append(b)

    return {
        "name": path.stem,
        "bpm": float(bpm),
        "lengthBeats": float(total_beats),
        "notes": uniq,
    }


def humanize_name(stem: str) -> str:
    pretty = re.sub(r"[_\-]+", " ", stem)
    pretty = re.sub(r"(\D)(\d)", r"\1 \2", pretty)
    return pretty.upper()


def emit_cpp_header(grooves, out_path: pathlib.Path):
    lines = [
        "// v1.6.0 — GENERATED by tools/analyze_starter_grooves.py.",
        "// DO NOT EDIT: re-run the analyzer to update.",
        "// Source of truth = tools/starter_grooves/*.wav.",
        "",
        "#pragma once",
        "",
        "#include \"AIBackend.h\"",
        "",
        "#include <array>",
        "#include <string_view>",
        "",
        "namespace aidrum",
        "{",
        "    struct StarterGroove",
        "    {",
        "        std::string_view name;",
        "        double          bpm;",
        "        double          lengthBeats;",
        "        MidiPattern     pattern;",
        "    };",
        "",
        "    inline const std::vector<StarterGroove>& starterGrooveLibrary()",
        "    {",
        "        static const std::vector<StarterGroove> kLibrary = []",
        "        {",
        "            std::vector<StarterGroove> g;",
    ]

    for gr in grooves:
        lines.append("            {")
        lines.append(f"                StarterGroove sg;")
        lines.append(f"                sg.name = {json.dumps(humanize_name(gr['name']))};")
        lines.append(f"                sg.bpm = {gr['bpm']};")
        lines.append(f"                sg.lengthBeats = {gr['lengthBeats']};")
        lines.append(f"                sg.pattern.lengthInBeats = {gr['lengthBeats']};")
        for note in gr["notes"]:
            if note["beat"] > gr["lengthBeats"]:
                continue
            lines.append(
                "                sg.pattern.notes.push_back ({ "
                f"{note['note']}, {note['vel']}f, {note['beat']}, {note['len']} "
                "});"
            )
        lines.append("                g.push_back (std::move (sg));")
        lines.append("            }")

    lines += [
        "            return g;",
        "        }();",
        "        return kLibrary;",
        "    }",
        "}",
    ]

    out_path.write_text("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", type=pathlib.Path, required=True)
    ap.add_argument("--output", type=pathlib.Path, required=True)
    args = ap.parse_args()

    wavs = sorted(args.input.glob("*.wav"))
    grooves = []
    for w in wavs:
        try:
            gr = analyze_wav(w)
        except Exception as e:  # noqa: BLE001
            print(f"WARN: {w.name} failed: {e}")
            continue
        if gr is None:
            continue
        print(
            f"{w.name}: bpm={gr['bpm']:.0f} len={gr['lengthBeats']} notes={len(gr['notes'])}"
        )
        grooves.append(gr)

    emit_cpp_header(grooves, args.output)
    print(f"\nWrote {len(grooves)} grooves to {args.output}")


if __name__ == "__main__":
    main()
