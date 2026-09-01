#!/usr/bin/env python3
"""Give a kit its own snare, taken from another import.

A kit that borrows another kit's recordings ("samples" in its manifest) still
has to have its own snare: the snare is the drum a listener tells two kits
apart by, and re-tuning one recording twice gives you the same drum at two
pitches. This copies the snare family out of a second import, level-matches it
to the kit it is joining, and rewrites the manifest so those articulations play
the new files while everything else stays borrowed.

    ./graft_snare.py <snare-import> <target-kit> --gain-db 11
"""
import argparse
import glob
import json
import os
import shutil

import numpy as np
import soundfile as sf

FAMILY = ("snare", "snare_rim", "snare_ghost", "snare_flam", "snare_roll",
          "sidestick")
PREFIX = "graft_"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("target")
    ap.add_argument("--gain-db", type=float, default=0.0,
                    help="level match: kits are recorded at different levels")
    args = ap.parse_args()

    with open(os.path.join(args.source, "kit.json")) as f:
        source = json.load(f)
    manifest_path = os.path.join(args.target, "kit.json")
    with open(manifest_path) as f:
        target = json.load(f)

    for stale in glob.glob(os.path.join(args.target, PREFIX + "*.flac")):
        os.remove(stale)

    gain = 10.0 ** (args.gain_db / 20.0)
    entries, copied = [], set()
    for entry in source["pieces"]:
        if entry["piece"] not in FAMILY:
            continue
        name = PREFIX + entry["file"]
        if name not in copied:
            audio, rate = sf.read(os.path.join(args.source, entry["file"]),
                                  dtype="float64", always_2d=True)
            sf.write(os.path.join(args.target, name),
                     np.clip(audio * gain, -1.0, 1.0), rate, subtype="PCM_16")
            copied.add(name)
        entries.append(dict(entry, file=name))

    kept = [e for e in target["pieces"] if e["piece"] not in FAMILY]
    target["pieces"] = kept + entries
    target["source"] = target["source"] + " - snare: " + source["source"]
    with open(manifest_path, "w") as f:
        json.dump(target, f, indent=1)

    for licence in glob.glob(os.path.join(args.source, "LICENSE-*.txt")):
        shutil.copyfile(licence,
                        os.path.join(args.target, os.path.basename(licence)))

    print(f"{len(copied)} files, {len(entries)} entries grafted onto "
          f"{target['name']}")


if __name__ == "__main__":
    main()
