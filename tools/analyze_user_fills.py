#!/usr/bin/env python3
"""v1.6.1-rc.9 — analyze the 20 user-supplied intensity-graded fill WAVs and
emit a regenerated Source/FillLibrary.generated.h.

Each WAV is one full-bar drum fill graded 10%-100% intensity. We onset-detect
+ band-classify each hit (reusing the analyzer logic from
analyze_starter_grooves.py), quantise to a 1/16 grid, and emit a
MidiPattern. The resulting library is what the FILL cycler rotates through
and what auto-fill drops on bar 8 of every 8-bar phrase.

Usage:
    python3 tools/analyze_user_fills.py \
        --input tools/user_fills \
        --output Source/FillLibrary.generated.h
"""
from __future__ import annotations

import argparse
import math
import pathlib
import re

import librosa
import numpy as np

# 8-lane canonical numbers (PluginProcessor.cpp noteToLane()).
K_KICK = 36
K_SNARE = 38
K_CLOSED_HAT = 42
K_LOW_TOM = 43
K_HIGH_TOM = 48
K_L_CRASH = 49
K_RIDE = 51
K_R_CRASH = 57


def band_energy(stft_mag, sr, edges):
    freqs = np.linspace(0.0, sr / 2.0, stft_mag.shape[0])
    lo, hi = edges
    mask = (freqs >= lo) & (freqs < hi)
    if not mask.any():
        return np.zeros(stft_mag.shape[1])
    return stft_mag[mask, :].mean(axis=0)


def classify_onset(stft_mag, sr, frame_idx):
    """8-lane fill classifier. Conservative: any ambiguous hit -> closed hat."""
    f0 = max(0, frame_idx)
    atk_end = min(stft_mag.shape[1], frame_idx + 2)
    body_start = min(stft_mag.shape[1], frame_idx + 3)
    body_end = min(stft_mag.shape[1], frame_idx + 9)
    atk_spec = stft_mag[:, f0:atk_end].mean(axis=1)
    body_spec = (
        stft_mag[:, body_start:body_end].mean(axis=1)
        if body_end > body_start
        else atk_spec
    )

    freqs = np.linspace(0.0, sr / 2.0, atk_spec.shape[0])
    if atk_spec.sum() <= 0:
        return K_CLOSED_HAT
    centroid = float(np.sum(freqs * atk_spec) / np.sum(atk_spec))

    sub_e = float(band_energy(stft_mag, sr, (20.0, 80.0))[f0:atk_end].mean())
    kick_e = float(band_energy(stft_mag, sr, (60.0, 180.0))[f0:atk_end].mean())
    low_e = float(band_energy(stft_mag, sr, (180.0, 350.0))[f0:atk_end].mean())
    body_e = float(band_energy(stft_mag, sr, (300.0, 800.0))[f0:atk_end].mean())
    crack_e = float(band_energy(stft_mag, sr, (1200.0, 4500.0))[f0:atk_end].mean())
    hi_e = float(band_energy(stft_mag, sr, (4500.0, 9000.0))[f0:atk_end].mean())
    air_e = float(band_energy(stft_mag, sr, (9000.0, 16000.0))[f0:atk_end].mean())

    body_air_e = float(band_energy(stft_mag, sr, (9000.0, 16000.0))[body_start:body_end].mean()) if body_end > body_start else 0.0
    body_hi_e = float(band_energy(stft_mag, sr, (4500.0, 9000.0))[body_start:body_end].mean()) if body_end > body_start else 0.0

    # Cymbal: long sustain, lots of air-band energy.
    if body_air_e > 0.5 * (sub_e + kick_e + low_e) and centroid > 4500.0:
        # Crash vs ride distinguished by air vs hi ratio.
        if air_e > hi_e * 1.2:
            # Alternate L/R crash by frame index parity so fills get both.
            return K_L_CRASH if (frame_idx % 2 == 0) else K_R_CRASH
        return K_RIDE

    # Kick: low-dominant body.
    if (sub_e + kick_e) > (body_e + crack_e) * 1.1 and centroid < 1200.0:
        return K_KICK

    # Snare: mid-heavy with stick crack.
    if crack_e > body_e * 0.6 and crack_e > hi_e * 0.4 and 600.0 < centroid < 4500.0:
        return K_SNARE

    # Toms: low/mid body, weak crack, no sustain.
    if low_e > crack_e * 1.2 and body_air_e < hi_e * 0.5:
        if sub_e > body_e * 0.55:
            return K_LOW_TOM
        return K_HIGH_TOM

    # Hat / ghost.
    return K_CLOSED_HAT


def estimate_bpm_from_fill(y, sr, onset_env, fallback=120.0):
    try:
        tempo, _ = librosa.beat.beat_track(onset_envelope=onset_env, sr=sr)
        bpm = float(np.asarray(tempo).flatten()[0])
        if not math.isfinite(bpm) or bpm <= 0:
            return fallback
        while bpm > 180.0:
            bpm *= 0.5
        while bpm < 70.0:
            bpm *= 2.0
        return float(round(bpm))
    except Exception:
        return fallback


def quantise_to_grid(onset_times, bpm, div=4):
    step = 60.0 / bpm / div
    return np.round(onset_times / step) * step


def analyze_fill(path: pathlib.Path):
    y, sr = librosa.load(str(path), sr=None, mono=True)
    if sr < 22050:
        y = librosa.resample(y, orig_sr=sr, target_sr=44100)
        sr = 44100

    nz = np.where(np.abs(y) > 0.005)[0]
    if nz.size:
        y = y[nz[0]:]

    hop = 256
    stft = np.abs(librosa.stft(y, n_fft=1024, hop_length=hop))
    onset_env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)
    # Permissive onset detect — fills have rapid 32nd / triplet rolls.
    onset_frames = librosa.onset.onset_detect(
        onset_envelope=onset_env, sr=sr, hop_length=hop,
        backtrack=True, delta=0.10, wait=2,
    )
    # 30 ms minimum separation captures fast double strokes without
    # double-triggering the same hit.
    min_sep = int(0.030 * sr / hop)
    filtered = []
    for f in onset_frames:
        if filtered and f - filtered[-1] < min_sep:
            continue
        filtered.append(int(f))
    onset_frames = np.array(filtered, dtype=int)
    if onset_frames.size == 0:
        return None

    onset_times = librosa.frames_to_time(onset_frames, sr=sr, hop_length=hop)
    bpm = estimate_bpm_from_fill(y, sr, onset_env)
    quantised = quantise_to_grid(onset_times, bpm, div=8)  # 1/32 grid

    peak_env = float(np.max(onset_env)) if onset_env.size else 1.0
    peak_env = max(peak_env, 1e-6)

    notes = []
    for t_raw, t_q, f in zip(onset_times, quantised, onset_frames):
        note = classify_onset(stft, sr, int(f))
        beat_pos = t_q * (bpm / 60.0)
        win = y[int(t_raw * sr):int(t_raw * sr) + int(0.04 * sr)]
        peak_amp = float(np.max(np.abs(win))) if win.size else 0.5
        env_strength = float(onset_env[int(f)] / peak_env) if 0 <= int(f) < onset_env.size else 0.5
        vel = 0.30 + 0.50 * peak_amp + 0.30 * env_strength
        vel = float(np.clip(vel, 0.20, 1.0))
        notes.append({
            "note": int(note),
            "beat": float(round(beat_pos, 4)),
            "vel": round(vel, 3),
            "len": 0.25,
        })

    # Cap fill length at 4 beats (1 bar). Each user fill is a "full bar fill".
    notes = [n for n in notes if n["beat"] < 4.0]

    # Dedup hits landing on the same 1/64 slot for the same drum.
    seen = set()
    uniq = []
    for n in notes:
        key = (n["note"], round(n["beat"] * 16))
        if key in seen:
            continue
        seen.add(key)
        uniq.append(n)

    return {
        "name": path.stem,
        "bpm": float(bpm),
        "notes": uniq,
    }


_INTENSITY_RE = re.compile(r"(\d+)\s*(?:_|%)\s*Intensity", re.IGNORECASE)


def parse_intensity(stem: str) -> int:
    m = _INTENSITY_RE.search(stem)
    return int(m.group(1)) if m else 50


def humanize(stem: str) -> str:
    pretty = re.sub(r"[_]+", " ", stem).strip()
    pretty = re.sub(r"\s+", " ", pretty)
    return pretty


def emit_header(fills, out_path: pathlib.Path):
    lines = [
        "// v1.6.1-rc.9 — GENERATED by tools/analyze_user_fills.py.",
        "// DO NOT EDIT: re-run the analyzer to update.",
        "// Source of truth = tools/user_fills/*.wav (20 intensity-graded",
        "// drum fills supplied by the user). Each fill is parsed into our",
        "// 8-lane canonical MIDI (kick=36 snare=38 hat=42 lowTom=43",
        "// highTom=48 lcrash=49 ride=51 rcrash=57) and exposed as the",
        "// fill library the FILL cycler rotates through and the auto-fill",
        "// scheduler drops on bar 8 of every 8-bar phrase.",
        "#pragma once",
        "",
        "#include \"MidiPattern.h\"",
        "",
        "#include <string_view>",
        "#include <vector>",
        "",
        "namespace aidrum",
        "{",
        "    struct BuiltInFill",
        "    {",
        "        std::string_view name;",
        "        int             intensity;  // 10..100",
        "        MidiPattern     pattern;",
        "    };",
        "",
        "    inline const std::vector<BuiltInFill>& fillLibrary()",
        "    {",
        "        static const std::vector<BuiltInFill> kFills = [] {",
        "            std::vector<BuiltInFill> v;",
    ]

    # Sort: by intensity ascending so the cycler walks light->heavy in order;
    # intensity ties fall back to filename so HardRock and Standard variants
    # interleave predictably.
    fills_sorted = sorted(fills, key=lambda f: (f["intensity"], f["name"]))
    for fl in fills_sorted:
        lines.append("            {")
        lines.append("                MidiPattern p;")
        lines.append("                p.lengthInBeats = 4.00;")
        lines.append("                p.isFill = true;")
        for n in fl["notes"]:
            lines.append(
                f"                p.notes.push_back ({{ {n['note']}, {n['vel']}f, "
                f"{n['beat']}, {n['len']} }});"
            )
        safe_name = fl["display"].replace("\\", "\\\\").replace("\"", "\\\"")
        lines.append(
            f"                v.push_back ({{ \"{safe_name}\", "
            f"{fl['intensity']}, std::move (p) }});"
        )
        lines.append("            }")

    lines += [
        "            return v;",
        "        }();",
        "        return kFills;",
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
    fills = []
    for w in wavs:
        try:
            r = analyze_fill(w)
        except Exception as e:  # noqa: BLE001
            print(f"WARN: {w.name} failed: {e}")
            continue
        if r is None:
            continue
        intensity = parse_intensity(w.stem)
        display = humanize(w.stem)
        print(f"  {w.name}: bpm={r['bpm']:.0f} notes={len(r['notes'])} intensity={intensity}")
        fills.append({
            "name": w.stem,
            "display": display,
            "intensity": intensity,
            "bpm": r["bpm"],
            "notes": r["notes"],
        })

    emit_header(fills, args.output)
    print(f"\nWrote {len(fills)} fills to {args.output}")


if __name__ == "__main__":
    main()
