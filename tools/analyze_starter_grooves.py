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
    "sub":  (20.0, 80.0),      # kick fundamental
    "kick": (60.0, 180.0),     # kick body
    "low":  (180.0, 350.0),    # low tom / snare body
    "body": (300.0, 800.0),    # mid tom body / snare shell
    "crack":(1200.0, 4500.0),  # snare snappy crack
    "hi":   (4500.0, 9000.0),  # hat / ride stick tick
    "air":  (9000.0, 16000.0), # crash / open-hat shimmer
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
    """Classify a single onset by spectral centroid + band-energy ratios.

    v1.6.1-rc.2 — rewritten to be strict & conservative. We ONLY emit
    KICK / SNARE / CLOSED_HAT / RIDE / CRASH / LOW_TOM / MID_TOM. Any
    ambiguous hit falls back to CLOSED_HAT (quietest mis-classification
    from a listener's perspective). Decision is centroid-first so that
    "obviously a kick" (very low centroid) can never become a hat, and
    "obviously a cymbal" (centroid > 5 kHz with long ring) can never
    become a kick.
    """
    # Use TWO windows: a tiny attack spec (1 frame) dominated by the
    # stick/click transient, and a body spec (frames 3..8) that captures
    # the resonant decay. Kicks have low-dominant bodies even when the
    # attack frame has a broadband click; snares have mid-heavy bodies;
    # cymbals sustain high-band energy for many frames.
    f0 = max(0, frame_idx)
    atk_end = min(stft_mag.shape[1], frame_idx + 2)
    body_start = min(stft_mag.shape[1], frame_idx + 3)
    body_end = min(stft_mag.shape[1], frame_idx + 9)
    atk_spec  = stft_mag[:, f0:atk_end].mean(axis=1)
    if body_end > body_start:
        body_spec = stft_mag[:, body_start:body_end].mean(axis=1)
    else:
        body_spec = atk_spec
    spec = atk_spec  # legacy alias for centroid calc
    freqs = np.linspace(0.0, sr / 2.0, spec.shape[0])
    if spec.sum() < 1e-7:
        return K_CLOSED_HAT

    def band_of(s, lo, hi):
        m = (freqs >= lo) & (freqs < hi)
        return float(s[m].mean()) if m.any() else 0.0

    def band(lo, hi):
        return band_of(atk_spec, lo, hi)

    def bband(lo, hi):
        return band_of(body_spec, lo, hi)

    sub_e   = band(*BAND_EDGES["sub"])
    kick_e  = band(*BAND_EDGES["kick"])
    low_e   = band(*BAND_EDGES["low"])
    body_e  = band(*BAND_EDGES["body"])
    crack_e = band(*BAND_EDGES["crack"])
    hi_e    = band(*BAND_EDGES["hi"])
    air_e   = band(*BAND_EDGES["air"])

    low_total  = sub_e + kick_e
    mid_total  = low_e + body_e
    high_total = hi_e + air_e

    # Spectral centroid — the single most reliable signal.
    centroid = float(np.sum(spec * freqs) / (np.sum(spec) + 1e-9))

    # High-band sustain: how long does the energy in 4.5–16 kHz stay above
    # 40% of its attack-frame peak? Closed hat ~3-6 frames, ride ~12-20,
    # open hat / crash ~25+.
    sustain_frames = 0
    if frame_idx < stft_mag.shape[1] - 6:
        peak_hi = band_energy(stft_mag[:, frame_idx:frame_idx + 1], sr,
                              (BAND_EDGES["hi"][0], BAND_EDGES["air"][1])).mean()
        if peak_hi > 1e-6:
            thresh = 0.4 * peak_hi
            for k in range(frame_idx + 1, min(stft_mag.shape[1], frame_idx + 45)):
                hi_k = band_energy(stft_mag[:, k:k + 1], sr,
                                   (BAND_EDGES["hi"][0], BAND_EDGES["air"][1])).mean()
                if hi_k < thresh:
                    break
                sustain_frames += 1

    # Body-window energies (100 ms after attack). Kicks show dominant
    # low-band energy in the body even if their attack-frame click was
    # broadband; cymbals keep their high-band energy high in the body.
    body_sub   = bband(*BAND_EDGES["sub"])
    body_kick  = bband(*BAND_EDGES["kick"])
    body_low   = bband(*BAND_EDGES["low"])
    body_body  = bband(*BAND_EDGES["body"])
    body_hi    = bband(*BAND_EDGES["hi"])
    body_air   = bband(*BAND_EDGES["air"])
    body_lows  = body_sub + body_kick
    body_highs = body_hi + body_air

    # Body-window centroid — stable against attack-frame click artefacts.
    if body_spec.sum() > 1e-7:
        body_centroid = float(np.sum(body_spec * freqs) / (np.sum(body_spec) + 1e-9))
    else:
        body_centroid = centroid

    # --- STRICT decision tree ------------------------------------------
    # rc.2 final — uses BODY spec (post-attack decay) as the primary
    # signal. Attack-frame click artefacts lie for kicks (their click
    # can register as broadband), but 80 ms later the kick is a pure
    # low-frequency sine — so the body centroid is reliable.

    # 1. KICK — diagnostic: a kick's body window (≈60–180 ms after
    #    attack) has MASSIVE low-band energy (sub+kick bands) that
    #    dwarfs every other band. Body centroid is NOT reliable
    #    because broadband room noise in the tail pulls it up, but
    #    the absolute low-band energy is unambiguous.
    body_mid_total = body_low + body_body
    if body_lows > body_mid_total * 2.0 and body_lows > body_highs * 5.0 \
       and body_lows > 0.3:
        return K_KICK
    # Weak-kick fallback: even at low overall level, if lows dominate
    # by a wide margin relative to everything else, it's a kick.
    if body_lows > body_mid_total * 3.0 and body_lows > body_highs * 10.0 \
       and body_lows > 0.05:
        return K_KICK

    # 2. CYMBAL FAMILY — centroid up in the air / hi band, sustained
    #    high-frequency energy. Sub-branch on sustain length: long →
    #    crash, medium → ride, short → closed hat.
    if centroid > 5000.0 and high_total > mid_total and high_total > low_total:
        if sustain_frames >= 22 and air_e > hi_e * 0.8:
            return K_CRASH
        if sustain_frames >= 10:
            return K_RIDE
        return K_CLOSED_HAT

    # 3. SNARE — anything in the mid centroid range (≈ 500 Hz up to
    #    5 kHz) that has at least a token amount of crack energy. This
    #    is the WIDEST band because a real snare covers a huge tonal
    #    range (deep-tuned wooden snare at 700 Hz → piccolo at 4 kHz).
    if 500.0 <= centroid <= 5000.0 and crack_e > 0.001:
        # Reject if it's clearly a tom (no crack, body dominates).
        if crack_e < body_e * 0.3 and body_e > crack_e * 3.0 \
           and centroid < 1200.0 and high_total < mid_total * 0.4:
            # likely a tom
            if sub_e > body_e * 0.6:
                return K_LOW_TOM
            return K_MID_TOM
        return K_SNARE

    # 4. HIGH-CENTROID leftover — centroid 3.5–5 kHz with low sustain
    #    that wasn't picked up by the snare branch: call it a hat.
    if centroid > 3500.0:
        if sustain_frames >= 22:
            return K_CRASH
        if sustain_frames >= 10:
            return K_RIDE
        return K_CLOSED_HAT

    # 5. MID-TOM fallback for body-heavy hits below 500 Hz centroid
    #    (sub-bass toms, dropped snares with no crack).
    if mid_total > high_total * 1.3 and crack_e < body_e * 0.4:
        if sub_e > body_e * 0.6:
            return K_LOW_TOM
        return K_MID_TOM

    # 6. Ultimate fall-through — treat as closed hat (quiet ghost).
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
    # v1.6.1-rc.2 — single conservative pass. The ghost-note pass from rc.1
    # was creating garbage onsets that got mis-classified. 40 ms minimum
    # separation prevents the detector from double-triggering on long
    # hits (crashes, rides, open hats).
    onset_frames = librosa.onset.onset_detect(
        onset_envelope=onset_env, sr=sr, hop_length=hop,
        backtrack=True, delta=0.15, wait=3,
    )
    min_sep = int(0.040 * sr / hop)
    filtered = []
    for f in onset_frames:
        if filtered and f - filtered[-1] < min_sep:
            continue
        filtered.append(int(f))
    onset_frames = np.array(filtered, dtype=int)
    if onset_frames.size == 0:
        return None

    onset_times = librosa.frames_to_time(onset_frames, sr=sr, hop_length=hop)
    bpm = estimate_bpm(y, sr, onset_env)
    quantised = quantise_to_grid(onset_times, bpm, "1/16")

    # v1.6.1 — measure the global peak-strength once so velocities normalise
    # across the clip. The result is a true dynamic range (quiet ghost
    # ~0.35, accent backbeat ~1.0) rather than the old 0.55–1.0 squash.
    peak_env = float(np.max(onset_env)) if onset_env.size else 1.0
    peak_env = max(peak_env, 1e-6)

    beats = []
    for t_raw, t_q, f in zip(onset_times, quantised, onset_frames):
        note = classify_onset(stft, sr, int(f), hop)
        beat_pos = t_q * (bpm / 60.0)
        # Velocity = blend of local peak amplitude and onset-envelope
        # strength, both normalised. Gives us real bedroom→stadium range.
        win = y[int(t_raw * sr):int(t_raw * sr) + int(0.04 * sr)]
        peak_amp = float(np.max(np.abs(win))) if win.size else 0.5
        env_strength = float(onset_env[int(f)] / peak_env) if 0 <= int(f) < onset_env.size else 0.5
        vel = 0.35 + 0.45 * peak_amp + 0.30 * env_strength
        vel = float(np.clip(vel, 0.30, 1.0))
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
