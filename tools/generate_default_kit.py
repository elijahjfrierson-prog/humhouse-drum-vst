"""
Generate the bundled HumHouse Drums default kits (CC0).

v1.5.0 — **5 kits**, one per musical archetype, each tuned to the
spectral fingerprint of the user-provided reference MP3s:

    PopRock    -> POPROCK1SAMPLE.mp3, ageless.84.mp3       (bright wooden, 8k rolloff)
    NuRock     -> BARRIERS - Nu Rock.mp3                   (aggressive, kick-dominant, crack)
    AltRock    -> amped.83.mp3, acoldshoulder.81.mp3       (warm saturated mid-body)
    IndieLofi  -> acourseofitsown.mp3, bledthru.mp3        (dull thuddy, dark rolloff ~4.4k)
    Thrash     -> (no reference; targets thrash-metal)     (choppy quick kick, bright crack snare)

Snare character axis: dry-bright-saturated -> dull-background-thuddy
Kick character axis : round-thud           -> choppy-quick-thrash

All samples are rendered **DRY** — zero baked-in reverb, zero room tone.
The plugin's ROOM AMT knob owns every bit of wetness the user hears.

Audio is generated procedurally and released to the public domain (CC0).
"""
from __future__ import annotations

import math
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfiltfilt

SR = 48000
ROOT = Path(__file__).resolve().parent.parent / "Resources" / "DefaultKit"
ROOT.mkdir(parents=True, exist_ok=True)

KITS = ["PopRock", "NuRock", "AltRock", "IndieLofi", "Thrash"]


# ---------- DSP helpers ---------------------------------------------------
def lp(y, fc, order=4):
    sos = butter(order, fc / (SR / 2), btype="low", output="sos")
    return sosfiltfilt(sos, y)


def hp(y, fc, order=4):
    sos = butter(order, fc / (SR / 2), btype="high", output="sos")
    return sosfiltfilt(sos, y)


def bp(y, lo, hi, order=4):
    sos = butter(order, [lo / (SR / 2), hi / (SR / 2)], btype="band", output="sos")
    return sosfiltfilt(sos, y)


def to_wav(path: Path, y: np.ndarray, peak=0.9):
    if y.ndim == 1:
        y = np.stack([y, y], axis=1)
    pk = np.max(np.abs(y)) + 1e-12
    y = y * (peak / pk)
    y16 = (y * 32767).astype(np.int16)
    wavfile.write(path, SR, y16)


# ---------- per-kit character profiles -----------------------------------
# Each profile encodes the spectral fingerprint we're targeting, derived
# from the librosa analysis of the user's reference MP3s.
PROFILES = {
    "PopRock": dict(
        # POPROCK1 + ageless.84: bright wooden, high rolloff, kick-dominant.
        kick_fund=45, kick_sweep_from=140, kick_decay=0.22, kick_sub=0.5,
        kick_click_gain=0.6, kick_click_hp=1800, kick_lp=5500,
        snare_fund=205, snare_body_decay=0.14, snare_wire_lo=900, snare_wire_hi=6500,
        snare_wire_gain=0.85, snare_click_gain=0.55, snare_lp=8500,
        hat_bright=12000, hat_decay=0.033,
        cym_lp=13000, cym_attack_f=6400,
    ),
    "NuRock": dict(
        # BARRIERS Nu Rock: 66% kick, 7% crack, aggressive+punchy.
        kick_fund=52, kick_sweep_from=170, kick_decay=0.17, kick_sub=0.35,
        kick_click_gain=0.9, kick_click_hp=2200, kick_lp=6500,
        snare_fund=220, snare_body_decay=0.10, snare_wire_lo=1100, snare_wire_hi=7500,
        snare_wire_gain=1.1, snare_click_gain=0.75, snare_lp=9500,
        hat_bright=13500, hat_decay=0.025,
        cym_lp=14000, cym_attack_f=7200,
    ),
    "AltRock": dict(
        # amped.83 + acoldshoulder.81: warm saturated body-heavy (42% body).
        kick_fund=48, kick_sweep_from=130, kick_decay=0.28, kick_sub=0.55,
        kick_click_gain=0.45, kick_click_hp=1500, kick_lp=4800,
        snare_fund=190, snare_body_decay=0.18, snare_wire_lo=700, snare_wire_hi=5500,
        snare_wire_gain=0.7, snare_click_gain=0.45, snare_lp=7000,
        hat_bright=10500, hat_decay=0.04,
        cym_lp=11000, cym_attack_f=5400,
    ),
    "IndieLofi": dict(
        # acourseofitsown + bledthru: dull thuddy background, dark rolloff.
        kick_fund=42, kick_sweep_from=110, kick_decay=0.32, kick_sub=0.7,
        kick_click_gain=0.25, kick_click_hp=1100, kick_lp=3800,
        snare_fund=170, snare_body_decay=0.22, snare_wire_lo=500, snare_wire_hi=4500,
        snare_wire_gain=0.55, snare_click_gain=0.3, snare_lp=5500,
        hat_bright=8500, hat_decay=0.06,
        cym_lp=9500, cym_attack_f=4300,
    ),
    "Thrash": dict(
        # No reference MP3; targets thrash/metal: choppy quick kick, bright crack snare.
        kick_fund=58, kick_sweep_from=200, kick_decay=0.12, kick_sub=0.2,
        kick_click_gain=1.3, kick_click_hp=2800, kick_lp=7500,
        snare_fund=240, snare_body_decay=0.08, snare_wire_lo=1400, snare_wire_hi=9000,
        snare_wire_gain=1.3, snare_click_gain=1.0, snare_lp=11000,
        hat_bright=15000, hat_decay=0.02,
        cym_lp=15000, cym_attack_f=8500,
    ),
}


# ---------- drums ---------------------------------------------------------
def make_kick(p, seed=0):
    dur = 0.9
    n = int(dur * SR)
    t = np.arange(n) / SR
    pitch = p["kick_fund"] + (p["kick_sweep_from"] - p["kick_fund"]) * np.exp(-t / 0.025)
    phase = 2 * math.pi * np.cumsum(pitch) / SR
    body = np.sin(phase) * np.exp(-t / p["kick_decay"])
    sub = p["kick_sub"] * np.sin(2 * math.pi * (p["kick_fund"] - 5) * t) * np.exp(-t / (p["kick_decay"] * 1.6))
    rng = np.random.default_rng(seed)
    click = rng.standard_normal(n) * np.exp(-t / 0.004)
    click = hp(click, p["kick_click_hp"]) * p["kick_click_gain"]
    shell = 0.16 * np.sin(2 * math.pi * 120 * t) * np.exp(-t / 0.06)
    y = body + sub + click + shell
    y = lp(y, p["kick_lp"])
    front_n = SR // 50
    y[:front_n] = np.tanh(2.2 * y[:front_n]) * 1.2
    return np.concatenate([y, np.zeros(int(0.03 * SR))])


def make_snare(p, velocity=1.0, seed=0):
    dur = 0.55
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 42)
    fund = (
        np.sin(2 * math.pi * p["snare_fund"] * t) * np.exp(-t / p["snare_body_decay"])
        + 0.55 * np.sin(2 * math.pi * p["snare_fund"] * 1.65 * t) * np.exp(-t / (p["snare_body_decay"] * 0.75))
        + 0.30 * np.sin(2 * math.pi * p["snare_fund"] * 2.4 * t) * np.exp(-t / (p["snare_body_decay"] * 0.5))
    )
    click = rng.standard_normal(n) * np.exp(-t / 0.003)
    click = hp(click, 2500) * p["snare_click_gain"]
    wires = rng.standard_normal(n) * np.exp(-t / (p["snare_body_decay"] * 0.9))
    wires = bp(wires, p["snare_wire_lo"], p["snare_wire_hi"]) * p["snare_wire_gain"]
    ring = np.sin(2 * math.pi * 3100 * t) * np.exp(-t / 0.04) * 0.1
    y = 0.7 * fund + click + wires + ring
    y = lp(y, p["snare_lp"])
    y = y * (0.55 + 0.45 * velocity)
    return y


def make_hat_closed(p, seed=1):
    dur = 0.12
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 2)
    base = p["hat_bright"]
    partials = [base * r for r in [0.32, 0.42, 0.56, 0.70, 0.86, 1.0]]
    y = np.zeros(n)
    for f in partials:
        y += (0.5 + 0.5 * rng.random()) * np.sin(2 * math.pi * f * t) * np.exp(-t / 0.018)
    noise = rng.standard_normal(n) * np.exp(-t / 0.015)
    noise = bp(noise, base * 0.5, base * 1.15)
    y = 0.4 * y + noise
    y = y * np.exp(-t / p["hat_decay"])
    return y


def make_hat_pedal(p, seed=2):
    dur = 0.08
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 3)
    noise = rng.standard_normal(n) * np.exp(-t / 0.008)
    noise = bp(noise, 2500, 9000)
    thump = np.sin(2 * math.pi * 160 * t) * np.exp(-t / 0.02) * 0.35
    return 0.7 * noise + thump


def make_hat_open(p, seed=3):
    dur = 0.55
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 4)
    base = p["hat_bright"]
    partials = [base * r for r in [0.30, 0.43, 0.56, 0.68, 0.85, 1.0]]
    y = np.zeros(n)
    for f in partials:
        y += np.sin(2 * math.pi * f * t) * np.exp(-t / 0.22)
    y = 0.35 * y
    noise = rng.standard_normal(n) * np.exp(-t / 0.18)
    noise = bp(noise, base * 0.42, base * 1.05)
    y = y + 0.8 * noise
    y = y * np.exp(-t / (p["hat_decay"] * 9))
    return y


def make_tom(p, fundamental, seed=5):
    dur = 0.85
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed)
    pitch = fundamental * (1 + 0.4 * np.exp(-t / 0.02))
    phase = 2 * math.pi * np.cumsum(pitch) / SR
    body = np.sin(phase) * np.exp(-t / 0.30)
    partial = 0.45 * np.sin(2 * math.pi * fundamental * 2.1 * t) * np.exp(-t / 0.16)
    click = rng.standard_normal(n) * np.exp(-t / 0.003)
    click = hp(click, 2200) * 0.3
    y = body + partial + click
    y = lp(y, max(4200, int(fundamental * 32)))
    return y


def make_ride(p, seed=6):
    dur = 1.2
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 6)
    base = p["cym_attack_f"]
    partials = [base * r for r in [0.08, 0.14, 0.20, 0.33, 0.50, 0.72, 1.0, 1.38, 1.86]]
    y = np.zeros(n)
    for f in partials:
        y += np.sin(2 * math.pi * f * t) * np.exp(-t / (0.8 if f < 3000 else 0.45))
    y = 0.3 * y
    noise = rng.standard_normal(n) * np.exp(-t / 0.55)
    noise = bp(noise, base * 0.4, p["cym_lp"])
    ping = np.sin(2 * math.pi * base * t) * np.exp(-t / 0.08) * 0.5
    return y + 0.6 * noise + ping


def make_ride_bell(p, seed=7):
    dur = 0.9
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 7)
    base = p["cym_attack_f"] * 0.9
    partials = [base * r for r in [0.15, 0.23, 0.32, 0.45, 0.61, 0.80, 1.0]]
    y = np.zeros(n)
    for f in partials:
        y += np.sin(2 * math.pi * f * t) * np.exp(-t / 0.55)
    noise = rng.standard_normal(n) * np.exp(-t / 0.08)
    noise = bp(noise, 3000, p["cym_lp"]) * 0.4
    return 0.35 * y + noise


def make_crash(p, seed=8):
    dur = 2.2
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 8)
    base = p["cym_attack_f"]
    partials = [base * r for r in [0.06, 0.10, 0.16, 0.26, 0.40, 0.58, 0.81, 1.18, 1.68, 2.06]]
    y = np.zeros(n)
    for f in partials:
        y += (0.4 + 0.6 * rng.random()) * np.sin(2 * math.pi * f * t) * np.exp(-t / 1.1)
    y = 0.28 * y
    noise = rng.standard_normal(n) * np.exp(-t / 1.0)
    noise = bp(noise, 2000, p["cym_lp"])
    attack = np.sin(2 * math.pi * base * t) * np.exp(-t / 0.04) * 0.4
    return y + 0.9 * noise + attack


def make_china(p, seed=9):
    dur = 1.4
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 9)
    base = p["cym_attack_f"]
    partials = [base * r for r in [0.07, 0.12, 0.20, 0.29, 0.42, 0.64, 0.97, 1.37, 1.92]]
    y = np.zeros(n)
    for f in partials:
        y += (0.5 + 0.5 * rng.random()) * np.sin(2 * math.pi * f * t) * np.exp(-t / 0.75)
    y = 0.3 * y
    noise = rng.standard_normal(n) * np.exp(-t / 0.55)
    noise = bp(noise, 2500, p["cym_lp"])
    attack = np.tanh(4 * rng.standard_normal(n)) * np.exp(-t / 0.012)
    attack = bp(attack, 3500, min(11000, p["cym_lp"])) * 0.5
    return y + 0.8 * noise + attack


# ---------- write everything ----------------------------------------------
def write_all():
    for old in ROOT.glob("*.wav"):
        old.unlink()

    for kit_name in KITS:
        p = PROFILES[kit_name]
        prefix = f"{kit_name}__"

        to_wav(ROOT / f"{prefix}kick.wav",       make_kick(p, seed=hash(kit_name) & 0xffff))
        # 3 snare velocity layers — soft/med/hard.
        to_wav(ROOT / f"{prefix}snare_1.wav",    make_snare(p, velocity=0.55, seed=1))
        to_wav(ROOT / f"{prefix}snare_2.wav",    make_snare(p, velocity=0.78, seed=2))
        to_wav(ROOT / f"{prefix}snare_3.wav",    make_snare(p, velocity=1.00, seed=3))
        to_wav(ROOT / f"{prefix}snare.wav",      make_snare(p, velocity=1.00, seed=3))
        to_wav(ROOT / f"{prefix}hat_closed.wav", make_hat_closed(p))
        to_wav(ROOT / f"{prefix}hat_pedal.wav",  make_hat_pedal(p))
        to_wav(ROOT / f"{prefix}hat_open.wav",   make_hat_open(p))
        to_wav(ROOT / f"{prefix}tom_high.wav",   make_tom(p, 200, seed=10))
        to_wav(ROOT / f"{prefix}tom_mid.wav",    make_tom(p, 150, seed=11))
        to_wav(ROOT / f"{prefix}tom_low.wav",    make_tom(p, 95,  seed=12))
        to_wav(ROOT / f"{prefix}ride.wav",       make_ride(p))
        to_wav(ROOT / f"{prefix}ride_bell.wav",  make_ride_bell(p))
        to_wav(ROOT / f"{prefix}crash.wav",      make_crash(p))
        to_wav(ROOT / f"{prefix}china.wav",      make_china(p))
        print(f"  kit written: {kit_name}")

    (ROOT / "README.txt").write_text(
        "HumHouse Drums — bundled default kits (CC0, public domain).\n"
        "Generated by tools/generate_default_kit.py. 5 kits:\n"
        "  PopRock  — POPROCK1 + ageless.84   (bright wooden)\n"
        "  NuRock   — BARRIERS Nu Rock        (aggressive/punchy)\n"
        "  AltRock  — amped.83 + acoldshoulder (warm saturated)\n"
        "  IndieLofi— acourseofitsown + bledthru (dull thuddy)\n"
        "  Thrash   — (thrash metal target)   (choppy/bright)\n"
        "All samples are fully dry — the ROOM AMT knob owns all wetness.\n"
    )


if __name__ == "__main__":
    write_all()
    total = sum(f.stat().st_size for f in ROOT.glob("*.wav"))
    print(f"total bundled size: {total/1024/1024:.2f} MB across {len(list(ROOT.glob('*.wav')))} files")
