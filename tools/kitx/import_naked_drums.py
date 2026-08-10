#!/usr/bin/env python3
"""Build the HumHouse "Naked Rock" kit from Wilkinson Audio's Naked Drums.

Naked Drums (CC-BY 4.0) is a multi-mic acoustic kit with up to five recorded
velocity tiers and ten round robins per articulation. Its SFZ splits every
articulation across a pile of per-file `#define` tables; this reads those
tables, picks the mics and round robins Drums X uses, and writes a plain kit
folder plus a `kit.json` that the KitEngine can load straight off disk.

    ./import_naked_drums.py <naked-drums-repo> <out-dir>
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import zlib

import numpy as np
import soundfile as sf

RR_WORDS = ["one", "two", "three", "four", "five",
            "six", "seven", "eight", "nine", "ten"]

# How many round robins and mics we keep. Ten round robins would triple the
# download for a difference nobody hears past the fourth stroke.
ROUND_ROBINS = 4

# group-file suffix -> Drums X mic bus, per family of kit piece. Cymbals have
# no close mic of their own, so the overheads become their close mic and the
# wide china pair stands in for the overheads.
SHELL_MICS = {"close": ("in", "di", "top1"), "oh": ("oh",), "room": ("cr",)}
CYMBAL_MICS = {"close": ("oh",), "oh": ("ch",), "room": ("cr",)}

# Naked Drums group -> Drums X articulation. Several articulations are served
# by the same recording (the hat ladder is four sampled openings, not six).
PIECE_MAP = [
    ("kick/center",       ["kick"],                     SHELL_MICS),
    ("snare/center",      ["snare"],                    SHELL_MICS),
    ("snare/center#2",    ["snare_rim"],                SHELL_MICS),
    ("snare/sidestick",   ["sidestick"],                SHELL_MICS),
    ("hats/tight",        ["hat_tight"],                SHELL_MICS),
    ("hats/closed",       ["hat_closed"],               SHELL_MICS),
    ("hats/half",         ["hat_open_1", "hat_open_2"], SHELL_MICS),
    ("hats/open",         ["hat_open_3", "hat_open_4"], SHELL_MICS),
    ("hats/pedal",        ["hat_pedal"],                SHELL_MICS),
    ("ride/bow",          ["ride", "ride_edge"],        SHELL_MICS),
    ("ride/bell",         ["ride_bell"],                SHELL_MICS),
    ("crash1/edge",       ["left_crash"],               CYMBAL_MICS),
    ("crash2/edge",       ["right_crash", "ride_crash"], CYMBAL_MICS),
    ("china1/edge",       ["china"],                    CYMBAL_MICS),
    ("china2/edge",       ["crash_3"],                  CYMBAL_MICS),
    ("splash1/edge",      ["splash", "hat_splash"],     CYMBAL_MICS),
    ("splash2/edge",      ["perc"],                     CYMBAL_MICS),
    ("tom1/center",       ["tom_1"],                    SHELL_MICS),
    ("tom2/center",       ["tom_2"],                    SHELL_MICS),
    ("tom3/center",       ["tom_3"],                    SHELL_MICS),
    ("tom4/center",       ["tom_4"],                    SHELL_MICS),
]


# Longest tail we keep per family, in seconds. The library records cymbals out
# to six seconds of inaudible ring; that ring is most of the download.
TAIL_SECONDS = {"hat": 2.0, "ride": 4.5, "crash": 4.5, "china": 4.5,
                "splash": 3.0, "perc": 3.0}
SHELL_TAIL = 3.0
SILENCE = 10 ** (-72 / 20)
FADE_SECONDS = 0.06


def convert(src, dest, piece):
    """24-bit source -> dithered 16-bit FLAC with the dead tail cut off."""
    audio, rate = sf.read(src, dtype="float64", always_2d=True)
    limit = next((s for k, s in TAIL_SECONDS.items() if piece.startswith(k)),
                 SHELL_TAIL)

    loud = np.where(np.max(np.abs(audio), axis=1) > SILENCE)[0]
    end = int(loud[-1]) + 1 if len(loud) else len(audio)
    end = min(end, int(limit * rate), len(audio))

    fade = min(int(FADE_SECONDS * rate), end)
    audio = audio[:end].copy()
    audio[end - fade:] *= np.linspace(1.0, 0.0, fade)[:, None]

    # TPDF dither at one LSB, so ghost notes do not quantise into crackle.
    lsb = 1.0 / 32768.0
    rng = np.random.default_rng(zlib.crc32(os.path.basename(dest).encode()))
    audio += (rng.random(audio.shape) - rng.random(audio.shape)) * lsb
    sf.write(dest, np.clip(audio, -1.0, 1.0), rate, subtype="PCM_16")
    return os.path.getsize(dest)


def read_defines(path):
    txt = open(path).read()
    mic = re.search(r"#define \$MIC (\S+)", txt).group(1)
    art = re.search(r"#define \$ART (\S+)", txt).group(1)
    vels = [m.group(1) for m in re.finditer(r"#define \$VEL\d+ (\d+)", txt)]
    codes = dict(re.findall(r"#define \$CODE_(\d+) (\S+)", txt))
    return mic, art, vels, codes


def scan_groups(data_dir):
    """group name -> mic bucket -> sample names indexed by [layer][variant]."""
    kit = {}
    for gfile in sorted(os.listdir(os.path.join(data_dir, "group"))):
        piece, _, bucket = gfile[:-4].rpartition("_")
        text = open(os.path.join(data_dir, "group", gfile)).read()
        seen = {}
        for block in text.split("<group>")[1:]:
            label = re.search(r"group_label=\$(\w+)", block)
            code = re.search(r"file/([A-Z0-9]+)\.txt", block)
            region = re.search(r"region/v(\d)\.txt", block)
            if not (label and code and region):
                continue
            name = f"{piece}/{label.group(1)}"
            seen[name] = seen.get(name, 0) + 1
            if seen[name] > 1:
                name += f"#{seen[name]}"
            mic, art, vels, codes = read_defines(
                os.path.join(data_dir, "file", code.group(1) + ".txt"))
            per_pos = int(region.group(1))
            layers = []
            for layer, vel in enumerate(vels[:per_pos]):
                variants = []
                for pos in range(ROUND_ROBINS):
                    idx = pos * per_pos + layer + 1
                    if f"{idx:02d}" not in codes:
                        break
                    variants.append(
                        f"{mic}_{art}_{vel}_{RR_WORDS[pos]}-{codes[f'{idx:02d}']}.flac")
                layers.append(variants)
            kit.setdefault(name, {})[bucket] = layers
    return kit


def wanted_files(kit):
    """(source name, destination name) for every sample the kit needs."""
    out = {}
    for group, pieces, mics in PIECE_MAP:
        buckets = kit.get(group)
        if buckets is None:
            sys.exit(f"Naked Drums has no group {group}")
        for mic, candidates in mics.items():
            bucket = next((c for c in candidates if c in buckets), None)
            if bucket is None:
                continue
            for layer, variants in enumerate(buckets[bucket], start=1):
                for variant, source in enumerate(variants, start=1):
                    dest = f"{pieces[0]}_v{layer}_rr{variant}_{mic}.flac"
                    out[source] = dest
    return out


def build_manifest(kit, files):
    entries = []
    for group, pieces, mics in PIECE_MAP:
        buckets = kit[group]
        for mic, candidates in mics.items():
            bucket = next((c for c in candidates if c in buckets), None)
            if bucket is None:
                continue
            for layer, variants in enumerate(buckets[bucket], start=1):
                for variant, source in enumerate(variants, start=1):
                    for piece in pieces:
                        entries.append(dict(piece=piece, layer=layer,
                                            variant=variant, mic=mic,
                                            file=files[source]))
    return dict(name="Naked Rock", version="1",
                source="Naked Drums by Wilkinson Audio, CC-BY 4.0",
                pieces=entries)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo")
    ap.add_argument("out")
    ap.add_argument("--fetch", action="store_true",
                    help="git-checkout the needed blobs from a blobless clone")
    args = ap.parse_args()

    base = os.path.join(args.repo, "Wilkinson Audio", "Naked Drums")
    kit = scan_groups(os.path.join(base, "Data"))
    files = wanted_files(kit)
    print(f"{len(files)} samples across {len(PIECE_MAP)} articulations")

    if args.fetch:
        paths = [f"Wilkinson Audio/Naked Drums/Samples/{n}" for n in files]
        for i in range(0, len(paths), 200):
            subprocess.run(["git", "-C", args.repo, "checkout", "HEAD", "--"]
                           + paths[i:i + 200], check=True)

    os.makedirs(args.out, exist_ok=True)
    total = 0
    for source, dest in sorted(files.items()):
        src = os.path.join(base, "Samples", source)
        if not os.path.exists(src):
            sys.exit(f"missing sample {source} (run with --fetch?)")
        total += convert(src, os.path.join(args.out, dest), dest)

    manifest = build_manifest(kit, files)
    with open(os.path.join(args.out, "kit.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    shutil.copyfile(os.path.join(args.repo, "LICENSE"),
                    os.path.join(args.out, "LICENSE-NakedDrums.txt"))
    print(f"{total / 1e6:.0f} MB written to {args.out}, "
          f"{len(manifest['pieces'])} kit.json entries")


if __name__ == "__main__":
    main()
