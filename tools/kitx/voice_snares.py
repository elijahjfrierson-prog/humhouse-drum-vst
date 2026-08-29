#!/usr/bin/env python3
"""Find the tone each kit's snare rings on and write the notch into its kit.json.

A snare with one loud partial left ringing after the stroke does not read as a
snare with character: it reads as a bell bolted to the drum, which is why a
drummer tapes the head or drops a wallet on it before a take. This measures the
ring of the loudest strokes - the spectrum of the tail, with the body of the
drum ignored - and writes the strongest partial plus how much of it to take out
as ``ringHz`` / ``ringCut`` in the kit's snare voicing. The KitEngine applies it
as a narrow notch on the snare's own bus.

    ./voice_snares.py content/Kits
"""
import argparse
import glob
import json
import os

import numpy as np
import soundfile as sf

# The tail, not the stroke: the ring is what is left once the stick is gone.
TAIL_FROM, TAIL_TO = 0.08, 0.60

# Below this the partial is the drum's own body and taking it out would leave a
# thin snare, so only what rings above it counts as a tone.
MIN_HZ, MAX_HZ = 250.0, 1200.0

# How much louder than the rest of the tail a partial has to be before it is a
# ring rather than the drum's timbre, and the deepest cut we will write.
PROMINENCE = 8.0
MAX_CUT_DB = -6.0

# A partial this high is not the note of the drum any more, it is the bell tone
# a drummer tapes out, so it is allowed a deeper cut than a shell note.
BELL_HZ = 500.0
BELL_CUT_DB = -10.0

SNARE_PIECES = ("snare", "snare_rim")


def ring(paths):
    """(hertz, prominence) of the loudest partial in the tail of a snare."""
    spectrum, freqs = None, None
    for path in paths:
        audio, rate = sf.read(path, always_2d=True)
        mono = audio.mean(axis=1)
        # One window length for every stroke, zero-padded where the recording is
        # shorter, so the spectra of a set can be summed.
        length = int((TAIL_TO - TAIL_FROM) * rate)
        tail = np.zeros(length)
        cut = mono[int(TAIL_FROM * rate):int(TAIL_TO * rate)]
        if len(cut) < 1024:
            continue
        tail[:len(cut)] = cut
        magnitude = np.abs(np.fft.rfft(tail * np.hanning(length)))
        freqs = np.fft.rfftfreq(length, 1.0 / rate)
        spectrum = magnitude if spectrum is None else spectrum + magnitude

    if spectrum is None:
        return None

    band = (freqs >= MIN_HZ) & (freqs <= MAX_HZ)
    peak = int(np.argmax(spectrum * band))
    # Prominence against the middle of the band, so a drum whose tail is
    # broadband noise - which is what a snare should be - is left alone, while
    # one partial standing over the rest is found however loud the stroke was.
    rest = float(np.median(spectrum[band]))
    return float(freqs[peak]), float(spectrum[peak] / (rest + 1e-12))


def loudest(kit, piece, count=4):
    files = sorted(glob.glob(os.path.join(kit, f"*{piece}_v*_rr*_close.flac")))
    files = [f for f in files if os.path.basename(f).split("_v")[0].endswith(piece)]
    if not files:
        return []
    files.sort(key=lambda f: np.abs(sf.read(f, always_2d=True)[0]).max())
    return files[-count:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kits")
    args = ap.parse_args()

    for kit in sorted(glob.glob(os.path.join(args.kits, "*"))):
        manifest_path = os.path.join(kit, "kit.json")
        if not os.path.isfile(manifest_path):
            continue
        with open(manifest_path) as f:
            manifest = json.load(f)

        voicing = {entry["piece"]: entry for entry in manifest.get("voicing", [])}
        for piece in SNARE_PIECES:
            files = loudest(kit, piece)
            if not files:
                continue
            found = ring(files)
            if found is None:
                continue
            hertz, prominence = found
            entry = voicing.setdefault(piece, dict(piece=piece))
            if prominence < PROMINENCE:
                entry.pop("ringHz", None)
                entry.pop("ringCut", None)
                print(f"{os.path.basename(kit):16} {piece:10} "
                      f"tail is noise ({prominence:.1f}x at {hertz:.0f} Hz)")
                continue

            # Deeper cut the more the partial stands over the tail, by doubling
            # rather than by ratio, and floored so the drum keeps the tone that
            # makes it that drum.
            if hertz >= BELL_HZ:
                cut = max(BELL_CUT_DB,
                          -5.0 - 1.6 * np.log2(prominence / PROMINENCE))
            else:
                cut = max(MAX_CUT_DB,
                          -2.0 - 0.8 * np.log2(prominence / PROMINENCE))
            entry["ringHz"] = round(hertz, 1)
            entry["ringCut"] = round(cut, 1)
            print(f"{os.path.basename(kit):16} {piece:10} "
                  f"rings {hertz:.0f} Hz at {prominence:.1f}x -> {cut:+.1f} dB")

        if voicing:
            manifest["voicing"] = list(voicing.values())
            with open(manifest_path, "w") as f:
                json.dump(manifest, f, indent=1)


if __name__ == "__main__":
    main()
