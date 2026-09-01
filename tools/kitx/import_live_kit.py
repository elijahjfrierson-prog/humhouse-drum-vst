#!/usr/bin/env python3
"""Import a live-recorded kit: one take per piece per dynamic.

The takes are single strokes captured with the room open and a lot of leading
silence, so each one is trimmed to its attack and left to ring out to the end of
the recording - the ring is the part that stops a sample sounding plastic.

The layers are *not* level matched. What makes a real kit read as played rather
than triggered is that the softest stroke is 15-25 dB under the hardest and
darker with it, so the measured levels are kept and only nudged where a harder
take came back quieter than a softer one.

Pieces the session did not cover (hats, rim, cross-stick, china) are named from a
kit that already ships, which the loader resolves through "samples".

    python3 tools/kitx/import_live_kit.py <take-folder> [--out content/Kits/NuRock]
"""

import argparse
import json
import os
import re
import sys

import numpy as np
import soundfile as sf

# take name -> (piece, layer name)
LAYER_ORDER = ["light", "light medium", "medium", "heavy", "heaviest"]

PIECES = {
    "kick": "kick",
    "snare": "snare",
    "small tom": "tom_1",
    "floor tom": "tom_3",
    "ride": "ride",
    "left crash": "left_crash",
    "right crash": "right_crash",
}

# a piece the takes cover also stands in for its close relatives, so a 30-lane
# performance still plays on a seven-piece session
STAND_IN = {
    "tom_1": ["tom_2"],
    "tom_3": ["tom_4"],
    "ride": ["ride_bell", "ride_edge", "ride_crash"],
    "left_crash": ["crash_3"],
    "snare": ["snare_rim", "sidestick"],
}

# and the pieces no take covers are named out of the kit we borrow from
BORROWED = ["hat_closed", "hat_tight", "hat_open_1", "hat_open_2", "hat_open_3",
            "hat_open_4", "hat_pedal", "china"]

TAIL_DB = 62.0          # how far down a tail is followed before it is cut
PRE_ROLL = 0.004        # seconds kept in front of the attack


def parse_take(name):
    stem = os.path.splitext(os.path.basename(name))[0].lower()
    stem = re.sub(r"[_ ]*\d+$", "", stem.replace("_", " ")).strip()
    for piece_name, piece in PIECES.items():
        if stem.startswith(piece_name):
            layer = stem[len(piece_name):].strip()
            if layer in LAYER_ORDER:
                return piece, layer
    return None, None


def trim(path):
    x, sr = sf.read(path, dtype="float32", always_2d=True)
    mono = np.abs(x.mean(axis=1))
    win = max(1, int(sr * 0.003))
    sm = np.convolve(mono, np.ones(win, dtype=np.float32) / win, mode="same")
    peak = float(sm.max())
    if peak <= 0.0:
        return None, sr, 0.0

    over = np.nonzero(sm > peak * 0.10)[0]
    start = max(0, int(over[0]) - int(sr * PRE_ROLL))
    quiet = peak * 10 ** (-TAIL_DB / 20.0)
    ringing = np.nonzero(sm > quiet)[0]
    end = min(len(mono), int(ringing[-1]) + int(sr * 0.05))

    seg = np.array(x[start:end], dtype=np.float32)
    fade = min(len(seg), int(sr * 0.02))
    seg[-fade:] *= np.linspace(1.0, 0.0, fade, dtype=np.float32)[:, None]
    return seg, sr, float(np.abs(seg).max())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("takes")
    ap.add_argument("--out", default="content/Kits/NuRock")
    ap.add_argument("--name", default="Nu Rock")
    ap.add_argument("--borrow", default="../MuldjordMetal")
    ap.add_argument("--source", default="Recorded for HumHouse, supplied by the user")
    args = ap.parse_args()

    takes = {}
    for entry in sorted(os.listdir(args.takes)):
        if not entry.lower().endswith(".wav"):
            continue
        piece, layer = parse_take(entry)
        if piece is None:
            print(f"skipped {entry}: no piece in the name", file=sys.stderr)
            continue
        takes.setdefault(piece, {}).setdefault(layer, []).append(
            os.path.join(args.takes, entry))

    if not takes:
        raise SystemExit("no takes found")

    os.makedirs(args.out, exist_ok=True)
    pieces, report = [], {}

    measured = {}
    for piece, layers in sorted(takes.items()):
        present = [l for l in LAYER_ORDER if l in layers]
        rows = []
        for index, layer in enumerate(present, start=1):
            for variant, path in enumerate(sorted(layers[layer]), start=1):
                seg, sr, peak = trim(path)
                if seg is None:
                    continue
                rows.append({"layer": index, "variant": variant, "seg": seg,
                             "sr": sr, "peak": peak, "seconds": len(seg) / sr,
                             "take": layer})

        # Two takes filed under one dynamic are only round robins of each other
        # if they were played at the same weight: a session marked "heavy" twice
        # that came back 13 dB apart is two layers, so it is read as two.
        for layer in sorted({r["layer"] for r in rows}):
            group = sorted([r for r in rows if r["layer"] == layer],
                           key=lambda r: r["peak"])
            quietest = 20.0 * np.log10(max(1e-9, group[0]["peak"]))
            for r in group:
                db = 20.0 * np.log10(max(1e-9, r["peak"]))
                r["split"] = 1 if db - quietest > 6.0 else 0
        if any(r["split"] for r in rows):
            for r in sorted(rows, key=lambda r: (r["layer"], r["split"])):
                r["layer"] = r["layer"] * 2 - 1 + r["split"]
            for index, layer in enumerate(sorted({r["layer"] for r in rows}), start=1):
                for r in rows:
                    if r["layer"] == layer:
                        r["renumbered"] = index
            for r in rows:
                r["layer"] = r.pop("renumbered")
        for layer in sorted({r["layer"] for r in rows}):
            for variant, r in enumerate([r for r in rows if r["layer"] == layer], start=1):
                r["variant"] = variant

        # keep the recorded dynamic ladder, but never let a harder layer come
        # back quieter than the one below it
        floor = None
        for layer in sorted({r["layer"] for r in rows}):
            group = [r for r in rows if r["layer"] == layer]
            db = float(np.median([20.0 * np.log10(max(1e-9, r["peak"])) for r in group]))
            lift = 0.0 if floor is None or db >= floor + 1.5 else floor + 1.5 - db
            for r in group:
                r["gain"] = 10.0 ** (lift / 20.0)
            floor = db + lift
        measured[piece] = rows

    # The session was tracked with headroom to spare, so the whole kit comes up
    # by one gain: within a piece and between pieces the recorded balance is
    # what a listener hears.
    loudest = max(r["peak"] * r["gain"] for rows in measured.values() for r in rows)
    lift = 0.891 / max(1e-9, loudest)
    print(f"kit lifted {20.0 * np.log10(lift):+.1f} dB to -1 dBFS")

    for piece, rows in measured.items():
        for r in rows:
            name = f"{piece}_v{r['layer']}_rr{r['variant']}_close.flac"
            sf.write(os.path.join(args.out, name), r["seg"] * r["gain"] * lift,
                     r["sr"], format="FLAC", subtype="PCM_24")
            for target in [piece] + STAND_IN.get(piece, []):
                pieces.append({"piece": target, "layer": r["layer"],
                               "variant": r["variant"], "mic": "close",
                               "file": name})
        report[piece] = [{"take": r["take"], "layer": r["layer"],
                          "peak_db": round(20.0 * np.log10(
                              max(1e-9, r["peak"] * r["gain"] * lift)), 2),
                          "seconds": round(r["seconds"], 2)} for r in rows]
        print(f"{piece:12s} {len(rows)} takes -> layers "
              f"{sorted({r['layer'] for r in rows})}  "
              + " ".join(f"{d['peak_db']:.1f}dB/{d['seconds']:.2f}s"
                         for d in report[piece]))

    # The pieces this session did not cover come from a kit that already ships:
    # its own manifest says which recording plays each of them, so a borrowed
    # hat arrives voiced the way that kit voiced it.
    borrowed = os.path.join(os.path.dirname(args.out.rstrip("/")),
                            args.borrow.lstrip("./"))
    with open(os.path.join(borrowed, "kit.json")) as f:
        lending = json.load(f)

    taken = {}
    for entry in lending["pieces"]:
        if entry["piece"] in BORROWED:
            pieces.append(entry)
            taken[entry["piece"]] = taken.get(entry["piece"], 0) + 1
    for piece in BORROWED:
        print(f"{piece:12s} {taken.get(piece, 0)} borrowed")

    manifest = {
        "name": args.name,
        "version": "1",
        "source": args.source,
        "samples": args.borrow,
        "trim": 0.0,
        "pieces": pieces,
    }
    with open(os.path.join(args.out, "kit.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    with open(os.path.join(args.out, "dynamics.json"), "w") as f:
        json.dump(report, f, indent=1)
    print(f"{len(pieces)} placements -> {args.out}/kit.json")


if __name__ == "__main__":
    main()
