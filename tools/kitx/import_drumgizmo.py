#!/usr/bin/env python3
"""Build a HumHouse kit from a DrumGizmo sample library (both CC-BY 4.0).

These libraries record every stroke through the whole microphone array, one
mono file per microphone per stroke, so a kit carries its own bleed. This picks
the strokes Drums X uses, sorts them into velocity layers by how hard they were
hit, folds the overhead and ambience pairs into stereo files, and writes a kit
folder plus the `kit.json` the KitEngine loads.

MuldjordKit is a Tama Superstar with two kick drums, which is what makes it the
metal kit here: the two kicks become the round robins of a double kick pedal.
DRSKit is a drier, tighter rock kit with sampled rimshots and hat articulations.

    ./import_drumgizmo.py --kit muldjord <checkout> <out-dir>
"""
import argparse
import json
import os
import re
import shutil
import sys
import zlib

import numpy as np
import soundfile as sf

# Per library: where the samples live, how its instrument folders map onto the
# Drums X articulations, and which of its microphones is each instrument's close
# mic. Several articulations share one recording (four sampled hat openings, not
# six); crashes were never close-miked, so their overheads stand in.
PROFILES = {
    "muldjord": dict(
        name="Muldjord Metal",
        folder=os.path.join("DrumGizmo", "MuldjordKit"),
        source="MuldjordKit by Lars Muldjord, CC-BY 4.0",
        pieces=[
            ("kick",        ["KdrumL", "KdrumR"]),
            ("snare",       ["Snare"]),
            ("snare_rim",   ["Snare"]),
            ("sidestick",   ["SnareRest"]),
            ("hat_tight",   ["HihatClosed"]),
            ("hat_closed",  ["HihatClosed"]),
            ("hat_pedal",   ["HihatClosed"]),
            ("hat_open_1",  ["HihatOpen"]),
            ("hat_open_2",  ["HihatOpen"]),
            ("hat_open_3",  ["HihatOpen"]),
            ("hat_open_4",  ["HihatOpen"]),
            ("ride",        ["RideL"]),
            ("ride_edge",   ["RideR"]),
            ("ride_bell",   ["RideLBell", "RideRBell"]),
            ("left_crash",  ["CrashL"]),
            ("right_crash", ["CrashR"]),
            ("ride_crash",  ["CrashR"]),
            ("china",       ["China"]),
            ("crash_3",     ["China"]),
            ("tom_1",       ["Tom1"]),
            ("tom_2",       ["Tom2"]),
            ("tom_3",       ["Tom3"]),
            ("tom_4",       ["Tom4"]),
        ],
        close={
            "KdrumL": "KdrumL", "KdrumR": "KdrumR",
            "Snare": "Snare_top", "SnareRest": "Snare_top",
            "Tom1": "Tom1", "Tom2": "Tom2", "Tom3": "Tom3", "Tom4": "Tom4",
            "HihatClosed": "Hihat", "HihatOpen": "Hihat",
            "RideL": "RideL", "RideR": "RideR",
            "RideLBell": "RideL", "RideRBell": "RideR",
        },
        license="LICENSE-MuldjordKit.txt",
    ),
    "drs": dict(
        name="DRS Rock",
        folder=os.path.join("DrumGizmo", "DRSKit"),
        source="DRSKit by the DrumGizmo team and Jes Eiler (DRSDrums), CC-BY 4.0",
        pieces=[
            ("kick",        ["Kdrum_with_contact", "Kdrum_without_contact"]),
            ("snare",       ["Snare"]),
            ("snare_rim",   ["Snare_rim"]),
            ("sidestick",   ["Snare_rest"]),
            ("hat_tight",   ["Hihat_closed"]),
            ("hat_closed",  ["Hihat_closed"]),
            ("hat_pedal",   ["Hihat_foot"]),
            ("hat_open_1",  ["Hihat_semi_open"]),
            ("hat_open_2",  ["Hihat_semi_open"]),
            ("hat_open_3",  ["Hihat_open"]),
            ("hat_open_4",  ["Hihat_open"]),
            ("ride",        ["Ride_tip"]),
            ("ride_edge",   ["Ride_shank"]),
            ("ride_bell",   ["Ride_tip_bell"]),
            ("left_crash",  ["Crash_left_tip"]),
            ("right_crash", ["Crash_right_tip"]),
            ("ride_crash",  ["Crash_right_shank"]),
            ("tom_1",       ["Tom1"]),
            ("tom_2",       ["Tom2"]),
            ("tom_3",       ["Tom3"]),
            ("tom_4",       ["Tom3"]),
        ],
        close={
            "Kdrum_with_contact": "Kdrum_front",
            "Kdrum_without_contact": "Kdrum_front",
            "Snare": "Snare_top", "Snare_rim": "Snare_top",
            "Snare_rest": "Snare_top",
            "Tom1": "Tom1", "Tom2": "Tom2", "Tom3": "Tom3",
            "Hihat_closed": "Hihat", "Hihat_open": "Hihat",
            "Hihat_semi_open": "Hihat", "Hihat_foot": "Hihat",
            "Ride_tip": "Ride", "Ride_shank": "Ride", "Ride_tip_bell": "Ride",
        },
        license="LICENSE-DRSKit.txt",
    ),
    # Only the snare: it is the drum the Sludge kit cannot share with the metal
    # kit, because a snare is what tells two kits apart. Fetched by
    # fetch_crocell_snare.py, which pulls the strokes out of the 5.6 GB archive
    # rather than the archive itself.
    "crocell-snare": dict(
        name="Crocell Snare",
        folder="",
        source="CrocellKit by the DrumGizmo team, sampled by Lars Muldjord, CC-BY 4.0",
        pieces=[
            ("snare",     ["Snare"]),
            ("snare_rim", ["SnareRim"]),
            ("sidestick", ["SnareRest"]),
        ],
        close={
            "Snare": "Snare_top", "SnareRim": "Snare_top",
            "SnareRest": "Snare_top",
        },
        license="LICENSE-CrocellKit.txt",
    ),
}

# Velocity layers and round robins per layer. Cymbals ring for seconds, so they
# get fewer variants than the shells - past two the download grows faster than
# the realism does.
LAYERS = 6
SHELL_ROUND_ROBINS = 3
CYMBAL_ROUND_ROBINS = 2

# Longest tail we keep, in seconds. Generous on purpose: a strike that stops
# before the drum has finished ringing is what makes a sampled kit sound gated.
CYMBAL_TAIL = 5.0
SHELL_TAIL = 5.0
SILENCE = 10 ** (-84 / 20)
FADE_SECONDS = 0.35


def stroke_index(name):
    return int(re.match(r"(\d+)-", name).group(1))


def strokes(base, instrument):
    """Stroke number -> {microphone: path} for one instrument folder."""
    folder = os.path.join(base, "Samples", instrument)
    out = {}
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".flac"):
            continue
        _, _, mic = name[:-5].partition(f"-{instrument}-")
        out.setdefault(stroke_index(name), {})[mic] = os.path.join(folder, name)
    return out


def is_cymbal(profile, instrument):
    """Anything without its own close mic, plus the ride, rings for seconds."""
    close = profile["close"].get(instrument)
    return close is None or close.lower().startswith("ride")


def peak(path):
    audio, _ = sf.read(path, dtype="float32", always_2d=True)
    return float(np.max(np.abs(audio))) if len(audio) else 0.0


# Shortest recording that is a drum being hit. The upstream libraries contain a
# few placeholder strokes of a handful of samples (MuldjordKit's first open hat,
# for one); shipped as a kit sample, those play as a click and nothing else,
# which is exactly what a gated sample sounds like.
MIN_STROKE_SECONDS = 0.05


def is_real_stroke(path):
    info = sf.info(path)
    return info.frames >= MIN_STROKE_SECONDS * info.samplerate


def pick(base, profile, instruments, round_robins):
    """[layer][variant] -> {microphone: path}, softest layer first."""
    hits = []
    for instrument in instruments:
        close = profile["close"].get(instrument, "OHL")
        for _, mics in sorted(strokes(base, instrument).items()):
            if close in mics and is_real_stroke(mics[close]):
                hits.append((peak(mics[close]), instrument, mics))
    if not hits:
        sys.exit(f"no strokes found for {instruments}")

    hits.sort(key=lambda h: h[0])
    count = min(LAYERS, len(hits))
    bounds = [round(i * len(hits) / count) for i in range(count + 1)]

    out = []
    for lo, hi in zip(bounds, bounds[1:]):
        group = hits[lo:hi]
        # Spread the kept variants across the layer so the round robins are as
        # different from each other as the recordings allow.
        take = min(round_robins, len(group))
        idx = np.linspace(0, len(group) - 1, take).round().astype(int)
        out.append([(group[i][1], group[i][2]) for i in idx])
    return out


def write_sample(sources, dest, cymbal):
    """One microphone of one stroke -> dithered 16-bit FLAC, tail intact."""
    channels = []
    rate = None
    for path in sources:
        audio, rate = sf.read(path, dtype="float64", always_2d=True)
        channels.append(audio[:, 0])

    length = min(len(c) for c in channels)
    audio = np.stack([c[:length] for c in channels], axis=1)

    loud = np.where(np.max(np.abs(audio), axis=1) > SILENCE)[0]
    natural = int(loud[-1]) + 1 if len(loud) else length
    limit = CYMBAL_TAIL if cymbal else SHELL_TAIL
    end = max(1, min(natural, int(limit * rate)))

    audio = audio[:end].copy()
    if end < natural:
        fade = min(int(FADE_SECONDS * rate), end)
        audio[end - fade:] *= np.linspace(1.0, 0.0, fade)[:, None]

    # TPDF dither at one LSB, so ghost notes do not quantise into crackle.
    lsb = 1.0 / 32768.0
    rng = np.random.default_rng(zlib.crc32(os.path.basename(dest).encode()))
    audio += (rng.random(audio.shape) - rng.random(audio.shape)) * lsb
    sf.write(dest, np.clip(audio, -1.0, 1.0), rate, subtype="PCM_16")
    return os.path.getsize(dest)


def mic_sources(profile, instrument, mics):
    """Drums X mic bus -> the source files that make it up."""
    close = profile["close"].get(instrument)
    buses = {}
    if close is not None and close in mics:
        buses["close"] = [mics[close]]
        if "OHL" in mics and "OHR" in mics:
            buses["oh"] = [mics["OHL"], mics["OHR"]]
        if "AmbL" in mics and "AmbR" in mics:
            buses["room"] = [mics["AmbL"], mics["AmbR"]]
    elif "OHL" in mics and "OHR" in mics:
        buses["close"] = [mics["OHL"], mics["OHR"]]
        if "AmbL" in mics and "AmbR" in mics:
            buses["oh"] = [mics["AmbL"], mics["AmbR"]]
    return buses


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kit", choices=sorted(PROFILES), required=True)
    ap.add_argument("repo")
    ap.add_argument("out")
    args = ap.parse_args()

    profile = PROFILES[args.kit]
    base = os.path.join(args.repo, profile["folder"])
    if not os.path.isdir(os.path.join(base, "Samples")):
        sys.exit(f"{base} has no Samples folder")

    os.makedirs(args.out, exist_ok=True)
    entries, total, written = [], 0, {}

    for piece, instruments in profile["pieces"]:
        cymbal = any(is_cymbal(profile, i) for i in instruments)
        rr = CYMBAL_ROUND_ROBINS if cymbal else SHELL_ROUND_ROBINS
        for layer, variants in enumerate(pick(base, profile, instruments, rr),
                                        start=1):
            for variant, (instrument, mics) in enumerate(variants, start=1):
                for mic, sources in mic_sources(profile, instrument, mics).items():
                    key = tuple(sources)
                    # Articulations that share a recording share the file too:
                    # the manifest places one sample in several lanes.
                    if key not in written:
                        name = f"{piece}_v{layer}_rr{variant}_{mic}.flac"
                        total += write_sample(sources,
                                              os.path.join(args.out, name),
                                              cymbal)
                        written[key] = name
                    entries.append(dict(piece=piece, layer=layer,
                                        variant=variant, mic=mic,
                                        file=written[key]))

    manifest = dict(name=profile["name"], version="1",
                    source=profile["source"], pieces=entries)
    with open(os.path.join(args.out, "kit.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    licence = os.path.join(args.repo, "LICENSE")
    if not os.path.exists(licence):
        licence = os.path.join(args.repo, profile["license"])
    shutil.copyfile(licence, os.path.join(args.out, profile["license"]))
    print(f"{total / 1e6:.0f} MB written to {args.out}, "
          f"{len(entries)} kit.json entries")


if __name__ == "__main__":
    main()
