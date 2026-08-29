#!/usr/bin/env python3
"""Fetch the CrocellKit drums (CC-BY 4.0) as a DrumGizmo sample tree.

CrocellKit ships as a 5.6 GB zip of multichannel WAVs, one file per stroke.
The Sludge Nu Metal kit is built from it so that it is a different drum set in
a different room rather than the metal kit tuned down - a re-tune of the same
recording reads as one kit played twice, whatever is done to it afterwards.
Rather than downloading the whole library this reads the zip's directory over
HTTP, keeps the strokes that become the velocity layers, and splits their
channels into the per-microphone layout the importer expects:

    <out>/Samples/<Instrument>/<n>-<Instrument>-<mic>.flac

    ./fetch_crocell_snare.py <out-dir> [--snare-only]
"""
import argparse
import io
import os
import re
import time
import xml.etree.ElementTree as ET

import numpy as np
import soundfile as sf
from remotezip import RemoteZip

URL = "https://drumgizmo.org/kits/CrocellKit/CrocellKit1_1.zip"

# The array every stroke is kept through: CrocellKit channel -> the microphone
# name the importer reads.
MICS = {"OHLeft": "OHL", "OHRight": "OHR",
        "AmbLeft": "AmbL", "AmbRight": "AmbR"}

# Instrument -> its close microphone, as the CrocellKit channel and as the name
# the importer reads. Crocell's cymbals were not close-miked, so those play
# through the overheads, which is how they were recorded.
CLOSE = {"Snare": ("SnareTop", "Snare_top"),
         "SnareRim": ("SnareTop", "Snare_top"),
         "SnareRimShot": ("SnareTop", "Snare_top"),
         "SnareRest": ("SnareTop", "Snare_top"),
         "KDrumL": ("KDrumInside", "KDrum"),
         "KDrumR": ("KDrumInside", "KDrum"),
         "Tom1": ("Tom1", "Tom1"), "Tom2": ("Tom2", "Tom2"),
         "FTom1": ("FTom1", "FTom1"), "FTom2": ("FTom2", "FTom2"),
         "HihatClosed": ("Hihat", "Hihat"),
         "HihatSemiOpen": ("Hihat", "Hihat"),
         "HihatOpen": ("Hihat", "Hihat"),
         "HihatPedal": ("Hihat", "Hihat"),
         "RideR": ("Ride", "Ride"), "RideRBell": ("Ride", "Ride")}

# Instrument -> how many strokes to keep, spread over the recorded dynamic
# range. Six layers times three round robins for the drums themselves; rims,
# stick rests and cymbal colours are colours, so they get fewer.
SNARE = {"Snare": 18, "SnareRim": 12, "SnareRest": 12}
WANTED = dict(SNARE, SnareRimShot=12,
              KDrumL=18, KDrumR=12,
              Tom1=12, Tom2=12, FTom1=12, FTom2=12,
              HihatClosed=12, HihatSemiOpen=8, HihatOpen=12, HihatPedal=6,
              RideR=12, RideRBell=8,
              CrashL=8, CrashR=8, ChinaR=6, SplashL=4)


def strokes_from_xml(xml, mics):
    """[(power, file, {channel: filechannel})], softest first."""
    root = ET.fromstring(xml)
    out = []
    for sample in root.iter("sample"):
        chans, path = {}, None
        for audio in sample.iter("audiofile"):
            chan = audio.get("channel")
            if chan in mics:
                chans[chan] = int(audio.get("filechannel"))
            if audio.get("file") is not None:
                path = audio.get("file")
        if path is not None and len(chans) == len(mics):
            out.append((float(sample.get("power", 0.0)), path, chans))
    out.sort(key=lambda s: s[0])
    return out


def read_entry(name, tries=8):
    """One entry out of the remote zip, retrying: a five gigabyte file served
    over one connection drops it now and then, and a dropped range read must
    not cost the whole fetch."""
    for attempt in range(tries):
        try:
            with RemoteZip(URL) as zf:
                return zf.read(name)
        except Exception as error:              # network, not programmer error
            if attempt == tries - 1:
                raise
            print("retrying", name, error, flush=True)
            time.sleep(2.0 * (attempt + 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--snare-only", action="store_true",
                    help="just the snare, as the first pass of this fetch did")
    args = ap.parse_args()

    for instrument, count in (SNARE if args.snare_only else WANTED).items():
        mics = dict(MICS)
        if instrument in CLOSE:
            mics[CLOSE[instrument][0]] = CLOSE[instrument][1]
        xml = read_entry(f"CrocellKit/{instrument}/{instrument}.xml").decode()
        picks = strokes_from_xml(xml, mics)
        keep = np.linspace(0, len(picks) - 1, min(count, len(picks)))
        folder = os.path.join(args.out, "Samples", instrument)
        os.makedirs(folder, exist_ok=True)

        for index in keep.round().astype(int):
            _, path, chans = picks[index]
            stroke = int(re.match(r"(\d+)-", os.path.basename(path)).group(1))
            done = [os.path.join(folder, f"{stroke}-{instrument}-{mic}.flac")
                    for mic in mics.values()]
            if all(os.path.exists(d) for d in done):
                continue

            raw = read_entry(f"CrocellKit/{instrument}/{path}")
            audio, rate = sf.read(io.BytesIO(raw), dtype="float64",
                                  always_2d=True)
            for chan, filechannel in chans.items():
                dest = os.path.join(folder,
                                    f"{stroke}-{instrument}-{mics[chan]}.flac")
                sf.write(dest, audio[:, filechannel - 1], rate,
                         subtype="PCM_24")
            print(instrument, stroke, "ok", flush=True)

    with open(os.path.join(args.out, "LICENSE-CrocellKit.txt"), "w") as f:
        f.write("CrocellKit by the DrumGizmo team, sampled by Lars Muldjord\n"
                "at JBOSound with the kit of the band Crocell.\n"
                "Creative Commons Attribution 4.0 International (CC-BY 4.0):\n"
                "https://creativecommons.org/licenses/by/4.0/\n")


if __name__ == "__main__":
    main()
