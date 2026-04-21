"""
Generate the bundled HumHouse Drums default kit (CC0).

Tuned to the fingerprint of POPROCK1SAMPLE.mp3:
  * spectral centroid ~3.7 kHz, rolloff85 ~7.2 kHz  -> rolled-off highs
  * 30% sub + 43% kick band  -> fat modern kick with sub reinforcement
  * snare fundamental ~200 Hz, body 120-300 Hz
  * room tail ~46 ms (tight, controlled)

Every file in this script is written by us and placed in the public
domain (CC0). Drop the Resources/DefaultKit/ folder that this script
generates into the plugin bundle; the SampleKit loader will pick it
up verbatim.
"""
from __future__ import annotations

import math
import os
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt, sosfiltfilt, fftconvolve

SR = 48000
OUT_DIR = Path(__file__).resolve().parent.parent / "Resources" / "DefaultKit"
OUT_DIR.mkdir(parents=True, exist_ok=True)


# ---------- helpers -------------------------------------------------------
def lp(y, fc, order=4):
    sos = butter(order, fc / (SR / 2), btype="low", output="sos")
    return sosfiltfilt(sos, y)


def hp(y, fc, order=4):
    sos = butter(order, fc / (SR / 2), btype="high", output="sos")
    return sosfiltfilt(sos, y)


def bp(y, lo, hi, order=4):
    sos = butter(order, [lo / (SR / 2), hi / (SR / 2)], btype="band", output="sos")
    return sosfiltfilt(sos, y)


def t60(coef, dur):
    """Per-sample exponential decay so that the signal drops 60dB after dur seconds."""
    n = int(dur * SR)
    return np.exp(-np.log(1000.0) * np.arange(n) / max(1, n))


def env_exp(length, tau):
    n = int(length * SR)
    return np.exp(-np.arange(n) / (tau * SR))


def env_ad(attack, decay, length):
    n = int(length * SR)
    a = int(attack * SR)
    d = n - a
    env = np.zeros(n)
    if a > 0:
        env[:a] = np.linspace(0.0, 1.0, a)
    if d > 0:
        env[a:] = np.exp(-np.arange(d) / (decay * SR))
    return env


def to_wav(path: Path, y: np.ndarray, peak=0.95):
    if y.ndim == 1:
        y = np.stack([y, y], axis=1)
    peak_val = np.max(np.abs(y)) + 1e-12
    y = y * (peak / peak_val)
    y16 = (y * 32767).astype(np.int16)
    wavfile.write(path, SR, y16)
    print(f"wrote {path.relative_to(OUT_DIR.parent.parent)}  "
          f"{y.shape[0]/SR*1000:.0f} ms  peak {peak_val:.3f}")


# Tight ~46 ms small-studio room IR — exponential noise decay.
def room_ir(t_ms=46, pre_ms=3, seed=7):
    rng = np.random.default_rng(seed)
    n = int(SR * 0.25)
    ir = rng.standard_normal(n) * np.exp(-np.arange(n) / (t_ms / 1000 * SR))
    pre = int(pre_ms / 1000 * SR)
    ir = np.concatenate([np.zeros(pre), ir])
    ir = hp(ir, 120)
    ir = lp(ir, 8000)
    ir = ir / (np.max(np.abs(ir)) + 1e-12) * 0.25
    return ir


ROOM_IR = room_ir()


def room(y, wet=0.25):
    if y.ndim == 1:
        wet_sig = fftconvolve(y, ROOM_IR)[: len(y)]
    else:
        wet_l = fftconvolve(y[:, 0], ROOM_IR)[: len(y)]
        wet_r = fftconvolve(y[:, 1], ROOM_IR)[: len(y)]
        wet_sig = np.stack([wet_l, wet_r], axis=1)
    return y + wet * wet_sig


# ---------- drums ---------------------------------------------------------
def make_kick():
    dur = 0.9
    n = int(dur * SR)
    t = np.arange(n) / SR
    # Pitch sweep 140 -> 45 Hz over 80 ms (the POPROCK1 kick fundamental is ~35-45 Hz).
    pitch = 45 + 95 * np.exp(-t / 0.025)
    phase = 2 * math.pi * np.cumsum(pitch) / SR
    body = np.sin(phase) * np.exp(-t / 0.26)
    # Sub reinforcement at 40 Hz matching the 30% sub-band weight.
    sub = 0.5 * np.sin(2 * math.pi * 40 * t) * np.exp(-t / 0.42)
    # Beater click — short HP noise burst.
    click = np.random.default_rng(0).standard_normal(n) * np.exp(-t / 0.004)
    click = hp(click, 1800) * 0.6
    # Gentle shell rustle at ~120 Hz
    shell = 0.18 * np.sin(2 * math.pi * 120 * t) * np.exp(-t / 0.06)
    y = body + sub + click + shell
    y = lp(y, 5500)
    # Shape transient — soft knee compressor via tanh saturation on front.
    front = np.tanh(3.0 * y[: SR // 50]) * 1.3
    y[: SR // 50] = front
    y = room(y, 0.18)
    # Pad with a little silence so the tail is clean.
    return np.concatenate([y, np.zeros(int(0.05 * SR))])


def make_snare(velocity=1.0, seed=0):
    dur = 0.6
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 42)
    # Wood shell fundamental ~200 Hz + partials 330 / 480 Hz.
    fund = (
        np.sin(2 * math.pi * 200 * t) * np.exp(-t / 0.16)
        + 0.55 * np.sin(2 * math.pi * 330 * t) * np.exp(-t / 0.11)
        + 0.30 * np.sin(2 * math.pi * 480 * t) * np.exp(-t / 0.07)
    )
    # Stick attack click.
    click = rng.standard_normal(n) * np.exp(-t / 0.003)
    click = hp(click, 2500) * 0.55
    # Snare wires — broadband noise 1-8 kHz, slight resonance at ~4 kHz.
    wires = rng.standard_normal(n) * np.exp(-t / 0.12)
    wires = bp(wires, 900, 6500)
    # Tonal ring at ~3k for the "crack".
    ring = np.sin(2 * math.pi * 3100 * t) * np.exp(-t / 0.04) * 0.12
    y = 0.7 * fund + click + 0.8 * wires + ring
    # Gentle HF roll-off to match rolloff85 ~7kHz.
    y = lp(y, 8500)
    y = y * (0.55 + 0.45 * velocity)
    y = room(y, 0.22)
    return y


def make_hat_closed(seed=1):
    dur = 0.12
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 2)
    # 6 metallic partials 4-12 kHz inharmonic ratio.
    partials = [4200, 5400, 7100, 8800, 10600, 12300]
    y = np.zeros(n)
    for i, f in enumerate(partials):
        y += (0.5 + 0.5 * rng.random()) * np.sin(2 * math.pi * f * t) * np.exp(-t / 0.018)
    noise = rng.standard_normal(n) * np.exp(-t / 0.015)
    noise = bp(noise, 6000, 14000)
    y = 0.4 * y + noise
    y = y * np.exp(-t / 0.035)
    y = room(y, 0.1)
    return y


def make_hat_pedal(seed=2):
    dur = 0.08
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 3)
    noise = rng.standard_normal(n) * np.exp(-t / 0.008)
    noise = bp(noise, 2500, 9000)
    thump = np.sin(2 * math.pi * 160 * t) * np.exp(-t / 0.02) * 0.35
    return 0.7 * noise + thump


def make_hat_open(seed=3):
    dur = 0.6
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 4)
    partials = [3900, 5250, 6800, 8300, 10400, 12100]
    y = np.zeros(n)
    for f in partials:
        y += np.sin(2 * math.pi * f * t) * np.exp(-t / 0.22)
    y = 0.35 * y
    noise = rng.standard_normal(n) * np.exp(-t / 0.18)
    noise = bp(noise, 5000, 13000)
    y = y + 0.8 * noise
    y = y * np.exp(-t / 0.3)
    y = room(y, 0.15)
    return y


def make_tom(fundamental, dur=0.9, seed=5):
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed)
    pitch = fundamental * (1 + 0.4 * np.exp(-t / 0.02))
    phase = 2 * math.pi * np.cumsum(pitch) / SR
    body = np.sin(phase) * np.exp(-t / 0.32)
    partial = 0.45 * np.sin(2 * math.pi * fundamental * 2.1 * t) * np.exp(-t / 0.18)
    click = rng.standard_normal(n) * np.exp(-t / 0.003)
    click = hp(click, 2200) * 0.3
    y = body + partial + click
    y = lp(y, 6000)
    y = room(y, 0.24)
    return y


def make_ride(seed=6):
    dur = 1.4
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 6)
    partials = [520, 880, 1300, 2100, 3100, 4500, 6400, 8700, 11800]
    y = np.zeros(n)
    for f in partials:
        y += np.sin(2 * math.pi * f * t) * np.exp(-t / (0.8 if f < 3000 else 0.45))
    y = 0.3 * y
    noise = rng.standard_normal(n) * np.exp(-t / 0.55)
    noise = bp(noise, 2500, 10000)
    ping = np.sin(2 * math.pi * 5200 * t) * np.exp(-t / 0.08) * 0.5
    y = y + 0.6 * noise + ping
    y = room(y, 0.18)
    return y


def make_ride_bell(seed=7):
    dur = 1.0
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 7)
    partials = [960, 1480, 2030, 2870, 3920, 5150, 7200]
    y = np.zeros(n)
    for f in partials:
        y += np.sin(2 * math.pi * f * t) * np.exp(-t / 0.55)
    noise = rng.standard_normal(n) * np.exp(-t / 0.08)
    noise = bp(noise, 3000, 7000) * 0.4
    return 0.35 * y + noise


def make_crash(seed=8):
    dur = 2.4
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 8)
    partials = [380, 620, 1000, 1640, 2500, 3700, 5200, 7600, 10800, 13200]
    y = np.zeros(n)
    for f in partials:
        y += (0.4 + 0.6 * rng.random()) * np.sin(2 * math.pi * f * t) * np.exp(-t / 1.2)
    y = 0.28 * y
    noise = rng.standard_normal(n) * np.exp(-t / 1.1)
    noise = bp(noise, 2000, 12000)
    y = y + 0.9 * noise
    # Washy attack swell
    attack = np.sin(2 * math.pi * 6400 * t) * np.exp(-t / 0.04) * 0.4
    y = y + attack
    y = room(y, 0.25)
    return y


def make_china(seed=9):
    dur = 1.6
    n = int(dur * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed + 9)
    partials = [420, 780, 1250, 1850, 2700, 4100, 6200, 8800, 12300]
    y = np.zeros(n)
    for f in partials:
        y += (0.5 + 0.5 * rng.random()) * np.sin(2 * math.pi * f * t) * np.exp(-t / 0.8)
    y = 0.3 * y
    noise = rng.standard_normal(n) * np.exp(-t / 0.55)
    noise = bp(noise, 2500, 13000)
    # Trashy distorted attack.
    attack = np.tanh(4 * rng.standard_normal(n)) * np.exp(-t / 0.012)
    attack = bp(attack, 3500, 11000) * 0.5
    y = y + 0.8 * noise + attack
    y = room(y, 0.2)
    return y


# ---------- write everything ----------------------------------------------
def write_kit():
    # Core kit.
    to_wav(OUT_DIR / "kick.wav",       make_kick())
    # Multi-velocity snare: 3 layers soft -> hard.
    to_wav(OUT_DIR / "snare_1.wav",    make_snare(velocity=0.55, seed=1))
    to_wav(OUT_DIR / "snare_2.wav",    make_snare(velocity=0.78, seed=2))
    to_wav(OUT_DIR / "snare_3.wav",    make_snare(velocity=1.00, seed=3))
    to_wav(OUT_DIR / "snare.wav",      make_snare(velocity=1.00, seed=3))
    to_wav(OUT_DIR / "hat_closed.wav", make_hat_closed())
    to_wav(OUT_DIR / "hat_pedal.wav",  make_hat_pedal())
    to_wav(OUT_DIR / "hat_open.wav",   make_hat_open())
    to_wav(OUT_DIR / "tom_high.wav",   make_tom(fundamental=200, seed=10))
    to_wav(OUT_DIR / "tom_mid.wav",    make_tom(fundamental=150, seed=11))
    to_wav(OUT_DIR / "tom_low.wav",    make_tom(fundamental=95,  seed=12))
    to_wav(OUT_DIR / "ride.wav",       make_ride())
    to_wav(OUT_DIR / "ride_bell.wav",  make_ride_bell())
    to_wav(OUT_DIR / "crash.wav",      make_crash())
    to_wav(OUT_DIR / "china.wav",      make_china())

    # License + README for the Resources folder so anyone redistributing
    # the build is covered.
    (OUT_DIR / "README.txt").write_text(
        "HumHouse Drums — bundled default kit.\n"
        "All audio generated by tools/generate_default_kit.py and released\n"
        "into the public domain (CC0). Tuned to the spectral fingerprint\n"
        "of the user-provided POPROCK1SAMPLE.mp3: fat sub-reinforced kick,\n"
        "wooden 200 Hz snare, rolled-off highs, tight ~46 ms room tail.\n"
    )
    print(f"default kit written to {OUT_DIR}")


if __name__ == "__main__":
    write_kit()
