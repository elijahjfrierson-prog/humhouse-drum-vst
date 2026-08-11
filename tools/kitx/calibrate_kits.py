#!/usr/bin/env python3
"""Level-match the kits.

Every kit is recorded and normalised by whoever made it, so swapping kits used
to change how loud the instrument is and how it sat in a mix. This measures the
loudest layer of the pieces a groove leans on and writes a ``trim`` in dB into
each ``kit.json`` so all kits arrive at the same working level.
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

# The pieces that carry a groove, and how much of the perceived level each one
# is responsible for.
WEIGHTS = {"kick": 0.34, "snare": 0.34, "hat_closed": 0.16, "ride": 0.08, "right_crash": 0.08}

# Levels are matched to one kit rather than to an absolute target: the kits
# already sit at a sensible working level, and the point of the trim is that
# swapping kits does not change how loud the instrument is.
REFERENCE_KIT = "NakedRock"


def piece_rms(kit_dir: Path, entries: list[dict]) -> float | None:
    """RMS of the loudest close-mic layer, averaged over its round robins."""
    close = [e for e in entries if e.get("mic") == "close"]
    if not close:
        return None
    top = max(e["layer"] for e in close)
    values = []
    for e in close:
        if e["layer"] != top:
            continue
        path = kit_dir / e["file"]
        if not path.exists():
            continue
        audio, rate = sf.read(str(path), always_2d=True, dtype="float32")
        head = audio[: int(0.2 * rate)]
        if head.size == 0:
            continue
        values.append(float(np.sqrt(np.mean(np.square(head)))))
    if not values:
        return None
    return float(np.mean(values))


def kit_level(kit_dir: Path) -> float:
    """Weighted dBFS RMS of the kit's loudest layers."""
    manifest = json.loads((kit_dir / "kit.json").read_text())
    by_piece: dict[str, list[dict]] = {}
    for entry in manifest["pieces"]:
        by_piece.setdefault(entry["piece"], []).append(entry)

    total_weight = 0.0
    level = 0.0
    for piece, weight in WEIGHTS.items():
        rms = piece_rms(kit_dir, by_piece.get(piece, []))
        if rms is None or rms <= 0.0:
            continue
        level += weight * 20.0 * math.log10(rms)
        total_weight += weight
    if total_weight <= 0.0:
        raise SystemExit(f"{kit_dir.name}: no measurable pieces")
    return level / total_weight


def write_trim(kit_dir: Path, trim: float) -> None:
    path = kit_dir / "kit.json"
    manifest = json.loads(path.read_text())
    manifest["trim"] = round(trim, 2)
    path.write_text(json.dumps(manifest, indent=1) + "\n")


def main() -> None:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "content/Kits")
    kits = sorted(p for p in root.iterdir() if (p / "kit.json").exists())
    levels = {kit.name: kit_level(kit) for kit in kits}
    reference = levels.get(REFERENCE_KIT, max(levels.values()))
    for kit in kits:
        trim = reference - levels[kit.name]
        write_trim(kit, trim)
        print(f"{kit.name}: {levels[kit.name]:.2f} dBFS -> trim {trim:+.2f} dB")


if __name__ == "__main__":
    main()
